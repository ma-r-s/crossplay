#pragma once
#include <stdint.h>
typedef int gpio_num_t; typedef int esp_err_t;
#define GPIO_NUM_0 0
#define GPIO_NUM_13 13
#define GPIO_MODE_OUTPUT 2
#define GPIO_MODE_INPUT 1
inline esp_err_t gpio_hold_en(gpio_num_t) { return 0; }
inline esp_err_t gpio_hold_dis(gpio_num_t) { return 0; }
inline esp_err_t gpio_deep_sleep_hold_en() { return 0; }
inline esp_err_t gpio_deep_sleep_hold_dis() { return 0; }
inline esp_err_t gpio_set_direction(gpio_num_t, int) { return 0; }
inline esp_err_t gpio_set_level(gpio_num_t, uint32_t) { return 0; }
inline int gpio_get_level(gpio_num_t) { return 0; }
inline esp_err_t gpio_reset_pin(gpio_num_t) { return 0; }
