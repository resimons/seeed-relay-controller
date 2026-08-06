#pragma once

#include <stddef.h>

#define DEVICE_IDENTITY_ID_LEN 23
#define DEVICE_IDENTITY_MAC_STR_LEN 18

void device_identity_get_id(char *out, size_t out_len);
void device_identity_get_mac(char *out, size_t out_len);
