#pragma once
#include "FreeRTOS.h"
inline BaseType_t xTaskCreate(void (*)(void*), const char*, unsigned, void*, unsigned,
                              TaskHandle_t* task) { *task = nullptr; return pdPASS; }
inline BaseType_t xTaskNotify(TaskHandle_t, uint32_t, int) { return pdPASS; }
inline BaseType_t xTaskNotifyWait(uint32_t, uint32_t, uint32_t*, TickType_t) { return pdFALSE; }
