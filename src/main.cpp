#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqtt_client_manager.h"
#include "relay_controller.h"
#include "wifi_manager.h"

#define COMMAND_POLL_INTERVAL_MS 100
#define HEARTBEAT_INTERVAL_MINUTES 90
#define HEARTBEAT_INTERVAL_MS (HEARTBEAT_INTERVAL_MINUTES * 60 * 1000)

extern "C" void app_main(void)
{
    relay_controller_setup();
    wifi_manager_setup();
    mqtt_client_manager_setup();
    mqtt_client_manager_publish_heartbeat();
    mqtt_client_manager_publish_relay_state(relay_controller_get_state());

    uint32_t ms_since_heartbeat = 0;

    while (1) {
        bool desired_state;
        if (mqtt_client_manager_get_pending_command(&desired_state)) {
            relay_controller_set_state(desired_state);
            mqtt_client_manager_publish_relay_state(desired_state);
        }

        ms_since_heartbeat += COMMAND_POLL_INTERVAL_MS;
        if (ms_since_heartbeat >= HEARTBEAT_INTERVAL_MS) {
            mqtt_client_manager_publish_heartbeat();
            ms_since_heartbeat = 0;
        }

        vTaskDelay(pdMS_TO_TICKS(COMMAND_POLL_INTERVAL_MS));
    }
}
