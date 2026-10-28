#ifndef TSFP_VITA_INPUT_MAP_H
#define TSFP_VITA_INPUT_MAP_H

#include <stdint.h>

/* Platform-neutral physical-button bits; the Vita adapter owns SCE_CTRL_* translation. */
enum TsfpVitaPhysicalButton {
    TSFP_VITA_R = 1u << 0,
    TSFP_VITA_L = 1u << 1,
    TSFP_VITA_CROSS = 1u << 2,
    TSFP_VITA_SQUARE = 1u << 3,
    TSFP_VITA_CIRCLE = 1u << 4,
    TSFP_VITA_TRIANGLE = 1u << 5,
    TSFP_VITA_START = 1u << 6,
    TSFP_VITA_DPAD_LEFT = 1u << 7,
    TSFP_VITA_DPAD_RIGHT = 1u << 8
};

enum TsfpVitaAction {
    TSFP_ACTION_FIRE = 1u << 0,
    TSFP_ACTION_AIM = 1u << 1,
    TSFP_ACTION_USE = 1u << 2,
    TSFP_ACTION_RELOAD = 1u << 3,
    TSFP_ACTION_CROUCH = 1u << 4,
    TSFP_ACTION_NEXT_WEAPON = 1u << 5,
    TSFP_ACTION_PAUSE = 1u << 6,
    TSFP_ACTION_PREVIOUS_WEAPON = 1u << 7
};

typedef struct TsfpVitaSample {
    uint32_t buttons;
    uint8_t lx, ly, rx, ry;
} TsfpVitaSample;

typedef struct TsfpVitaInput {
    uint32_t actions;
    float move_x, move_y;
    float look_x, look_y;
} TsfpVitaInput;

/* Pure translation: no VitaSDK, host handles or guest memory. */
TsfpVitaInput tsfp_vita_translate(TsfpVitaSample sample);

#endif
