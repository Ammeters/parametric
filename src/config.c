#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "config.h"

/* Parse "#RRGGBB" → float[4] RGBA with given alpha */
static void hex_rgba(const char *hex, float a, float out[4]) {
    if (*hex == '#') hex++;
    unsigned r, g, b;
    sscanf(hex, "%02x%02x%02x", &r, &g, &b);
    out[0] = (float)r / 255.0f;
    out[1] = (float)g / 255.0f;
    out[2] = (float)b / 255.0f;
    out[3] = a;
}

struct parametric_config *parametric_config_create(void) {
    struct parametric_config *c = calloc(1, sizeof(*c));
    if (!c) return NULL;

    /* ── Mode ───────────────────────────────────────────────── */
    c->desktop_mode          = true;
    c->taskbar_always_visible = false;
    c->show_decorations      = true;
    c->decorations_on_mobile = false;

    /* ── Background gradient ────────────────────────────────── */
    hex_rgba("07527F", 1.0f, c->bg_tl);   /* top-left  */
    hex_rgba("00265C", 1.0f, c->bg_br);   /* bot-right */

    /* ── Taskbar: deep navy, 82 % opaque ────────────────────── */
    c->taskbar_fill[0] = 0.02f;
    c->taskbar_fill[1] = 0.11f;
    c->taskbar_fill[2] = 0.26f;
    c->taskbar_fill[3] = 0.84f;

    c->taskbar_border[0] = 1.0f;
    c->taskbar_border[1] = 1.0f;
    c->taskbar_border[2] = 1.0f;
    c->taskbar_border[3] = 0.16f;

    /* ── Buttons ────────────────────────────────────────────── */
    hex_rgba("FF4F4F", 0.92f, c->btn_close);
    hex_rgba("FFC83A", 0.92f, c->btn_min);

    /* ── Overview cards ─────────────────────────────────────── */
    c->card_fill[0] = 0.04f;
    c->card_fill[1] = 0.20f;
    c->card_fill[2] = 0.44f;
    c->card_fill[3] = 0.92f;

    c->card_hover[0] = 0.08f;
    c->card_hover[1] = 0.34f;
    c->card_hover[2] = 0.60f;
    c->card_hover[3] = 1.0f;

    /* ── Homebar: white pill ────────────────────────────────── */
    c->homebar[0] = 1.0f;
    c->homebar[1] = 1.0f;
    c->homebar[2] = 1.0f;
    c->homebar[3] = 0.90f;

    /* ── Notification panel ─────────────────────────────────── */
    c->notif_fill[0] = 0.03f;
    c->notif_fill[1] = 0.16f;
    c->notif_fill[2] = 0.34f;
    c->notif_fill[3] = 0.94f;

    /* ── Typography ─────────────────────────────────────────── */
    snprintf(c->font_ui,   sizeof(c->font_ui),   "Sans");
    snprintf(c->font_mono, sizeof(c->font_mono), "Monospace");

    return c;
}

void parametric_config_destroy(struct parametric_config *c) {
    free(c);
}
