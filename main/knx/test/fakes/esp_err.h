#pragma once
using esp_err_t = int;
constexpr esp_err_t ESP_OK = 0;
constexpr esp_err_t ESP_FAIL = -1;
inline const char* esp_err_to_name(esp_err_t e) { return e == ESP_OK ? "ESP_OK" : "ESP_FAIL"; }
