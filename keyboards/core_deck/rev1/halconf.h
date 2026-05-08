// Copyright 2024 vden
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include_next <halconf.h>

/* Enable I2C driver for PCF8574 button matrix */
#undef HAL_USE_I2C
#define HAL_USE_I2C TRUE

/* Enable PWM driver for display backlight control */
#undef HAL_USE_PWM
#define HAL_USE_PWM TRUE
