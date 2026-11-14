#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/dhcpv4.h>
#include <zephyr/net/mqtt.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/sys/atomic.h>

#include "command_topic.h"
#include "network.h"
#include "valve_hardware.h"

LOG_MODULE_REGISTER(valve_network, LOG_LEVEL_INF);

namespace {

constexpr char root[] = CONFIG_APP_MQTT_ROOT;
constexpr char availability[] = CONFIG_APP_MQTT_ROOT "/availability";
constexpr int64_t reconnectMs = 5000;
static_assert(sizeof(root) <= 80, "MQTT root must be at most 79 characters");

net_mgmt_event_callback wifiCallback;
atomic_t wifiConnected;
atomic_t mqttReady;
net_if *wifiInterface;
mqtt_client client;
sockaddr_in broker;
uint8_t rxBuffer[512];
uint8_t txBuffer[512];
bool connected;
bool subscribed;
int sessionError;
int64_t pingDeadline;
unsigned nextValve;
int64_t nextPeriodic;
valve::Status published[motor::kValveCount];
bool publishedValid[motor::kValveCount];
K_MUTEX_DEFINE(displayStatusMutex);
network::DisplaySnapshot publishedDisplayStatus;

/*
 * Convert a NUL-terminated application string to the length-delimited MQTT representation.
 * Args: text - source string whose storage must remain valid while the MQTT object is in use.
 * Returns: MQTT UTF-8 view of the string; it does not copy the bytes.
 */
mqtt_utf8 utf8(const char *text) {
    return {reinterpret_cast<const uint8_t *>(text), static_cast<uint32_t>(strlen(text))};
}

/*
 * Track station connection events and start/stop DHCP with the link.
 * Args: callback - event callback carrying the Wi-Fi result; event - event identifier;
 * iface - interface that generated the event.
 * Returns: None.
 */
void wifiEvent(net_mgmt_event_callback *callback, uint64_t event, net_if *iface) {
    if (iface != wifiInterface) return;
    if (event == NET_EVENT_WIFI_CONNECT_RESULT) {
        const auto *result = static_cast<const wifi_status *>(callback->info);
        if (result == nullptr || result->status != 0) {
            atomic_clear(&wifiConnected);
            atomic_clear(&mqttReady);
            LOG_WRN("Wi-Fi connection failed: %d", result ? result->status : -EIO);
            return;
        }
        atomic_set(&wifiConnected, 1);
        /* DHCP starts only after successful association; a link alone is not broker-ready. */
        net_dhcpv4_start(iface);
        LOG_INF("Wi-Fi connected; waiting for DHCP");
    } else if (event == NET_EVENT_WIFI_DISCONNECT_RESULT) {
        atomic_clear(&wifiConnected);
        atomic_clear(&mqttReady);
        /* Stop DHCP so an old lease is not mistaken for a usable connection. */
        net_dhcpv4_stop(iface);
        LOG_WRN("Wi-Fi disconnected");
    }
}

/*
 * Require both an associated Wi-Fi link and a preferred IPv4 address before opening MQTT.
 * Args: None.
 * Returns: True when the station can route broker traffic.
 */
bool networkReady() {
    if (wifiInterface == nullptr || atomic_get(&wifiConnected) == 0) return false;
    const in_addr *address = net_if_ipv4_get_global_addr(wifiInterface, NET_ADDR_PREFERRED);
    return address != nullptr && !net_ipv4_is_addr_unspecified(address);
}

/*
 * Cache Wi-Fi/IP status under a mutex for the lower-priority display thread.
 * Args: None.
 * Returns: None.
 */
void updateDisplaySnapshot() {
    network::DisplaySnapshot snapshot{};
    if (networkReady()) {
        const in_addr *address = net_if_ipv4_get_global_addr(wifiInterface, NET_ADDR_PREFERRED);
        if (address != nullptr && !net_ipv4_is_addr_unspecified(address)) {
            snapshot.wifiUp = true;
            memcpy(snapshot.ipv4, address->s4_addr, sizeof(snapshot.ipv4));
        }
    }
    k_mutex_lock(&displayStatusMutex, K_FOREVER);
    publishedDisplayStatus = snapshot;
    k_mutex_unlock(&displayStatusMutex);
}

/*
 * Request a station connection using the configured open-network or WPA2 credentials.
 * Args: None.
 * Returns: None; request failures are logged and retried by runNetwork.
 */
void requestWifi() {
    wifi_connect_req_params params{};
    params.ssid = reinterpret_cast<const uint8_t *>(CONFIG_APP_WIFI_SSID);
    params.ssid_length = strlen(CONFIG_APP_WIFI_SSID);
    params.psk = reinterpret_cast<const uint8_t *>(CONFIG_APP_WIFI_PASSWORD);
    params.psk_length = strlen(CONFIG_APP_WIFI_PASSWORD);
    params.channel = WIFI_CHANNEL_ANY;
    params.band = WIFI_FREQ_BAND_2_4_GHZ;
    params.security = params.psk_length ? WIFI_SECURITY_TYPE_PSK : WIFI_SECURITY_TYPE_NONE;
    params.mfp = WIFI_MFP_OPTIONAL;
    params.timeout = 10;
    const int result = net_mgmt(NET_REQUEST_WIFI_CONNECT, wifiInterface, &params, sizeof(params));
    if (result < 0 && result != -EALREADY) LOG_WRN("Wi-Fi request failed: %d", result);
}

/*
 * Publish a retained QoS 0 text value, used for availability and valve states.
 * Args: topic - destination topic; text - NUL-terminated payload.
 * Returns: Zero on success or a negative MQTT error.
 */
int publishText(const char *topic, const char *text) {
    uint8_t payload[16];
    const size_t length = strlen(text);
    if (length > sizeof(payload)) return -EMSGSIZE;
    memcpy(payload, text, length);
    mqtt_publish_param param{};
    param.message.topic.topic = utf8(topic);
    param.message.topic.qos = MQTT_QOS_0_AT_MOST_ONCE;
    param.message.payload = {payload, static_cast<uint32_t>(length)};
    param.retain_flag = 1;
    return mqtt_publish(&client, &param);
}

/*
 * Read a complete fragmented publish payload before the MQTT stream is used again.
 * Args: payload - destination buffer; length - exact payload size to read.
 * Returns: Zero when complete, or a negative socket/MQTT error or timeout.
 */
int readPayload(uint8_t *payload, size_t length) {
    size_t read = 0;
    const int64_t deadline = k_uptime_get() + 1000;
    while (read < length) {
        const int result = mqtt_read_publish_payload(&client, payload + read, length - read);
        if (result > 0) {
            read += result;
        } else if (result == 0) {
            return -ECONNRESET;
        } else if (result != -EAGAIN) {
            return result;
        } else {
            if (k_uptime_get() >= deadline) return -ETIMEDOUT;
            zsock_pollfd fd{client.transport.tcp.sock, ZSOCK_POLLIN, 0};
            const int polled = zsock_poll(&fd, 1, 50);
            if (polled < 0) return -errno;
            if (fd.revents & (ZSOCK_POLLERR | ZSOCK_POLLHUP | ZSOCK_POLLNVAL)) {
                return -ECONNRESET;
            }
        }
    }
    return 0;
}

/*
 * Match, validate, and queue one valve command, acknowledging QoS 1 only after reading it.
 * Args: param - decoded MQTT publish metadata and payload length.
 * Returns: None; a fatal read/ack error is recorded in sessionError.
 */
void receiveCommand(const mqtt_publish_param &param) {
    const mqtt_utf8 &topic = param.message.topic.topic;
    const int index = valve::commandValve(topic.utf8, topic.size, root);
    /* Disconnect on unsupported QoS or oversized input rather than desynchronizing MQTT payload reads. */
    if (param.message.payload.len > sizeof(rxBuffer) ||
        param.message.topic.qos > MQTT_QOS_1_AT_LEAST_ONCE) {
        LOG_WRN("Rejecting oversized command or unexpected QoS");
        sessionError = -EMSGSIZE;
        return;
    }
    uint8_t payload[sizeof(rxBuffer)]{};
    sessionError = readPayload(payload, param.message.payload.len);
    if (sessionError < 0) return;
    const valve::Command command = valve::parseCommand(payload, param.message.payload.len);
    if (index >= 0 && command != valve::Command::Invalid) {
        if (param.retain_flag) LOG_WRN("Retained command replay received; clear retained commands");
        hardware::submit(static_cast<unsigned>(index), command);
    } else if (index >= 0) {
        LOG_WRN("Ignoring invalid command topic or payload");
    }
    if (param.message.topic.qos == MQTT_QOS_1_AT_LEAST_ONCE) {
        /* Acknowledge only after consuming the payload, so redelivery remains possible on read failure. */
        mqtt_puback_param ack{};
        ack.message_id = param.message_id;
        sessionError = mqtt_publish_qos1_ack(&client, &ack);
    }
}

/*
 * Update connection/subscription state and dispatch incoming publishes.
 * Args: client - MQTT client associated with the event; event - decoded event and result.
 * Returns: None; errors are saved for serviceMqtt to terminate the session.
 */
void mqttEvent(mqtt_client *, const mqtt_evt *event) {
    if (event->result != 0) {
        atomic_clear(&mqttReady);
        sessionError = event->result < 0 ? event->result : -ECONNREFUSED;
        return;
    }
    switch (event->type) {
    case MQTT_EVT_CONNACK:
        if (event->param.connack.return_code != MQTT_CONNECTION_ACCEPTED) {
            atomic_clear(&mqttReady);
            sessionError = -ECONNREFUSED;
        } else {
            connected = true;
        }
        break;
    case MQTT_EVT_DISCONNECT:
        connected = false;
        atomic_clear(&mqttReady);
        sessionError = -ECONNRESET;
        break;
    case MQTT_EVT_SUBACK: {
        const mqtt_binstr &codes = event->param.suback.return_codes;
        /* Require both command filters to be accepted before publishing availability or state. */
        if (event->param.suback.message_id != 1 || codes.len != 2 ||
            codes.data[0] > MQTT_SUBACK_SUCCESS_QoS_1 ||
            codes.data[1] > MQTT_SUBACK_SUCCESS_QoS_1) {
            LOG_ERR("Broker rejected command subscriptions");
            atomic_clear(&mqttReady);
            sessionError = -EACCES;
        } else {
            subscribed = true;
        }
        break;
    }
    case MQTT_EVT_PUBLISH:
        receiveCommand(event->param.publish);
        break;
    default:
        break;
    }
}

/*
 * Poll the MQTT socket without blocking valve supervision, then process packets and keepalive.
 * Args: None.
 * Returns: Zero while healthy, or a negative error to end and reconnect the session.
 */
int serviceMqtt() {
    if (!networkReady()) return -ENETDOWN;
    zsock_pollfd fd{client.transport.tcp.sock, ZSOCK_POLLIN, 0};
    const int result = zsock_poll(&fd, 1, 100);
    if (result < 0) return -errno;
    if (fd.revents & (ZSOCK_POLLERR | ZSOCK_POLLHUP | ZSOCK_POLLNVAL)) return -ECONNRESET;
    if (fd.revents & ZSOCK_POLLIN) {
        const int input = mqtt_input(&client);
        if (input < 0 && input != -EAGAIN) return input;
    }
    if (sessionError < 0) return sessionError;
    if (connected) {
        /* mqtt_live sends keepalives; bound the time spent waiting for a broker ping response. */
        if (client.unacked_ping == 0) pingDeadline = 0;
        if (pingDeadline != 0 && k_uptime_get() >= pingDeadline) return -ETIMEDOUT;
        const int live = mqtt_live(&client);
        if (live < 0 && live != -EAGAIN) return live;
        if (client.unacked_ping > 0 && pingDeadline == 0) {
            pingDeadline = k_uptime_get() + 10000;
        }
    }
    return 0;
}

/*
 * Service MQTT until a connection or subscription callback sets the requested flag.
 * Args: flag - connection state to wait for.
 * Returns: Zero when set, or a negative session error/timeout.
 */
int waitFor(bool &flag) {
    const int64_t deadline = k_uptime_get() + 10000;
    while (!flag) {
        const int result = serviceMqtt();
        if (result < 0) return result;
        if (k_uptime_get() >= deadline) return -ETIMEDOUT;
    }
    return 0;
}

/*
 * Publish one retained valve state and update the local cache only after success.
 * Args: index - zero-based valve index.
 * Returns: Zero on success or a negative MQTT error.
 */
int publishStatus(unsigned index) {
    char topic[128];
    snprintf(topic, sizeof(topic), "%s/valve%u/state", root, index + 1);
    const valve::Status status = hardware::status(index);
    const int result = publishText(topic, valve::statusName(status));
    if (result == 0) {
        published[index] = status;
        publishedValid[index] = true;
    }
    return result;
}

/*
 * Publish all changed states promptly and refresh one valve periodically in round-robin order.
 * Args: None.
 * Returns: Zero on success or a negative MQTT error; failed periodic reports retry without advancing.
 */
int reportStatuses() {
    for (unsigned index = 0; index < motor::kValveCount; ++index) {
        if (!publishedValid[index] || hardware::status(index) != published[index]) {
            const int result = publishStatus(index);
            if (result < 0) return result;
        }
    }
    const int64_t now = k_uptime_get();
    if (now >= nextPeriodic) {
        const int result = publishStatus(nextValve);
        if (result < 0) {
            nextPeriodic = now + 2000;
            return result;
        }
        nextValve = (nextValve + 1) % motor::kValveCount;
        nextPeriodic = now + motor::kStatusIntervalMs;
    }
    return 0;
}

/*
 * Resolve the configured broker to an IPv4 socket address and release resolver storage.
 * Args: None.
 * Returns: Zero on success or a negative DNS/address error.
 */
int resolveBroker() {
    zsock_addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    zsock_addrinfo *addresses = nullptr;
    const int result = zsock_getaddrinfo(CONFIG_APP_MQTT_HOST, nullptr, &hints, &addresses);
    if (result != 0) {
        LOG_WRN("Broker DNS lookup failed: %d", result);
        return -EHOSTUNREACH;
    }
    if (addresses == nullptr || addresses->ai_addrlen != sizeof(broker)) {
        if (addresses != nullptr) zsock_freeaddrinfo(addresses);
        return -EADDRNOTAVAIL;
    }
    memcpy(&broker, addresses->ai_addr, sizeof(broker));
    broker.sin_port = htons(CONFIG_APP_MQTT_PORT);
    zsock_freeaddrinfo(addresses);
    return 0;
}

/*
 * Configure a clean MQTT session, establish subscriptions, and service it until failure.
 * The retained offline will is registered before connecting and takes effect when the socket aborts.
 * Args: None.
 * Returns: Zero only for a normal session end, otherwise a negative network/MQTT error.
 */
int mqttSession() {
    atomic_clear(&mqttReady);
    int result = resolveBroker();
    if (result < 0) return result;
    mqtt_client_init(&client);
    connected = false;
    subscribed = false;
    sessionError = 0;
    pingDeadline = 0;
    client.broker = &broker;
    client.evt_cb = mqttEvent;
    client.client_id = utf8(CONFIG_APP_MQTT_CLIENT_ID);
    client.protocol_version = MQTT_VERSION_3_1_1;
    client.transport.type = MQTT_TRANSPORT_NON_SECURE;
    client.rx_buf = rxBuffer;
    client.rx_buf_size = sizeof(rxBuffer);
    client.tx_buf = txBuffer;
    client.tx_buf_size = sizeof(txBuffer);
    client.keepalive = 30;
    client.clean_session = 1;
    /* Broker publishes retained offline if this connection disappears without a graceful disconnect. */
    mqtt_topic willTopic{utf8(availability), MQTT_QOS_0_AT_MOST_ONCE};
    mqtt_utf8 willMessage = utf8("offline");
    client.will_topic = &willTopic;
    client.will_message = &willMessage;
    client.will_retain = 1;
#if defined(CONFIG_APP_MQTT_AUTH)
    mqtt_utf8 username = utf8(CONFIG_APP_MQTT_USERNAME);
    mqtt_utf8 password = utf8(CONFIG_APP_MQTT_PASSWORD);
    client.user_name = &username;
    client.password = &password;
#endif
    result = mqtt_connect(&client);
    if (result < 0) return result;
    result = waitFor(connected);
    if (result == 0) {
        /* Subscribe to both supported command forms at QoS 1 before advertising availability. */
        mqtt_topic topics[] = {
            {utf8(CONFIG_APP_MQTT_ROOT "/+"), MQTT_QOS_1_AT_LEAST_ONCE},
            {utf8(CONFIG_APP_MQTT_ROOT "/+/set"), MQTT_QOS_1_AT_LEAST_ONCE},
        };
        mqtt_subscription_list subscriptions{};
        subscriptions.list = topics;
        subscriptions.list_count = ARRAY_SIZE(topics);
        subscriptions.message_id = 1;
        result = mqtt_subscribe(&client, &subscriptions);
    }
    if (result == 0) result = waitFor(subscribed);
    if (result == 0) result = publishText(availability, "online");
    if (result == 0) {
        memset(publishedValid, 0, sizeof(publishedValid));
        atomic_set(&mqttReady, 1);
        LOG_INF("MQTT connected and subscribed");
    }
    while (result == 0) {
        result = reportStatuses();
        if (result == 0) result = serviceMqtt();
    }
    atomic_clear(&mqttReady);
    /* Abort so the broker publishes the offline will, rather than leaving "online" retained. */
    const int aborted = mqtt_abort(&client);
    if (aborted < 0 && aborted != -ENOTCONN) LOG_WRN("MQTT socket cleanup failed: %d", aborted);
    return result;
}

/*
 * Reject missing or malformed Wi-Fi, broker, client-ID, and MQTT-root settings before connecting.
 * Args: None.
 * Returns: True when required values and supported lengths are valid.
 */
bool validConfiguration() {
    const size_t ssidLength = strlen(CONFIG_APP_WIFI_SSID);
    const size_t passwordLength = strlen(CONFIG_APP_WIFI_PASSWORD);
    if (ssidLength == 0 || ssidLength > 32 ||
        (passwordLength != 0 && (passwordLength < 8 || passwordLength > 63)) ||
        strlen(CONFIG_APP_MQTT_HOST) == 0 || strlen(CONFIG_APP_MQTT_CLIENT_ID) == 0 ||
        root[0] == '\0' || root[sizeof(root) - 2] == '/' || strpbrk(root, "+#") != nullptr) {
        LOG_ERR("Missing/invalid network configuration; copy local.conf.example to local.conf");
        return false;
    }
    return true;
}

}

namespace network {

/*
 * Read a coherent Wi-Fi/IP snapshot and derive MQTT readiness from the live session flag.
 * Returns: Current connection state and copied IPv4 octets.
 */
DisplaySnapshot displaySnapshot() {
    k_mutex_lock(&displayStatusMutex, K_FOREVER);
    DisplaySnapshot snapshot = publishedDisplayStatus;
    k_mutex_unlock(&displayStatusMutex);

    snapshot.wifiUp = snapshot.wifiUp && atomic_get(&wifiConnected) != 0;
    snapshot.mqttUp = snapshot.wifiUp && atomic_get(&mqttReady) != 0;
    if (!snapshot.wifiUp) memset(snapshot.ipv4, 0, sizeof(snapshot.ipv4));
    return snapshot;
}

}

/*
 * Validate configuration, connect Wi-Fi, and keep MQTT sessions alive with bounded retries.
 * Motion supervision runs in a separate thread throughout network outages and reconnect attempts.
 * Args: None.
 * Returns: None; the manager retries indefinitely after startup.
 */
void runNetwork() {
    if (!validConfiguration()) return;
    wifiInterface = net_if_get_wifi_sta();
    if (wifiInterface == nullptr) {
        LOG_ERR("No Wi-Fi station interface; networking disabled");
        return;
    }
    net_mgmt_init_event_callback(&wifiCallback, wifiEvent,
        NET_EVENT_WIFI_CONNECT_RESULT | NET_EVENT_WIFI_DISCONNECT_RESULT);
    net_mgmt_add_event_callback(&wifiCallback);
    nextPeriodic = k_uptime_get() + motor::kStatusIntervalMs;
    int64_t nextWifiAttempt = 0;
    int64_t nextMqttAttempt = 0;
    /* Retry association and broker sessions independently so Wi-Fi loss cannot stall motor checks. */
    for (;;) {
        updateDisplaySnapshot();
        const int64_t now = k_uptime_get();
        if (!atomic_get(&wifiConnected) && now >= nextWifiAttempt) {
            requestWifi();
            nextWifiAttempt = k_uptime_get() + reconnectMs;
        }
        if (networkReady() && now >= nextMqttAttempt) {
            const int result = mqttSession();
            LOG_WRN("MQTT session ended: %d; retrying", result);
            nextMqttAttempt = k_uptime_get() + reconnectMs;
        }
        k_sleep(K_MSEC(100));
    }
}
