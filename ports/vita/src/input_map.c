#include "input_map.h"

/* Apply a 16-count dead zone, then rescale each direction to full deflection. */
static float normalize_axis(int delta) {
    const int dead_zone = 16;
    if (delta > -dead_zone && delta < dead_zone) return 0.0f;
    if (delta >= 0) {
        if (delta <= dead_zone) return 0.0f;
        if (delta > 127) delta = 127;
        return (float)(delta - dead_zone) / (127.0f - dead_zone);
    }
    if (delta >= -dead_zone) return 0.0f;
    if (delta < -128) delta = -128;
    return (float)(delta + dead_zone) / (128.0f - dead_zone);
}

TsfpVitaInput tsfp_vita_translate(TsfpVitaSample sample) {
    TsfpVitaInput output = {0};
    output.move_x = normalize_axis((int)sample.lx - 128);
    output.move_y = -normalize_axis((int)sample.ly - 128);
    output.look_x = normalize_axis((int)sample.rx - 128);
    output.look_y = -normalize_axis((int)sample.ry - 128);
    if (sample.buttons & TSFP_VITA_R) output.actions |= TSFP_ACTION_FIRE;
    if (sample.buttons & TSFP_VITA_L) output.actions |= TSFP_ACTION_AIM;
    if (sample.buttons & TSFP_VITA_CROSS) output.actions |= TSFP_ACTION_USE;
    if (sample.buttons & TSFP_VITA_SQUARE) output.actions |= TSFP_ACTION_RELOAD;
    if (sample.buttons & TSFP_VITA_CIRCLE) output.actions |= TSFP_ACTION_CROUCH;
    if (sample.buttons & TSFP_VITA_TRIANGLE) output.actions |= TSFP_ACTION_NEXT_WEAPON;
    if (sample.buttons & TSFP_VITA_START) output.actions |= TSFP_ACTION_PAUSE;
    if (sample.buttons & TSFP_VITA_DPAD_LEFT) output.actions |= TSFP_ACTION_PREVIOUS_WEAPON;
    if (sample.buttons & TSFP_VITA_DPAD_RIGHT) output.actions |= TSFP_ACTION_NEXT_WEAPON;
    return output;
}
