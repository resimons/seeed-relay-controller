#include "mqtt_client_manager.h"
#include "mqtt_broker_config.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "device_identity.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "mqtt_client.h"

#define MQTT_CONNECT_TIMEOUT_MS 10000
#define MQTT_CONNECTED_BIT (1 << 0)
#define MQTT_PAYLOAD_MAX_LEN 160

static const char *TAG = "mqtt_client_manager";
static esp_mqtt_client_handle_t s_client;
static EventGroupHandle_t s_mqtt_event_group;
static QueueHandle_t s_command_queue;

static void mqtt_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
static void handle_command_data(const char *data, int data_len);
static bool extract_json_string_field(const char *json, const char *field_name, char *out, size_t out_len);
static int8_t get_rssi(void);

void mqtt_client_manager_setup(void)
{
    s_mqtt_event_group = xEventGroupCreate();
    // Length-1 queue holding only the latest desired relay state; xQueueOverwrite
    // means a burst of commands collapses to "apply the most recent one".
    s_command_queue = xQueueCreate(1, sizeof(bool));

    char broker_uri[64];
    snprintf(broker_uri, sizeof(broker_uri), MQTT_BROKER_USE_TLS ? "mqtts://%s:%d" : "mqtt://%s:%d",
             MQTT_BROKER_HOST, MQTT_BROKER_PORT);

    esp_mqtt_client_config_t mqtt_config = {};
    mqtt_config.broker.address.uri = broker_uri;
    mqtt_config.credentials.username = MQTT_BROKER_USERNAME;
    mqtt_config.credentials.authentication.password = MQTT_BROKER_PASSWORD;
#if MQTT_BROKER_USE_TLS
#if MQTT_BROKER_VERIFY_CERTIFICATE
    // Validate the broker's server certificate against this root CA.
    mqtt_config.broker.verification.certificate = MQTT_BROKER_ROOT_CA;
#else
    // No root CA set: esp-tls falls back to MBEDTLS_SSL_VERIFY_NONE, accepting
    // any server certificate. TLS still encrypts the connection either way.
#endif
    // Mutual TLS: present this device's own certificate/key to the broker.
    mqtt_config.credentials.authentication.certificate = MQTT_BROKER_CERTIFICATE;
    mqtt_config.credentials.authentication.key = MQTT_BROKER_PRIVATE_KEY;
#endif

    s_client = esp_mqtt_client_init(&mqtt_config);
    esp_mqtt_client_register_event(s_client, MQTT_EVENT_CONNECTED, mqtt_event_handler, NULL);
    esp_mqtt_client_register_event(s_client, MQTT_EVENT_DATA, mqtt_event_handler, NULL);
    esp_mqtt_client_start(s_client);

    ESP_LOGI(TAG, "connecting to MQTT broker %s", broker_uri);

    EventBits_t bits = xEventGroupWaitBits(s_mqtt_event_group, MQTT_CONNECTED_BIT, pdFALSE, pdFALSE,
                                            pdMS_TO_TICKS(MQTT_CONNECT_TIMEOUT_MS));

    if (bits & MQTT_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to MQTT broker");
    } else {
        ESP_LOGE(TAG, "timed out connecting to MQTT broker %s", broker_uri);
    }
}

void mqtt_client_manager_publish_heartbeat(void)
{
    char device_id[DEVICE_IDENTITY_ID_LEN];
    char mac_str[DEVICE_IDENTITY_MAC_STR_LEN];
    device_identity_get_id(device_id, sizeof(device_id));
    device_identity_get_mac(mac_str, sizeof(mac_str));

    char payload[MQTT_PAYLOAD_MAX_LEN];
    int payload_len = snprintf(payload, sizeof(payload),
                                "{\"device\":\"%s\",\"manufacturer\":\"xiao\",\"device_type\":\"esp32c6\",\"type\":\"heartbeat\",\"mac\":\"%s\",\"rssi\":%d}",
                                device_id, mac_str, get_rssi());

    esp_mqtt_client_publish(s_client, MQTT_TOPIC_HEARTBEAT, payload, payload_len, 1, 0);
    ESP_LOGI(TAG, "published to '%s': %s", MQTT_TOPIC_HEARTBEAT, payload);
}

void mqtt_client_manager_publish_relay_state(bool relay_on)
{
    char device_id[DEVICE_IDENTITY_ID_LEN];
    device_identity_get_id(device_id, sizeof(device_id));

    char payload[MQTT_PAYLOAD_MAX_LEN];
    int payload_len = snprintf(payload, sizeof(payload), "{\"state\":\"%s\",\"device\":\"%s\"}",
                                relay_on ? "on" : "off", device_id);

    // Retained so a subscriber (or this device on reconnect) can read the last known state immediately.
    esp_mqtt_client_publish(s_client, MQTT_TOPIC_RELAY_STATE, payload, payload_len, 1, 1);
    ESP_LOGI(TAG, "published to '%s': %s", MQTT_TOPIC_RELAY_STATE, payload);
}

bool mqtt_client_manager_get_pending_command(bool *relay_on)
{
    return xQueueReceive(s_command_queue, relay_on, 0) == pdTRUE;
}

static void mqtt_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    if (event_id == MQTT_EVENT_CONNECTED) {
        xEventGroupSetBits(s_mqtt_event_group, MQTT_CONNECTED_BIT);
        esp_mqtt_client_subscribe(s_client, MQTT_TOPIC_RELAY_COMMAND, 1);
        ESP_LOGI(TAG, "subscribed to '%s'", MQTT_TOPIC_RELAY_COMMAND);
    } else if (event_id == MQTT_EVENT_DATA) {
        handle_command_data(event->data, event->data_len);
    }
}

static void handle_command_data(const char *data, int data_len)
{
    char buf[MQTT_PAYLOAD_MAX_LEN];
    int copy_len = data_len < (int)sizeof(buf) - 1 ? data_len : (int)sizeof(buf) - 1;
    memcpy(buf, data, copy_len);
    buf[copy_len] = '\0';

    // The command topic is shared by every relay controller on the broker, so each
    // device must ignore commands addressed to a different device id.
    char command_device_id[DEVICE_IDENTITY_ID_LEN];
    if (!extract_json_string_field(buf, "device", command_device_id, sizeof(command_device_id))) {
        ESP_LOGW(TAG, "ignoring command payload with no 'device' field: %s", buf);
        return;
    }

    char own_device_id[DEVICE_IDENTITY_ID_LEN];
    device_identity_get_id(own_device_id, sizeof(own_device_id));

    if (strcasecmp(command_device_id, own_device_id) != 0) {
        ESP_LOGD(TAG, "ignoring command for device '%s' (this device is '%s')", command_device_id, own_device_id);
        return;
    }

    for (int i = 0; buf[i] != '\0'; i++) {
        buf[i] = (char)tolower((unsigned char)buf[i]);
    }

    bool desired_state;
    if (strstr(buf, "\"off\"") != NULL) {
        desired_state = false;
    } else if (strstr(buf, "\"on\"") != NULL) {
        desired_state = true;
    } else {
        ESP_LOGW(TAG, "ignoring unrecognized command payload: %s", buf);
        return;
    }

    xQueueOverwrite(s_command_queue, &desired_state);
}

static bool extract_json_string_field(const char *json, const char *field_name, char *out, size_t out_len)
{
    char key[32];
    snprintf(key, sizeof(key), "\"%s\":\"", field_name);

    const char *start = strstr(json, key);
    if (start == NULL) {
        return false;
    }
    start += strlen(key);

    const char *end = strchr(start, '"');
    if (end == NULL) {
        return false;
    }

    size_t value_len = (size_t)(end - start);
    if (value_len >= out_len) {
        value_len = out_len - 1;
    }
    memcpy(out, start, value_len);
    out[value_len] = '\0';
    return true;
}

static int8_t get_rssi(void)
{
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) != ESP_OK) {
        return 0;
    }
    return ap_info.rssi;
}
