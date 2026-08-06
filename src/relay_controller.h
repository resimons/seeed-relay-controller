#pragma once

#include <stdbool.h>

void relay_controller_setup(void);
void relay_controller_set_state(bool on);
bool relay_controller_get_state(void);
