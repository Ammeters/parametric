#define _POSIX_C_SOURCE 200809L
#include <time.h>
#include <math.h>
#include "animate.h"

float pm_clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

float pm_lerpf(float a, float b, float t) {
    return a + (b - a) * t;
}

/* Ease-out cubic: fast start, decelerates to stop */
float pm_ease_out_cubic(float t) {
    float u = 1.0f - t;
    return 1.0f - (u * u * u);
}

/* Ease-in-out cubic: smooth both ends */
float pm_ease_inout_cubic(float t) {
    return t < 0.5f
        ? 4.0f * t * t * t
        : 1.0f - powf(-2.0f * t + 2.0f, 3.0f) / 2.0f;
}

int64_t pm_now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000LL + (int64_t)(ts.tv_nsec / 1000);
}

/* ── pm_anim ─────────────────────────────────────────────────────── */
void pm_anim_start(struct pm_anim *a, float to, int dur_ms) {
    a->from     = a->cur;
    a->to       = to;
    a->start_us = pm_now_us();
    a->dur_us   = (int64_t)dur_ms * 1000LL;
    a->running  = true;
}

float pm_anim_tick(struct pm_anim *a) {
    if (!a->running) return a->cur;

    int64_t elapsed = pm_now_us() - a->start_us;
    float t = (float)elapsed / (float)a->dur_us;
    t = pm_clampf(t, 0.0f, 1.0f);

    a->cur = pm_lerpf(a->from, a->to, pm_ease_out_cubic(t));

    if (t >= 1.0f) {
        a->cur     = a->to;
        a->running = false;
    }
    return a->cur;
}
