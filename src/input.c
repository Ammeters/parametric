#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <string.h>
#include <linux/input-event-codes.h>

#include <wayland-server-core.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon.h>

#include "server.h"
#include "input.h"
#include "view.h"
#include "shell.h"

/* ================================================================== *
 *  Compositor keybindings                                           *
 *  Returns true if the key was handled by the compositor.           *
 * ================================================================== */
static bool handle_keybind(struct pm_server *s,
                             xkb_keysym_t sym, uint32_t mods)
{
    bool super = (mods & WLR_MODIFIER_LOGO)  != 0;
    bool ctrl  = (mods & WLR_MODIFIER_CTRL)  != 0;
    bool alt   = (mods & WLR_MODIFIER_ALT)   != 0;
    bool shift = (mods & WLR_MODIFIER_SHIFT) != 0;
    (void)shift;

    struct pm_shell *sh = s->shell;

    /* ── Super key: toggle overview ──────────────────────────── */
    if (super && (sym == XKB_KEY_Super_L || sym == XKB_KEY_Super_R)) {
        pm_overview_toggle(sh->overview);
        return true;
    }

    /* ── Super+W: overview ───────────────────────────────────── */
    if (super && sym == XKB_KEY_w) {
        pm_overview_toggle(sh->overview);
        return true;
    }

    /* ── Super+D: dismiss overview → desktop ─────────────────── */
    if (super && sym == XKB_KEY_d) {
        pm_overview_hide(sh->overview);
        return true;
    }

    /* ── Super+Q: close focused window ───────────────────────── */
    if (super && sym == XKB_KEY_q) {
        struct pm_view *v;
        wl_list_for_each(v, &s->views, link) {
            if (v->mapped && !v->minimised) {
                pm_view_close(v);
                return true;
            }
        }
        return true;
    }

    /* ── Super+M: minimise focused window ────────────────────── */
    if (super && sym == XKB_KEY_m) {
        struct pm_view *v;
        wl_list_for_each(v, &s->views, link) {
            if (v->mapped && !v->minimised) {
                pm_view_minimise(v);
                return true;
            }
        }
        return true;
    }

    /* ── Super+Tab: cycle to next window ─────────────────────── */
    if (super && sym == XKB_KEY_Tab) {
        struct pm_view *focused = NULL, *candidate = NULL;
        struct pm_view *v;
        wl_list_for_each(v, &s->views, link) {
            if (!v->mapped || v->minimised) continue;
            if (!focused)       { focused   = v; continue; }
            if (!candidate)     { candidate = v; break; }
        }
        /* wrap: candidate = last eligible view */
        if (!candidate && focused) {
            wl_list_for_each_reverse(v, &s->views, link) {
                if (v != focused && v->mapped && !v->minimised) {
                    candidate = v; break;
                }
            }
        }
        if (candidate)
            pm_server_focus_view(s, candidate,
                                  candidate->toplevel->base->surface);
        return true;
    }

    /* ── Super+H: show / hide notification panel ─────────────── */
    if (super && sym == XKB_KEY_h) {
        if (sh->notif->anim.cur > 0.5f)
            pm_notif_panel_hide(sh->notif);
        else
            pm_notif_panel_show(sh->notif);
        return true;
    }

    /* ── Alt+F4: close focused window ────────────────────────── */
    if (alt && sym == XKB_KEY_F4) {
        struct pm_view *v;
        wl_list_for_each(v, &s->views, link) {
            if (v->mapped && !v->minimised) {
                pm_view_close(v);
                return true;
            }
        }
        return true;
    }

    /* ── Ctrl+Alt+T: launch terminal (foot) ──────────────────── */
    if (ctrl && alt && sym == XKB_KEY_t) {
        if (fork() == 0) {
            execl("/bin/sh", "/bin/sh", "-c",
                  "foot || alacritty || kitty || xterm", NULL);
            _exit(1);
        }
        return true;
    }

    /* ── Ctrl+Alt+BackSpace: quit compositor ─────────────────── */
    if (ctrl && alt && sym == XKB_KEY_BackSpace) {
        wl_display_terminate(s->wl_display);
        return true;
    }

    return false;
}

/* ================================================================== *
 *  Keyboard listeners                                                *
 * ================================================================== */
static void on_kb_modifiers(struct wl_listener *l, void *data) {
    struct pm_keyboard *kb = wl_container_of(l, kb, l_modifiers);
    (void)data;
    wlr_seat_set_keyboard(kb->server->seat, kb->wlr_keyboard);
    wlr_seat_keyboard_notify_modifiers(kb->server->seat,
                                       &kb->wlr_keyboard->modifiers);
}

static void on_kb_key(struct wl_listener *l, void *data) {
    struct pm_keyboard *kb = wl_container_of(l, kb, l_key);
    struct pm_server *s = kb->server;
    struct wlr_keyboard_key_event *ev = data;

    /* Translate kernel keycode → XKB keysym */
    uint32_t keycode = ev->keycode + 8;
    const xkb_keysym_t *syms;
    int nsyms = xkb_state_key_get_syms(
        kb->wlr_keyboard->xkb_state, keycode, &syms);

    uint32_t mods = wlr_keyboard_get_modifiers(kb->wlr_keyboard);

    bool handled = false;
    if (ev->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        for (int i = 0; i < nsyms && !handled; i++)
            handled = handle_keybind(s, syms[i], mods);
    }

    if (!handled) {
        wlr_seat_set_keyboard(s->seat, kb->wlr_keyboard);
        wlr_seat_keyboard_notify_key(s->seat, ev->time_msec,
                                     ev->keycode, ev->state);
    }
}

static void on_kb_destroy(struct wl_listener *l, void *data) {
    struct pm_keyboard *kb = wl_container_of(l, kb, l_destroy);
    (void)data;
    wl_list_remove(&kb->l_modifiers.link);
    wl_list_remove(&kb->l_key.link);
    wl_list_remove(&kb->l_destroy.link);
    wl_list_remove(&kb->link);
    free(kb);
}

/* ================================================================== *
 *  pm_keyboard_init                                                  *
 * ================================================================== */
void pm_keyboard_init(struct pm_server *s, struct wlr_keyboard *wlr_kb) {
    struct pm_keyboard *kb = calloc(1, sizeof(*kb));
    if (!kb) return;

    kb->server       = s;
    kb->wlr_keyboard = wlr_kb;

    /* XKB keymap with system locale */
    struct xkb_context *ctx =
        xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    struct xkb_keymap *km =
        xkb_keymap_new_from_names(ctx, NULL,
                                   XKB_KEYMAP_COMPILE_NO_FLAGS);
    wlr_keyboard_set_keymap(wlr_kb, km);
    xkb_keymap_unref(km);
    xkb_context_unref(ctx);

    wlr_keyboard_set_repeat_info(wlr_kb, 25, 500);

    kb->l_modifiers.notify = on_kb_modifiers;
    wl_signal_add(&wlr_kb->events.modifiers, &kb->l_modifiers);

    kb->l_key.notify = on_kb_key;
    wl_signal_add(&wlr_kb->events.key, &kb->l_key);

    kb->l_destroy.notify = on_kb_destroy;
    wl_signal_add(&wlr_kb->base.events.destroy, &kb->l_destroy);

    wlr_seat_set_keyboard(s->seat, wlr_kb);
    wl_list_insert(&s->keyboards, &kb->link);
}

/* ================================================================== *
 *  pm_pointer_attach                                                 *
 * ================================================================== */
void pm_pointer_attach(struct pm_server *s, struct wlr_input_device *dev) {
    wlr_cursor_attach_input_device(s->cursor, dev);
    wlr_log(WLR_DEBUG, "Pointer attached: %s", dev->name);
}

/* ================================================================== *
 *  pm_gesture_complete                                               *
 * ================================================================== */
void pm_gesture_complete(struct pm_server *s, struct pm_gesture *g) {
    if (!g->active) return;

    double dy = g->start_y - g->cur_y;  /* positive = swipe up */

    if (g->edge == PM_EDGE_BOTTOM) {
        /* Swipe up from bottom edge */
        struct pm_overview *ov = s->shell->overview;
        if (dy > 120) {
            /* Large upswipe → go to desktop (hide overview) */
            pm_overview_hide(ov);
        } else if (dy > 40) {
            /* Medium upswipe → toggle overview */
            pm_overview_toggle(ov);
        }
        /* Small swipe → just homebar peek, already handled on motion */

    } else if (g->edge == PM_EDGE_TOP) {
        /* Swipe down from top → notification panel */
        double dy2 = g->cur_y - g->start_y;  /* positive = swipe down */
        if (dy2 > 40)
            pm_notif_panel_show(s->shell->notif);
    }

    g->active = false;
}
