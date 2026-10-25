#ifndef TEST_CONFIG_H
#define TEST_CONFIG_H
#define ESP_CONFIG_LOCAL_H
#ifdef TEST_NO_WIFI
#define ESP_WIFI_SSID ""
#else
#define ESP_WIFI_SSID "demo,\"ssid\\"
#endif
#define ESP_WIFI_PASSWORD "synthetic-test-password"
#include "../../BSP/debug_config.h"
#undef DEBUG_OUTPUT_ENABLE
#define DEBUG_OUTPUT_ENABLE 0
#endif
