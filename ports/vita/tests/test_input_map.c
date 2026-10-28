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
    puts("Vita input mapping: all host tests passed");
    return 0;
}
