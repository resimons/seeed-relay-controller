#pragma once

#include <stdbool.h>

void mqtt_client_manager_setup(void);
void mqtt_client_manager_publish_iamalive(void);
void mqtt_client_manager_publish_relay_state(bool relay_on);
bool mqtt_client_manager_get_pending_command(bool *relay_on);
