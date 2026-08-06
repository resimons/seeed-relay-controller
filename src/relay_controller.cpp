#include "relay_controller.h"

#include "driver/gpio.h"
#include "esp_log.h"

#define RELAY_GPIO_PIN GPIO_NUM_1

static const char *TAG = "relay_controller";
static bool relay_state = false;

void relay_controller_setup(void)
{
    gpio_config_t relay_config = {
        .pin_bit_mask = 1ULL << RELAY_GPIO_PIN,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&relay_config);
    gpio_set_level(RELAY_GPIO_PIN, relay_state);
}

void relay_controller_set_state(bool on)
{
    relay_state = on;
    gpio_set_level(RELAY_GPIO_PIN, relay_state);
    ESP_LOGI(TAG, "relay %s", relay_state ? "ON" : "OFF");
}

bool relay_controller_get_state(void)
{
    return relay_state;
}
