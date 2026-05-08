// Copyright 2024 vden
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include_next <mcuconf.h>

/* Enable I2C0 for PCF8574 button matrix on GP4 (SDA) / GP5 (SCL) */
#undef RP_I2C_USE_I2C0
#define RP_I2C_USE_I2C0 TRUE

/* Enable PWM0 for display backlight control on GP0 */
#undef RP_PWM_USE_PWM0
#define RP_PWM_USE_PWM0 TRUE
