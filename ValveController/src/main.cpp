#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "motor_output.h"
#include "mqtt_config.h"
#include "valve_config.h"
#include "valve_control.h"

namespace {

// Network retries and MQTT topic names are shared by all four valves.
constexpr uint32_t kReconnectIntervalMs = 5000;
constexpr char kBaseTopic[] = MQTT_ROOT_TOPIC;
constexpr char kAvailabilityTopic[] = MQTT_ROOT_TOPIC "/availability";
static_assert(sizeof(kBaseTopic) < 80, "MQTT root topic is too long");

WiFiClient networkClient;
PubSubClient mqttClient(networkClient);
// The network task sends commands through queues; the motor task publishes atomic snapshots.
QueueHandle_t commandQueues[hardware::kValveCount] = {};
std::atomic<valve::Status> statuses[hardware::kValveCount];
valve::Status publishedStatuses[hardware::kValveCount] = {};
bool publishedValid[hardware::kValveCount] = {};
uint8_t nextStatusValve = 0;
uint32_t nextStatusAt = 0;
uint32_t nextChangeReportAt = 0;
uint32_t lastWifiAttemptAt = 0;
uint32_t lastMqttAttemptAt = 0;
bool wifiAttempted = false;
bool mqttAttempted = false;

// Limit contacts are active-low and depend on external pull-ups to 3.3 V.
valve::Limits readLimits(uint8_t index) {
    const hardware::ValvePins &pins = hardware::kValves[index];
    return {digitalRead(pins.openLimit) == LOW, digitalRead(pins.closedLimit) == LOW};
}

void applyMotor(uint8_t index, valve::Motion motion) {
    const hardware::ValvePins &pins = hardware::kValves[index];
    const motor::Output output = motor::outputFor(motion, pins.openDirection);
#if MOTOR_DRIVER == MOTOR_DRIVER_BTS7960
    // Disable the bridge and clear both PWM legs before applying a new direction.
    const uint8_t rightChannel = index * 2;
    const uint8_t leftChannel = rightChannel + 1;
    digitalWrite(pins.enable, LOW);
    ledcWriteChannel(rightChannel, 0);
    ledcWriteChannel(leftChannel, 0);
    if (!output.enabled) {
        return;
    }

    ledcWriteChannel(rightChannel, output.primaryDuty);
    ledcWriteChannel(leftChannel, output.secondaryDuty);
    digitalWrite(pins.enable, HIGH);
#else
    // RC controllers need continuous neutral pulses when stopped, not zero duty.
    ledcWriteChannel(index, output.primaryDuty);
#endif
}

// Only this high-priority task changes motion, so network delays cannot pause limit checks.
void motorTask(void *) {
    valve::Control controls[hardware::kValveCount] = {
        valve::Control(hardware::kReversalDelayMs, hardware::kMaxTravelMs),
        valve::Control(hardware::kReversalDelayMs, hardware::kMaxTravelMs),
        valve::Control(hardware::kReversalDelayMs, hardware::kMaxTravelMs),
        valve::Control(hardware::kReversalDelayMs, hardware::kMaxTravelMs),
    };
    valve::Motion applied[hardware::kValveCount] = {};

    for (;;) {
        const uint32_t now = millis();
        for (uint8_t index = 0; index < hardware::kValveCount; ++index) {
            const valve::Limits limits = readLimits(index);
            // Each one-element queue holds the latest request for this valve.
            valve::Command command;
            if (xQueueReceive(commandQueues[index], &command, 0) == pdTRUE) {
                controls[index].request(command, limits, now);
            }
            controls[index].update(limits, now);
            const valve::Motion motion = controls[index].motion();
            if (motion != applied[index]) {
                applyMotor(index, motion);
                applied[index] = motion;
            }
            statuses[index].store(controls[index].status(limits));
        }
        vTaskDelay(1);
    }
}

// Retain the latest state at the broker; advance the local cache only after success.
bool publishValveStatus(uint8_t index) {
    char topic[128];
    snprintf(topic, sizeof(topic), "%s/valve%u/state", kBaseTopic, index + 1);
    const valve::Status status = statuses[index].load();
    if (!mqttClient.publish(topic, valve::statusName(status), true)) {
        return false;
    }
    publishedStatuses[index] = status;
    publishedValid[index] = true;
    return true;
}

// Accept either command-topic form and leave all motion decisions to the motor task.
// PubSubClient does not expose the retain flag here; commands must not be retained.
void onMqttMessage(char *topic, byte *payload, unsigned int length) {
    const valve::Command command = valve::parseCommand(payload, length);
    if (command == valve::Command::Invalid) {
        return;
    }
    for (uint8_t index = 0; index < hardware::kValveCount; ++index) {
        char directTopic[128];
        char setTopic[128];
        snprintf(directTopic, sizeof(directTopic), "%s/valve%u", kBaseTopic, index + 1);
        snprintf(setTopic, sizeof(setTopic), "%s/valve%u/set", kBaseTopic, index + 1);
        if (strcmp(topic, directTopic) == 0 || strcmp(topic, setTopic) == 0) {
            xQueueOverwrite(commandQueues[index], &command);
            return;
        }
    }
}

// Rate-limit connection attempts rather than waiting in a blocking reconnect loop.
void maintainWifi(uint32_t now) {
    if (WiFi.status() == WL_CONNECTED) {
        return;
    }
    if (!wifiAttempted || now - lastWifiAttemptAt >= kReconnectIntervalMs) {
        wifiAttempted = true;
        lastWifiAttemptAt = now;
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    }
}

// Register an offline last will, restore subscriptions, and republish states on reconnect.
void maintainMqtt(uint32_t now) {
    if (WiFi.status() != WL_CONNECTED || mqttClient.connected()) {
        return;
    }
    if (mqttAttempted && now - lastMqttAttemptAt < kReconnectIntervalMs) {
        return;
    }
    mqttAttempted = true;
    lastMqttAttemptAt = now;
    bool connected;
#if MQTT_AUTH_ENABLED
    connected = mqttClient.connect(MQTT_CLIENT_ID, MQTT_USERNAME, MQTT_PASSWORD,
                                   kAvailabilityTopic, 0, true, "offline");
#else
    connected = mqttClient.connect(MQTT_CLIENT_ID, kAvailabilityTopic, 0, true, "offline");
#endif
    if (connected) {
        const bool directSubscribed = mqttClient.subscribe(MQTT_ROOT_TOPIC "/+", 1);
        const bool setSubscribed = mqttClient.subscribe(MQTT_ROOT_TOPIC "/+/set", 1);
        if (!directSubscribed || !setSubscribed ||
            !mqttClient.publish(kAvailabilityTopic, "online", true)) {
            mqttClient.disconnect();
            return;
        }
        for (uint8_t index = 0; index < hardware::kValveCount; ++index) {
            publishedValid[index] = false;
        }
    }
}

// Report changes promptly and refresh one valve per interval in round-robin order.
// Failed periodic publications retry without advancing to the next valve.
void reportStatuses(uint32_t now) {
    if (!mqttClient.connected()) {
        return;
    }
    if (static_cast<int32_t>(now - nextChangeReportAt) >= 0) {
        for (uint8_t index = 0; index < hardware::kValveCount; ++index) {
            if (!publishedValid[index] || statuses[index].load() != publishedStatuses[index]) {
                publishValveStatus(index);
            }
        }
        nextChangeReportAt = now + 100;
    }
    if (static_cast<int32_t>(now - nextStatusAt) >= 0) {
        if (publishValveStatus(nextStatusValve)) {
            nextStatusValve = (nextStatusValve + 1) % hardware::kValveCount;
            nextStatusAt = now + hardware::kStatusIntervalMs;
        } else {
            nextStatusAt = now + 2000;
        }
    }
}

// Startup failures stop initialization before the motion task can start.
[[noreturn]] void halt(const char *reason) {
    Serial.println(reason);
    for (;;) {
        delay(1000);
    }
}

}

void setup() {
    Serial.begin(115200);
    // Reject invalid wiring before configuring motor outputs.
    if (!hardware::validPinMap()) {
        halt("Invalid valve GPIO map; controller disabled.");
    }

#if MOTOR_DRIVER == MOTOR_DRIVER_BTS7960
    // Hold every bridge disabled before attaching any of its PWM channels.
    for (const hardware::ValvePins &pins : hardware::kValves) {
        digitalWrite(pins.enable, LOW);
        pinMode(pins.enable, OUTPUT);
    }
#endif
    // Initialize stopped outputs, limit inputs, and command queues for each valve.
    for (uint8_t index = 0; index < hardware::kValveCount; ++index) {
        const hardware::ValvePins &pins = hardware::kValves[index];
#if MOTOR_DRIVER == MOTOR_DRIVER_BTS7960
        digitalWrite(pins.rightPwm, LOW);
        digitalWrite(pins.leftPwm, LOW);
        pinMode(pins.rightPwm, OUTPUT);
        pinMode(pins.leftPwm, OUTPUT);
        for (uint8_t side = 0; side < 2; ++side) {
            const uint8_t channel = index * 2 + side;
            if (!ledcAttachChannel(side == 0 ? pins.rightPwm : pins.leftPwm,
                                   hardware::kPwmFrequencyHz,
                                   hardware::kPwmResolutionBits, channel)) {
                halt("PWM setup failed; controller disabled.");
            }
            ledcWriteChannel(channel, 0);
        }
#else
        digitalWrite(pins.signalPwm, LOW);
        pinMode(pins.signalPwm, OUTPUT);
        if (!ledcAttachChannel(pins.signalPwm, hardware::kPwmFrequencyHz,
                               hardware::kPwmResolutionBits, index)) {
            halt("RC PWM setup failed; controller disabled.");
        }
        applyMotor(index, valve::Motion::Stopped);
#endif
        pinMode(pins.openLimit, INPUT);
        pinMode(pins.closedLimit, INPUT);
        commandQueues[index] = xQueueCreate(1, sizeof(valve::Command));
        if (commandQueues[index] == nullptr) {
            halt("Command queue allocation failed; controller disabled.");
        }
        statuses[index].store(valve::Control().status(readLimits(index)));
    }
    // Start independent motor supervision before enabling network activity.
    if (xTaskCreatePinnedToCore(motorTask, "valve-motors", 4096, nullptr, 3,
                              nullptr, 1) != pdPASS) {
        halt("Motor task creation failed; controller disabled.");
    }

    // Configure the network client; loop() handles connections and message traffic.
    WiFi.mode(WIFI_STA);
    mqttClient.setServer(MQTT_BROKER_HOST, MQTT_BROKER_PORT);
    mqttClient.setCallback(onMqttMessage);
    mqttClient.setSocketTimeout(1);
    mqttClient.setKeepAlive(30);
    mqttClient.setBufferSize(512);
    nextStatusAt = millis() + hardware::kStatusIntervalMs;
}

// Networking runs separately from motion supervision; outages do not stop limit monitoring.
void loop() {
    const uint32_t now = millis();
    maintainWifi(now);
    maintainMqtt(now);
    if (mqttClient.connected()) {
        mqttClient.loop();
    }
    reportStatuses(millis());
    delay(2);
}