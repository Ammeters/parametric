#pragma once
#include <stdint.h>
#include <stdbool.h>

/* ------------------------------------------------------------------ *
 *  Animation helpers                                                  *
 * ------------------------------------------------------------------ */

float   pm_ease_out_cubic(float t);   /* decelerate to stop        */
float   pm_ease_inout_cubic(float t); /* smooth start + stop       */
float   pm_clampf(float v, float lo, float hi);
float   pm_lerpf(float a, float b, float t);
int64_t pm_now_us(void);              /* CLOCK_MONOTONIC µs        */

/* ── Durations (ms) ─────────────────────────────────────────────── */
#define PM_DUR_OVERVIEW_MS   300
#define PM_DUR_HOMEBAR_MS    180
#define PM_DUR_NOTIF_MS      260
#define PM_FPS                60
#define PM_FRAME_MS   (1000 / PM_FPS)

/* ── Generic animated scalar ─────────────────────────────────────── *
 *  Embed this in any struct that needs a single animated float.      *
 * ------------------------------------------------------------------ */
struct pm_anim {
    float   from, to, cur;
    int64_t start_us;
    int64_t dur_us;
    bool    running;
};

/* Start animating toward `to` over `dur_ms` milliseconds */
void pm_anim_start(struct pm_anim *a, float to, int dur_ms);

/* Advance the animation; returns the current value.
 * Sets a->running = false when complete.                            */
float pm_anim_tick(struct pm_anim *a);
