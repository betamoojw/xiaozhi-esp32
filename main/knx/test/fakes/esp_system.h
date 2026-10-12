#pragma once
#include <cstdlib>
[[noreturn]] inline void esp_system_abort(const char*) { std::abort(); }
