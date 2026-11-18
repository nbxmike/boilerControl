#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/dhcpv4.h>
#include <zephyr/net/mqtt.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/sys/atomic.h>

#include "app.h"
#include "config_secrets.h"

LOG_MODULE_REGISTER(router_network, LOG_LEVEL_INF);

BUILD_ASSERT(sizeof(WIFI_SSID) > 1 && sizeof(WIFI_SSID) <= 33,
	     "WIFI_SSID must contain 1 to 32 bytes");
BUILD_ASSERT(sizeof(WIFI_PASSWORD) == 1 ||
	     (sizeof(WIFI_PASSWORD) >= 9 && sizeof(WIFI_PASSWORD) <= 64),
	     "Use an empty Wi-Fi password for an open network, or 8 to 63 bytes for WPA2");
BUILD_ASSERT(sizeof(MQTT_BROKER_HOST) > 1, "MQTT_BROKER_HOST must not be empty");
BUILD_ASSERT(MQTT_BROKER_PORT > 0 && MQTT_BROKER_PORT <= 65535,
	     "MQTT_BROKER_PORT must be between 1 and 65535");

static const char availability[] = "thermostat-router/availability";
static struct net_mgmt_event_callback wifi_callback;
static struct net_if *wifi_interface;
static atomic_t wifi_connected;
static atomic_t mqtt_ready;
static struct mqtt_client client;
static struct sockaddr_in broker;
static uint8_t rx_buffer[512];
static uint8_t tx_buffer[512];
static bool connected;
static bool subscribed;
static int session_error;
static int64_t ping_deadline;
static struct report_schedule reports;

static struct mqtt_utf8 utf8(const char *text)
{
	return (struct mqtt_utf8) {
		.utf8 = (const uint8_t *)text,
		.size = strlen(text),
	};
}

void app_network_status(bool *wifi, bool *mqtt)
{
	*wifi = atomic_get(&wifi_connected) != 0;
	*mqtt = *wifi && atomic_get(&mqtt_ready) != 0;
}

static void wifi_event(struct net_mgmt_event_callback *callback, uint64_t event,
		       struct net_if *iface)
{
	if (iface != wifi_interface) {
		return;
	}
	if (event == NET_EVENT_WIFI_CONNECT_RESULT) {
		const struct wifi_status *status = callback->info;

		if (status == NULL || status->status != 0) {
			atomic_clear(&wifi_connected);
			atomic_clear(&mqtt_ready);
			LOG_WRN("Wi-Fi connection failed: %d", status ? status->status : -EIO);
			return;
		}
		atomic_set(&wifi_connected, 1);
		net_dhcpv4_start(iface);
		LOG_INF("Wi-Fi associated; waiting for DHCP");
	} else if (event == NET_EVENT_WIFI_DISCONNECT_RESULT) {
		atomic_clear(&wifi_connected);
		atomic_clear(&mqtt_ready);
		net_dhcpv4_stop(iface);
		LOG_WRN("Wi-Fi disconnected");
	}
}

static bool network_ready(void)
{
	if (atomic_get(&wifi_connected) == 0) {
		return false;
	}
	const struct in_addr *address =
		net_if_ipv4_get_global_addr(wifi_interface, NET_ADDR_PREFERRED);

	return address != NULL && !net_ipv4_is_addr_unspecified(address);
}

static void request_wifi(void)
{
	struct wifi_connect_req_params params = {
		.ssid = (const uint8_t *)WIFI_SSID,
		.ssid_length = strlen(WIFI_SSID),
		.psk = (const uint8_t *)WIFI_PASSWORD,
		.psk_length = strlen(WIFI_PASSWORD),
		.channel = WIFI_CHANNEL_ANY,
		.band = WIFI_FREQ_BAND_2_4_GHZ,
		.security = sizeof(WIFI_PASSWORD) > 1 ?
			WIFI_SECURITY_TYPE_PSK : WIFI_SECURITY_TYPE_NONE,
		.mfp = WIFI_MFP_OPTIONAL,
		.timeout = 10,
	};
	int result = net_mgmt(NET_REQUEST_WIFI_CONNECT, wifi_interface, &params, sizeof(params));

	if (result < 0 && result != -EALREADY) {
		LOG_WRN("Wi-Fi request failed: %d", result);
	}
}

static int publish_text(const char *topic, const char *text)
{
	struct mqtt_publish_param param = {0};

	param.message.topic.topic = utf8(topic);
	param.message.topic.qos = MQTT_QOS_0_AT_MOST_ONCE;
	param.message.payload.data = (uint8_t *)text;
	param.message.payload.len = strlen(text);
	param.retain_flag = 1;
	return mqtt_publish(&client, &param);
}

static int publish_zone(unsigned int zone)
{
	return publish_text(router_state_topics[zone], router_mode_name(app_zone_mode(zone)));
}

static int read_payload(uint8_t *payload, size_t length)
{
	size_t received = 0;
	int64_t deadline = k_uptime_get() + 2000;

	while (received < length) {
		int result = mqtt_read_publish_payload(&client, payload + received,
						       length - received);

		if (result > 0) {
			received += result;
		} else if (result == 0) {
			return -ECONNRESET;
		} else if (result != -EAGAIN) {
			return result;
		} else {
			if (k_uptime_get() >= deadline) {
				return -ETIMEDOUT;
			}
			struct zsock_pollfd fd = {
				.fd = client.transport.tcp.sock, .events = ZSOCK_POLLIN,
			};
			int polled = zsock_poll(&fd, 1, 50);

			if (polled < 0) {
				return -errno;
			}
			if (fd.revents & (ZSOCK_POLLERR | ZSOCK_POLLHUP | ZSOCK_POLLNVAL)) {
				return -ECONNRESET;
			}
		}
	}
	return 0;
}

static void receive_command(const struct mqtt_publish_param *param)
{
	int zone = router_command_zone(param->message.topic.topic.utf8,
				       param->message.topic.topic.size);
	uint8_t payload[64];

	/* Abort oversized/unsupported packets rather than leave unread bytes in the stream. */
	if (param->message.payload.len > sizeof(payload) ||
	    param->message.topic.qos > MQTT_QOS_1_AT_LEAST_ONCE) {
		LOG_WRN("Rejecting oversized command or unsupported QoS");
		session_error = -EMSGSIZE;
		return;
	}
	session_error = read_payload(payload, param->message.payload.len);
	if (session_error < 0) {
		return;
	}
	enum zone_mode mode = router_parse_mode(payload, param->message.payload.len);

	if (zone < 0 || mode == ZONE_INVALID) {
		LOG_WRN("Ignoring invalid command topic or payload");
	} else {
		if (param->retain_flag) {
			LOG_WRN("Retained command replay; use non-retained commands for reset bypass");
		}
		session_error = app_set_zone((unsigned int)zone, mode);
		if (session_error < 0) {
			return;
		}
		LOG_INF("Zone %u: %s", (unsigned int)zone + 1, router_mode_name(mode));
	}
	if (param->message.topic.qos == MQTT_QOS_1_AT_LEAST_ONCE) {
		struct mqtt_puback_param ack = {.message_id = param->message_id};

		session_error = mqtt_publish_qos1_ack(&client, &ack);
		if (session_error < 0) {
			return;
		}
	}
	/* Even repeated commands get an immediate retained state response. */
	if (zone >= 0 && mode != ZONE_INVALID) {
		session_error = publish_zone((unsigned int)zone);
	}
}

static void mqtt_event(struct mqtt_client *mqtt, const struct mqtt_evt *event)
{
	ARG_UNUSED(mqtt);
	if (event->result != 0) {
		session_error = event->result < 0 ? event->result : -ECONNREFUSED;
		atomic_clear(&mqtt_ready);
		return;
	}
	switch (event->type) {
	case MQTT_EVT_CONNACK:
		if (event->param.connack.return_code != MQTT_CONNECTION_ACCEPTED) {
			session_error = -ECONNREFUSED;
		} else {
			connected = true;
		}
		break;
	case MQTT_EVT_DISCONNECT:
		connected = false;
		atomic_clear(&mqtt_ready);
		session_error = -ECONNRESET;
		break;
	case MQTT_EVT_SUBACK: {
		const struct mqtt_binstr *codes = &event->param.suback.return_codes;

		if (event->param.suback.message_id != 1 || codes->len != ROUTER_ZONE_COUNT) {
			session_error = -EPROTO;
			break;
		}
		for (unsigned int i = 0; i < codes->len; ++i) {
			if (codes->data[i] > MQTT_SUBACK_SUCCESS_QoS_1) {
				LOG_ERR("Broker rejected zone %u subscription", i + 1);
				session_error = -EACCES;
				return;
			}
		}
		subscribed = true;
		break;
	}
	case MQTT_EVT_PUBLISH:
		receive_command(&event->param.publish);
		break;
	default:
		break;
	}
}

static int service_mqtt(void)
{
	if (!network_ready()) {
		return -ENETDOWN;
	}
	struct zsock_pollfd fd = {.fd = client.transport.tcp.sock, .events = ZSOCK_POLLIN};
	int result = zsock_poll(&fd, 1, 100);

	if (result < 0) {
		return -errno;
	}
	if (fd.revents & (ZSOCK_POLLERR | ZSOCK_POLLHUP | ZSOCK_POLLNVAL)) {
		return -ECONNRESET;
	}
	if (fd.revents & ZSOCK_POLLIN) {
		result = mqtt_input(&client);
		if (result < 0 && result != -EAGAIN) {
			return result;
		}
	}
	if (session_error < 0) {
		return session_error;
	}
	if (connected) {
		if (client.unacked_ping == 0) {
			ping_deadline = 0;
		}
		if (ping_deadline != 0 && k_uptime_get() >= ping_deadline) {
			return -ETIMEDOUT;
		}
		result = mqtt_live(&client);
		if (result < 0 && result != -EAGAIN) {
			return result;
		}
		if (client.unacked_ping > 0 && ping_deadline == 0) {
			ping_deadline = k_uptime_get() + 10000;
		}
	}
	return 0;
}

static int wait_for(const bool *flag)
{
	int64_t deadline = k_uptime_get() + 10000;

	while (!*flag) {
		int result = service_mqtt();

		if (result < 0) {
			return result;
		}
		if (k_uptime_get() >= deadline) {
			return -ETIMEDOUT;
		}
	}
	return 0;
}

static int resolve_broker(void)
{
	struct zsock_addrinfo hints = {.ai_family = AF_INET, .ai_socktype = SOCK_STREAM};
	struct zsock_addrinfo *addresses = NULL;
	int result = zsock_getaddrinfo(MQTT_BROKER_HOST, NULL, &hints, &addresses);

	if (result != 0) {
		LOG_WRN("Broker DNS lookup failed: %d", result);
		return -EHOSTUNREACH;
	}
	if (addresses == NULL || addresses->ai_addrlen != sizeof(broker)) {
		if (addresses != NULL) {
			zsock_freeaddrinfo(addresses);
		}
		return -EADDRNOTAVAIL;
	}
	memcpy(&broker, addresses->ai_addr, sizeof(broker));
	broker.sin_port = htons(MQTT_BROKER_PORT);
	zsock_freeaddrinfo(addresses);
	return 0;
}

static int mqtt_session(void)
{
	int result = resolve_broker();

	if (result < 0) {
		return result;
	}
	mqtt_client_init(&client);
	connected = false;
	subscribed = false;
	session_error = 0;
	ping_deadline = 0;
	client.broker = &broker;
	client.evt_cb = mqtt_event;
	client.protocol_version = MQTT_VERSION_3_1_1;
	client.transport.type = MQTT_TRANSPORT_NON_SECURE;
	client.rx_buf = rx_buffer;
	client.rx_buf_size = sizeof(rx_buffer);
	client.tx_buf = tx_buffer;
	client.tx_buf_size = sizeof(tx_buffer);
	client.keepalive = 30;
	client.clean_session = 1;

	const struct net_linkaddr *mac = net_if_get_link_addr(wifi_interface);
	char client_id[48];

	if (mac->len != 6) {
		return -EADDRNOTAVAIL;
	}
	snprintf(client_id, sizeof(client_id), "thermostat-router-%02x%02x%02x%02x%02x%02x",
		 mac->addr[0], mac->addr[1], mac->addr[2], mac->addr[3], mac->addr[4], mac->addr[5]);
	client.client_id = utf8(client_id);
	struct mqtt_topic will_topic = {
		.topic = utf8(availability), .qos = MQTT_QOS_0_AT_MOST_ONCE,
	};
	struct mqtt_utf8 will_message = utf8("offline");

	client.will_topic = &will_topic;
	client.will_message = &will_message;
	client.will_retain = 1;
#if MQTT_AUTH_ENABLED
	struct mqtt_utf8 username = utf8(MQTT_USERNAME);
	struct mqtt_utf8 password = utf8(MQTT_PASSWORD);

	client.user_name = &username;
	client.password = &password;
#endif
	result = mqtt_connect(&client);
	if (result < 0) {
		return result;
	}
	result = wait_for(&connected);
	if (result == 0) {
		struct mqtt_topic topics[ROUTER_ZONE_COUNT];

		for (unsigned int i = 0; i < ROUTER_ZONE_COUNT; ++i) {
			topics[i].topic = utf8(router_command_topics[i]);
			topics[i].qos = MQTT_QOS_1_AT_LEAST_ONCE;
		}
		struct mqtt_subscription_list subscriptions = {
			.list = topics, .list_count = ROUTER_ZONE_COUNT, .message_id = 1,
		};

		result = mqtt_subscribe(&client, &subscriptions);
	}
	if (result == 0) {
		result = wait_for(&subscribed);
	}
	for (unsigned int zone = 0; result == 0 && zone < ROUTER_ZONE_COUNT; ++zone) {
		result = publish_zone(zone);
	}
	if (result == 0) {
		result = publish_text(availability, "online");
	}
	if (result == 0) {
		atomic_set(&mqtt_ready, 1);
		LOG_INF("MQTT connected and subscribed");
	}
	while (result == 0) {
		result = service_mqtt();
		int64_t now = k_uptime_get();

		if (result == 0 && router_report_due(&reports, now)) {
			result = publish_zone(reports.zone);
			if (result == 0) {
				router_report_advance(&reports, now);
			}
		}
	}
	atomic_clear(&mqtt_ready);
	/* Abort rather than gracefully disconnect so the broker emits the offline will. */
	int aborted = mqtt_abort(&client);

	if (aborted < 0 && aborted != -ENOTCONN) {
		LOG_WRN("MQTT cleanup failed: %d", aborted);
	}
	return result;
}

void app_run_network(void)
{
	wifi_interface = net_if_get_wifi_sta();
	if (wifi_interface == NULL) {
		LOG_ERR("No Wi-Fi station interface; zones remain in bypass");
		return;
	}
	net_mgmt_init_event_callback(&wifi_callback, wifi_event,
				    NET_EVENT_WIFI_CONNECT_RESULT | NET_EVENT_WIFI_DISCONNECT_RESULT);
	net_mgmt_add_event_callback(&wifi_callback);
	router_report_start(&reports, k_uptime_get());
	int64_t next_wifi_attempt = 0;
	int64_t next_mqtt_attempt = 0;

	for (;;) {
		int64_t now = k_uptime_get();

		if (atomic_get(&wifi_connected) == 0 && now >= next_wifi_attempt) {
			request_wifi();
			next_wifi_attempt = k_uptime_get() + 15000;
		}
		if (network_ready() && now >= next_mqtt_attempt) {
			int result = mqtt_session();

			LOG_WRN("MQTT session ended: %d; retrying", result);
			next_mqtt_attempt = k_uptime_get() + 5000;
		}
		k_sleep(K_MSEC(100));
	}
}
