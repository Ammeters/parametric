#pragma once
#include <stdbool.h>

/* ------------------------------------------------------------------ *
 *  Parametric – runtime configuration                                 *
 * ------------------------------------------------------------------ */

struct parametric_config {
    /* ── Mode ───────────────────────────────────────────────── */
    bool desktop_mode;           /* true=desktop, false=mobile        */

    /* ── Taskbar ─────────────────────────────────────────────── */
    bool taskbar_always_visible; /* dock-style always-on              */

    /* ── Window decorations ──────────────────────────────────── */
    bool show_decorations;       /* compositor-drawn CSD              */
    bool decorations_on_mobile;  /* show CSD in mobile mode too       */

    /* ── Colours (RGBA 0-1) ──────────────────────────────────── */
    float bg_tl[4];              /* gradient top-left   #07527F       */
    float bg_br[4];              /* gradient bot-right  #00265C       */

    float taskbar_fill[4];       /* frosted navy                      */
    float taskbar_border[4];     /* thin white rim                    */

    float btn_close[4];          /* #FF4F4F                           */
    float btn_min[4];            /* #FFC83A                           */

    float card_fill[4];          /* overview card body                */
    float card_hover[4];         /* hovered card body                 */

    float homebar[4];            /* white pill                        */
    float notif_fill[4];         /* notification panel body           */

    /* ── Typography ─────────────────────────────────────────── */
    char font_ui[64];
    char font_mono[64];

    /* ── Startup ─────────────────────────────────────────────── */
    char startup_cmd[256];
};

struct parametric_config *parametric_config_create(void);
void                      parametric_config_destroy(struct parametric_config *cfg);
