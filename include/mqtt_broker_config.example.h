#pragma once

// Copy this file to mqtt_broker_config.h and fill in your broker's details.
// mqtt_broker_config.h is gitignored so real broker details never get committed.
#define MQTT_BROKER_HOST "your-broker-host"
#define MQTT_BROKER_PORT 8883
#define MQTT_BROKER_USERNAME "your-username"
#define MQTT_BROKER_PASSWORD "your-password"
#define MQTT_TOPIC_IAMALIVE "devices/iamalive"
#define MQTT_TOPIC_RELAY_COMMAND "devices/relay/command"
#define MQTT_TOPIC_RELAY_STATE "devices/relay/state"

// Set to 0 to connect over plain "mqtt://" instead of "mqtts://" (no TLS at all,
// no certificates used). Leave at 1 for TLS with mutual authentication.
#define MQTT_BROKER_USE_TLS 1

// Only relevant when MQTT_BROKER_USE_TLS is 1. Set to 0 to skip validating the
// broker's server certificate against MQTT_BROKER_ROOT_CA (accepts any server
// certificate) — useful for a broker with a self-signed cert during local testing.
// Client mutual-auth certificate/key below are still presented either way.
#define MQTT_BROKER_VERIFY_CERTIFICATE 1

// Fill with the broker's CA certificate, PEM format, LINE ENDING preserved (\n per line).
#define MQTT_BROKER_ROOT_CA \
"-----BEGIN CERTIFICATE-----\n" \
"-----END CERTIFICATE-----\n"

// Fill with your certificate.pem.crt, PEM format, LINE ENDING preserved (\n per line).
#define MQTT_BROKER_CERTIFICATE \
"-----BEGIN CERTIFICATE-----\n" \
"-----END CERTIFICATE-----\n"

// Fill with your private.pem.key, PEM format, LINE ENDING preserved (\n per line).
#define MQTT_BROKER_PRIVATE_KEY \
"-----BEGIN PRIVATE KEY-----\n" \
"-----END PRIVATE KEY-----\n"

