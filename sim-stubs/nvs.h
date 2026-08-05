#pragma once
#include <stddef.h>
#include <stdint.h>
typedef uint32_t nvs_handle_t;
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_NVS_NOT_FOUND 0x1102
typedef enum { NVS_READONLY, NVS_READWRITE } nvs_open_mode_t;
inline esp_err_t nvs_open(const char*, nvs_open_mode_t, nvs_handle_t*) { return -1; }
inline esp_err_t nvs_get_u8(nvs_handle_t, const char*, uint8_t*) { return -1; }
inline esp_err_t nvs_set_u8(nvs_handle_t, const char*, uint8_t) { return -1; }
inline esp_err_t nvs_commit(nvs_handle_t) { return -1; }
inline void nvs_close(nvs_handle_t) {}
