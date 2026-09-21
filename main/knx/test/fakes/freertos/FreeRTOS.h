#pragma once
#include <cstdint>
using TickType_t = uint32_t;
using BaseType_t = int;
using TaskHandle_t = void*;
constexpr int pdPASS = 1, pdFALSE = 0, eSetBits = 0;
constexpr uint32_t portMAX_DELAY = UINT32_MAX;
#define pdMS_TO_TICKS(ms) (ms)
