/* Custom RGB matrix effect: constant dim glow + reactive burst on keypress.
 * Switches to red hue when alerts are active. */

#ifdef RGB_MATRIX_KEYREACTIVE_ENABLED

RGB_MATRIX_EFFECT(GLOW_REACTIVE)

#    ifdef RGB_MATRIX_CUSTOM_EFFECT_IMPLS

extern bool display_has_alerts(void);

#define GLOW_IDLE_TIMEOUT_MS 600000  /* 10 minutes */
#define GLOW_DIM_PERCENT     30      /* dim to 30% after idle */

bool GLOW_REACTIVE(effect_params_t* params) {
    RGB_MATRIX_USE_LIMITS(led_min, led_max);

    hsv_t    hsv     = rgb_matrix_config.hsv;
    uint8_t  max_v   = hsv.v;

    /* Dim after idle timeout */
    if (last_input_activity_elapsed() > GLOW_IDLE_TIMEOUT_MS) {
        max_v = max_v * GLOW_DIM_PERCENT / 100;
    }

    /* Override hue to red during active alerts */
    if (display_has_alerts()) {
        hsv.h = 0;
        hsv.s = 255;
    }
    uint16_t max_tick = 65535 / qadd8(rgb_matrix_config.speed, 1);
    uint8_t  glow    = max_v * 7 / 10;  /* 70% constant base glow */

    for (uint8_t i = led_min; i < led_max; i++) {
        RGB_MATRIX_TEST_LED_FLAGS();

        uint8_t val = glow;

        /* Find most recent keypress on this LED */
        uint16_t tick = max_tick;
        for (int8_t j = g_last_hit_tracker.count - 1; j >= 0; j--) {
            if (g_last_hit_tracker.index[j] == i && g_last_hit_tracker.tick[j] < tick) {
                tick = g_last_hit_tracker.tick[j];
                break;
            }
        }

        /* Reactive burst: hold at full brightness, then fade back to glow.
         * The hold period lets the burst show after the keycap bounces back. */
        uint16_t offset = scale16by8(tick, qadd8(rgb_matrix_config.speed, 1));
        uint8_t  hold   = 100; /* hold full brightness for ~100/256 of fade window */
        if (offset < 256) {
            uint8_t reactive;
            if (offset < hold) {
                reactive = max_v;  /* full brightness during hold */
            } else {
                reactive = scale8(255 - (uint8_t)((offset - hold) * 255 / (256 - hold)), max_v);
            }
            if (reactive > val) val = reactive;
        }

        hsv.v = val;
        rgb_t rgb = rgb_matrix_hsv_to_rgb(hsv);
        rgb_matrix_set_color(i, rgb.r, rgb.g, rgb.b);
    }

    return rgb_matrix_check_finished_leds(led_max);
}

#    endif // RGB_MATRIX_CUSTOM_EFFECT_IMPLS

#endif // RGB_MATRIX_KEYREACTIVE_ENABLED
