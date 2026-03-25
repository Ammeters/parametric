#pragma once
#include <stdbool.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_input_device.h>

struct pm_server;

/* ── Keyboard ────────────────────────────────────────────────────── */
struct pm_keyboard {
    struct wl_list        link;
    struct pm_server     *server;
    struct wlr_keyboard  *wlr_keyboard;

    struct wl_listener l_modifiers;
    struct wl_listener l_key;
    struct wl_listener l_destroy;
};

/* ── Edge-swipe gesture tracker ──────────────────────────────────── *
 *  Used for homebar swipe-up and notification swipe-down.           *
 * ------------------------------------------------------------------ */
enum pm_edge { PM_EDGE_NONE, PM_EDGE_BOTTOM, PM_EDGE_TOP };

struct pm_gesture {
    bool         active;
    enum pm_edge edge;
    double       start_x, start_y;
    double       cur_x,   cur_y;
};

void pm_keyboard_init(struct pm_server *s, struct wlr_keyboard *kb);
void pm_pointer_attach(struct pm_server *s, struct wlr_input_device *dev);

/* Evaluate a completed gesture and fire shell actions */
void pm_gesture_complete(struct pm_server *s, struct pm_gesture *g);
