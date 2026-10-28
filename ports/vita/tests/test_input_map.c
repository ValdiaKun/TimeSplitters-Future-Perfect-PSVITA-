#include "../src/input_map.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

static int close_to(float actual, float expected) {
    return fabsf(actual - expected) < 0.0001f;
}

int main(void) {
    TsfpVitaSample sample = {0, 128, 128, 128, 128};
    TsfpVitaInput result = tsfp_vita_translate(sample);
    assert(result.actions == 0);
    assert(close_to(result.move_x, 0.0f));
    assert(close_to(result.move_y, 0.0f));
    assert(close_to(result.look_x, 0.0f));
    assert(close_to(result.look_y, 0.0f));

    sample.lx = 143;
    sample.ly = 113;
    sample.rx = 113;
    sample.ry = 143;
    result = tsfp_vita_translate(sample);
    assert(close_to(result.move_x, 0.0f));
    assert(close_to(result.move_y, 0.0f));
    assert(close_to(result.look_x, 0.0f));
    assert(close_to(result.look_y, 0.0f));

    sample.lx = 255;
    sample.ly = 0;
    sample.rx = 0;
    sample.ry = 255;
    result = tsfp_vita_translate(sample);
    assert(close_to(result.move_x, 1.0f));
    assert(close_to(result.move_y, 1.0f));
    assert(close_to(result.look_x, -1.0f));
    assert(close_to(result.look_y, -1.0f));

    sample.buttons = TSFP_VITA_R | TSFP_VITA_L | TSFP_VITA_CROSS |
                     TSFP_VITA_SQUARE | TSFP_VITA_CIRCLE | TSFP_VITA_TRIANGLE |
                     TSFP_VITA_START | TSFP_VITA_DPAD_LEFT | TSFP_VITA_DPAD_RIGHT;
    result = tsfp_vita_translate(sample);
    assert(result.actions == ((1u << 8) - 1u));
    static const uint32_t physical[] = {
        TSFP_VITA_R, TSFP_VITA_L, TSFP_VITA_CROSS, TSFP_VITA_SQUARE,
        TSFP_VITA_CIRCLE, TSFP_VITA_TRIANGLE, TSFP_VITA_START,
        TSFP_VITA_DPAD_LEFT, TSFP_VITA_DPAD_RIGHT
    };
    static const uint32_t actions[] = {
        TSFP_ACTION_FIRE, TSFP_ACTION_AIM, TSFP_ACTION_USE, TSFP_ACTION_RELOAD,
        TSFP_ACTION_CROUCH, TSFP_ACTION_NEXT_WEAPON, TSFP_ACTION_PAUSE,
        TSFP_ACTION_PREVIOUS_WEAPON, TSFP_ACTION_NEXT_WEAPON
    };
    for (unsigned i = 0; i < sizeof(physical) / sizeof(physical[0]); ++i) {
        sample.buttons = physical[i];
        assert(tsfp_vita_translate(sample).actions == actions[i]);
    }
    sample.buttons = 0;
    float previous = -1.0f;
    for (unsigned i = 0; i < 256; ++i) {
        sample.lx = sample.ly = sample.rx = sample.ry = (uint8_t)i;
        result = tsfp_vita_translate(sample);
        assert(isfinite(result.move_x) && result.move_x >= -1 && result.move_x <= 1);
        assert(result.move_x >= previous);
        assert(close_to(result.move_x, result.look_x));
        assert(close_to(result.move_y, -result.move_x));
        assert(close_to(result.look_y, result.move_y));
        previous = result.move_x;
    }
    puts("Vita input mapping: all host tests passed");
    return 0;
}
