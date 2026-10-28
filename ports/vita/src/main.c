/* Diagnostic homebrew only: the TimeSplitters guest runtime is NOT linked. */
#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <vita2d.h>

#include "input_map.h"
#include "diagnostics.h"

static TsfpVitaSample sample_controller(SceCtrlData *pad) {
    TsfpVitaSample sample = {0, 128, 128, 128, 128};
    int result = sceCtrlPeekBufferPositive(0, pad, 1);
    static int failed;
    if (result <= 0) {
        if (!failed) tsfp_diagnostic(sceKernelGetProcessTimeWide(), "error", "controller_sample", result);
        failed = 1;
        *pad = (SceCtrlData){0};
        return sample;
    }
    if (failed) tsfp_diagnostic(sceKernelGetProcessTimeWide(), "info", "controller_recovered", result);
    failed = 0;

    sample.lx = pad->lx;
    sample.ly = pad->ly;
    sample.rx = pad->rx;
    sample.ry = pad->ry;
    if (pad->buttons & SCE_CTRL_RTRIGGER) sample.buttons |= TSFP_VITA_R;
    if (pad->buttons & SCE_CTRL_LTRIGGER) sample.buttons |= TSFP_VITA_L;
    if (pad->buttons & SCE_CTRL_CROSS) sample.buttons |= TSFP_VITA_CROSS;
    if (pad->buttons & SCE_CTRL_SQUARE) sample.buttons |= TSFP_VITA_SQUARE;
    if (pad->buttons & SCE_CTRL_CIRCLE) sample.buttons |= TSFP_VITA_CIRCLE;
    if (pad->buttons & SCE_CTRL_TRIANGLE) sample.buttons |= TSFP_VITA_TRIANGLE;
    if (pad->buttons & SCE_CTRL_START) sample.buttons |= TSFP_VITA_START;
    if (pad->buttons & SCE_CTRL_LEFT) sample.buttons |= TSFP_VITA_DPAD_LEFT;
    if (pad->buttons & SCE_CTRL_RIGHT) sample.buttons |= TSFP_VITA_DPAD_RIGHT;
    return sample;
}

int main(void) {
    tsfp_diagnostic(sceKernelGetProcessTimeWide(), "info", "bootstrap_start", 0);
    int result = vita2d_init();
    if (result < 0) {
        tsfp_diagnostic(sceKernelGetProcessTimeWide(), "error", "vita2d_init", result);
        return 1;
    }
    vita2d_set_clear_color(RGBA8(13, 20, 33, 255));
    vita2d_set_vblank_wait(1);
    vita2d_pgf *font = vita2d_load_default_pgf();
    if (!font) {
        tsfp_diagnostic(sceKernelGetProcessTimeWide(), "error", "default_font", -1);
        vita2d_fini();
        return 1;
    }
    result = sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
    if (result < 0) {
        tsfp_diagnostic(sceKernelGetProcessTimeWide(), "error", "controller_mode", result);
        vita2d_free_pgf(font);
        vita2d_fini();
        return 1;
    }
    tsfp_diagnostic(sceKernelGetProcessTimeWide(), "info", "diagnostic_ready", 0);

    SceCtrlData pad = {0};
    for (;;) {
        TsfpVitaSample sample = sample_controller(&pad);
        TsfpVitaInput input = tsfp_vita_translate(sample);
        if ((pad.buttons & (SCE_CTRL_SELECT | SCE_CTRL_START)) ==
            (SCE_CTRL_SELECT | SCE_CTRL_START)) break;

        vita2d_start_drawing();
        vita2d_clear_screen();
        vita2d_draw_rectangle(35, 105, 890, 2, RGBA8(72, 120, 185, 255));
        vita2d_draw_rectangle(330, 165, 300, 235, RGBA8(30, 40, 57, 255));
        vita2d_draw_line(480, 165, 480, 400, RGBA8(60, 85, 115, 255));
        vita2d_draw_line(330, 282, 630, 282, RGBA8(60, 85, 115, 255));
        float cross_x = 480.0f + input.look_x * 140.0f;
        float cross_y = 282.0f - input.look_y * 105.0f;
        vita2d_draw_rectangle(cross_x - 6.0f, cross_y - 1.0f, 12.0f, 2.0f,
                              RGBA8(125, 220, 180, 255));
        vita2d_draw_rectangle(cross_x - 1.0f, cross_y - 6.0f, 2.0f, 12.0f,
                              RGBA8(125, 220, 180, 255));
        if (font) {
            const unsigned int white = RGBA8(235, 241, 255, 255);
            const unsigned int soft = RGBA8(170, 190, 215, 255);
            vita2d_pgf_draw_text(font, 35, 55, white, 1.25f,
                                  "TimeSplitters Future Perfect - Vita Bootstrap");
            vita2d_pgf_draw_text(font, 35, 85, soft, 1.0f,
                                  "Vita controls + display test. GAME ENGINE NOT LINKED.");
            vita2d_pgf_draw_textf(font, 35, 440, white, 1.0f,
                                  "Move: %.2f %.2f     Look: %.2f %.2f",
                                  input.move_x, input.move_y, input.look_x, input.look_y);
            vita2d_pgf_draw_textf(font, 35, 468, soft, 1.0f,
                                  "Action flags: 0x%02X     R:Fire L:Aim X:Use []:Reload",
                                  (unsigned)input.actions);
            vita2d_pgf_draw_text(font, 35, 496, soft, 1.0f,
                                  "O:Crouch  Triangle:Next  D-pad:Weapon  SELECT+START:Exit");
        }
        vita2d_end_drawing();
        vita2d_swap_buffers();
    }
    vita2d_wait_rendering_done();
    if (font) vita2d_free_pgf(font);
    vita2d_fini();
    tsfp_diagnostic(sceKernelGetProcessTimeWide(), "info", "bootstrap_exit", 0);
    sceKernelExitProcess(0);
    return 0;
}
