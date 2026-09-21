#pragma once
#include "esp_err.h"
#include <cstdint>
struct esp_netif_t {};
struct esp_netif_ip_info_t { struct { uint32_t addr; } ip; };
inline esp_err_t esp_netif_get_ip_info(esp_netif_t*, esp_netif_ip_info_t* info) {
    info->ip.addr = 1;
    return ESP_OK;
}
