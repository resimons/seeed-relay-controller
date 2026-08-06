#include "device_identity.h"

#include <stdint.h>
#include <stdio.h>
#include "esp_mac.h"
#include "esp_wifi.h"

void device_identity_get_id(char *out, size_t out_len)
{
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    // Byte order reversed to match the MCUDEVICE-<id> convention used by the existing device fleet.
    snprintf(out, out_len, "MCUDEVICE-%02X%02X%02X%02X%02X%02X",
              mac[5], mac[4], mac[3], mac[2], mac[1], mac[0]);
}

void device_identity_get_mac(char *out, size_t out_len)
{
    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    snprintf(out, out_len, "%02x:%02x:%02x:%02x:%02x:%02x",
              mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}
