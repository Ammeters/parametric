#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_gamma_control_v1.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_presentation_time.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_server_decoration.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_viewporter.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/box.h>
#include <wlr/util/log.h>

#include "server.h"
#include "output.h"
#include "view.h"
#include "input.h"
#include "shell.h"
#include "config.h"

/* ================================================================== *
 *  Hit testing                                                       *
 * ================================================================== */
struct pm_view *pm_server_view_at(struct pm_server *s,
                                  double lx, double ly,
                                  struct wlr_surface **surf_out,
                                  double *sx, double *sy)
{
    struct wlr_scene_node *node =
        wlr_scene_node_at(&s->scene->tree.node, lx, ly, sx, sy);
    if (!node || node->type != WLR_SCENE_NODE_BUFFER)
        return NULL;

    struct wlr_scene_buffer *sbuf = wlr_scene_buffer_from_node(node);
    struct wlr_scene_surface *ssurf = wlr_scene_surface_try_from_buffer(sbuf);
    if (!ssurf) return NULL;

    if (surf_out) *surf_out = ssurf->surface;

    /* Walk up the scene tree until we hit a node tagged with a pm_view */
    struct wlr_scene_tree *tree = node->parent;
    while (tree && tree != &s->scene->tree) {
        struct pm_view *v = tree->node.data;
        if (v) return v;
        tree = tree->node.parent;
    }
    return NULL;
}

/* ================================================================== *
 *  Focus                                                             *
 * ================================================================== */
void pm_server_focus_view(struct pm_server *s, struct pm_view *v,
                           struct wlr_surface *surface)
{
    if (!v || !surface) return;

    struct wlr_seat *seat = s->seat;
    struct wlr_surface *prev =
        seat->keyboard_state.focused_surface;

    if (prev == surface) return;

    if (prev) {
        struct wlr_xdg_surface *prev_xdg =
            wlr_xdg_surface_try_from_wlr_surface(prev);
        if (prev_xdg &&
            prev_xdg->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL)
            wlr_xdg_toplevel_set_activated(prev_xdg->toplevel, false);
    }

    pm_view_raise(v);
    wlr_xdg_toplevel_set_activated(v->toplevel, true);

    struct wlr_keyboard *kb = wlr_seat_get_keyboard(seat);
    if (kb)
        wlr_seat_keyboard_notify_enter(seat, surface,
            kb->keycodes, kb->num_keycodes, &kb->modifiers);
}

/* ================================================================== *
 *  Output                                                            *
 * ================================================================== */
static void on_new_output(struct wl_listener *l, void *data) {
    struct pm_server *s = wl_container_of(l, s, l_new_output);
    struct wlr_output *wlr_output = data;

    wlr_output_init_render(wlr_output, s->allocator, s->renderer);

    /* Commit preferred mode */
    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, true);
    struct wlr_output_mode *mode = wlr_output_preferred_mode(wlr_output);
    if (mode) wlr_output_state_set_mode(&state, mode);
    wlr_output_commit_state(wlr_output, &state);
    wlr_output_state_finish(&state);

    pm_output_init(s, wlr_output);
}

/* ================================================================== *
 *  XDG shell                                                         *
 * ================================================================== */
static void on_new_toplevel(struct wl_listener *l, void *data) {
    struct pm_server *s = wl_container_of(l, s, l_new_toplevel);
    struct wlr_xdg_toplevel *toplevel = data;

    struct pm_view *v = calloc(1, sizeof(*v));
    if (!v) return;
    pm_view_init(v, s, toplevel);
}

static void on_new_popup(struct wl_listener *l, void *data) {
    struct pm_server *s = wl_container_of(l, s, l_new_popup);
    struct wlr_xdg_popup *popup = data;
    (void)s;

    /* Attach popup's scene tree under its parent's scene tree */
    struct wlr_xdg_surface *parent =
        wlr_xdg_surface_try_from_wlr_surface(popup->parent);
    if (!parent) return;

    struct wlr_scene_tree *parent_tree = parent->data;
    popup->base->data = wlr_scene_xdg_surface_create(parent_tree, popup->base);
}

/* ================================================================== *
 *  Layer-shell                                                       *
 * ================================================================== */
static void on_new_layer_surface(struct wl_listener *l, void *data) {
    struct pm_server *s = wl_container_of(l, s, l_new_layer_surface);
    struct wlr_layer_surface_v1 *ls = data;

    struct wlr_scene_tree *tree;
    switch (ls->current.layer) {
    case ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND:
    case ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM:
        tree = s->layer_bg;   break;
    case ZWLR_LAYER_SHELL_V1_LAYER_TOP:
        tree = s->layer_top;  break;
    case ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY:
    default:
        tree = s->layer_overlay; break;
    }

    wlr_scene_layer_surface_v1_create(tree, ls);
    wlr_log(WLR_DEBUG, "layer-shell: %s", ls->namespace);
}

/* ================================================================== *
 *  Cursor / pointer                                                  *
 * ================================================================== */

/* Route pointer position to the correct focused surface */
static void update_pointer_focus(struct pm_server *s, uint32_t time_msec) {
    double sx, sy;
    struct wlr_surface *surface = NULL;
    struct pm_view *v = pm_server_view_at(s,
        s->cursor->x, s->cursor->y, &surface, &sx, &sy);

    if (!v)
        wlr_cursor_set_xcursor(s->cursor, s->cursor_mgr, "default");

    if (surface) {
        wlr_seat_pointer_notify_enter(s->seat, surface, sx, sy);
        wlr_seat_pointer_notify_motion(s->seat, time_msec, sx, sy);
    } else {
        wlr_seat_pointer_clear_focus(s->seat);
    }
}

static void on_cursor_motion(struct wl_listener *l, void *data) {
    struct pm_server *s = wl_container_of(l, s, l_cursor_motion);
    struct wlr_pointer_motion_event *ev = data;

    wlr_cursor_move(s->cursor, &ev->pointer->base,
                    ev->delta_x, ev->delta_y);

    /* Homebar reveal: show when mouse within 8px of bottom edge */
    struct wlr_output *out = wlr_output_layout_output_at(
        s->output_layout, s->cursor->x, s->cursor->y);
    if (out) {
        int ow, oh;
        wlr_output_effective_resolution(out, &ow, &oh);
        struct wlr_box lb;
        wlr_output_layout_get_box(s->output_layout, out, &lb);
        int local_y = (int)s->cursor->y - lb.y;

        if (s->shell) {
            if (local_y >= oh - 8)
                pm_homebar_show(s->shell->homebar);
            else if (local_y < oh - 48)
                pm_homebar_hide(s->shell->homebar);
        }
    }

    update_pointer_focus(s, ev->time_msec);
}

static void on_cursor_motion_abs(struct wl_listener *l, void *data) {
    struct pm_server *s = wl_container_of(l, s, l_cursor_motion_abs);
    struct wlr_pointer_motion_absolute_event *ev = data;
    wlr_cursor_warp_absolute(s->cursor, &ev->pointer->base, ev->x, ev->y);
    update_pointer_focus(s, ev->time_msec);
}

static void on_cursor_button(struct wl_listener *l, void *data) {
    struct pm_server *s = wl_container_of(l, s, l_cursor_button);
    struct wlr_pointer_button_event *ev = data;

    wlr_seat_pointer_notify_button(s->seat, ev->time_msec,
                                   ev->button, ev->state);

    if (ev->state != WL_POINTER_BUTTON_STATE_PRESSED) return;

    double cx = s->cursor->x, cy = s->cursor->y;

    /* Overview click handling (takes full priority) */
    if (s->shell && s->shell->overview->active) {
        if (pm_overview_handle_click(s->shell->overview, cx, cy))
            return;
        /* Click outside cards hides overview */
        pm_overview_hide(s->shell->overview);
        return;
    }

    /* Notification panel dismiss on click */
    if (s->shell) {
        struct pm_notif_panel *np = s->shell->notif;
        if (np->anim.cur > 0.01f) {
            pm_notif_panel_hide(np);
            return;
        }
    }

    /* Taskbar icon click */
    if (s->shell && pm_taskbar_handle_click(s->shell->taskbar, cx, cy))
        return;

    /* Focus clicked application window */
    double sx, sy;
    struct wlr_surface *surface = NULL;
    struct pm_view *v = pm_server_view_at(s, cx, cy, &surface, &sx, &sy);
    if (v) pm_server_focus_view(s, v, surface);
}

static void on_cursor_axis(struct wl_listener *l, void *data) {
    struct pm_server *s = wl_container_of(l, s, l_cursor_axis);
    struct wlr_pointer_axis_event *ev = data;
    wlr_seat_pointer_notify_axis(s->seat, ev->time_msec,
        ev->orientation, ev->delta, ev->delta_discrete,
        ev->source, ev->relative_direction);
}

static void on_cursor_frame(struct wl_listener *l, void *data) {
    struct pm_server *s = wl_container_of(l, s, l_cursor_frame);
    (void)data;
    wlr_seat_pointer_notify_frame(s->seat);
}

/* ================================================================== *
 *  Input devices                                                     *
 * ================================================================== */
static void on_new_input(struct wl_listener *l, void *data) {
    struct pm_server *s = wl_container_of(l, s, l_new_input);
    struct wlr_input_device *dev = data;

    switch (dev->type) {
    case WLR_INPUT_DEVICE_KEYBOARD:
        pm_keyboard_init(s, wlr_keyboard_from_input_device(dev));
        break;
    case WLR_INPUT_DEVICE_POINTER:
        pm_pointer_attach(s, dev);
        break;
    default: break;
    }

    uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
    if (!wl_list_empty(&s->keyboards))
        caps |= WL_SEAT_CAPABILITY_KEYBOARD;
    wlr_seat_set_capabilities(s->seat, caps);
}

static void on_request_cursor(struct wl_listener *l, void *data) {
    struct pm_server *s = wl_container_of(l, s, l_request_cursor);
    struct wlr_seat_pointer_request_set_cursor_event *ev = data;
    if (s->seat->pointer_state.focused_client == ev->seat_client)
        wlr_cursor_set_surface(s->cursor, ev->surface,
                               ev->hotspot_x, ev->hotspot_y);
}

static void on_request_selection(struct wl_listener *l, void *data) {
    struct pm_server *s = wl_container_of(l, s, l_request_selection);
    struct wlr_seat_request_set_selection_event *ev = data;
    wlr_seat_set_selection(s->seat, ev->source, ev->serial);
}

/* ================================================================== *
 *  pm_server_init                                                    *
 * ================================================================== */
bool pm_server_init(struct pm_server *s) {
    /* ── Display + event loop ─────────────────────────────────── */
    s->wl_display = wl_display_create();
    if (!s->wl_display) return false;
    s->event_loop = wl_display_get_event_loop(s->wl_display);

    /* ── Backend / renderer / allocator ──────────────────────── */
    s->backend = wlr_backend_autocreate(s->event_loop, NULL);
    if (!s->backend) return false;

    s->renderer = wlr_renderer_autocreate(s->backend);
    if (!s->renderer) return false;
    wlr_renderer_init_wl_display(s->renderer, s->wl_display);

    s->allocator = wlr_allocator_autocreate(s->backend, s->renderer);
    if (!s->allocator) return false;

    /* ── Core globals ─────────────────────────────────────────── */
    s->compositor    = wlr_compositor_create(s->wl_display, 6, s->renderer);
    s->subcompositor = wlr_subcompositor_create(s->wl_display);
    s->data_device_mgr = wlr_data_device_manager_create(s->wl_display);

    /* ── Output layout ────────────────────────────────────────── */
    s->output_layout = wlr_output_layout_create(s->wl_display);

    /* ── Scene graph ──────────────────────────────────────────── */
    s->scene = wlr_scene_create();
    s->scene_layout = wlr_scene_attach_output_layout(s->scene,
                                                      s->output_layout);

    /* Layer trees — order = back to front */
    s->layer_bg      = wlr_scene_tree_create(&s->scene->tree);
    s->layer_windows = wlr_scene_tree_create(&s->scene->tree);
    s->layer_shell_bg= wlr_scene_tree_create(&s->scene->tree);
    s->layer_overview= wlr_scene_tree_create(&s->scene->tree);
    s->layer_top     = wlr_scene_tree_create(&s->scene->tree);
    s->layer_taskbar = wlr_scene_tree_create(&s->scene->tree);
    s->layer_overlay = wlr_scene_tree_create(&s->scene->tree);

    /* Hide dynamic layers until needed */
    wlr_scene_node_set_enabled(&s->layer_shell_bg->node, false);
    wlr_scene_node_set_enabled(&s->layer_overview->node,  false);

    /* ── Extended protocols ───────────────────────────────────── */
    s->xdg_shell    = wlr_xdg_shell_create(s->wl_display, 6);
    s->layer_shell  = wlr_layer_shell_v1_create(s->wl_display, 4);
    s->xdg_output_mgr =
        wlr_xdg_output_manager_v1_create(s->wl_display, s->output_layout);
    s->presentation = wlr_presentation_create(s->wl_display, s->backend);
    s->viewporter   = wlr_viewporter_create(s->wl_display);
    s->gamma_mgr    = wlr_gamma_control_manager_v1_create(s->wl_display);

    /* Server-side decorations by default */
    s->deco_mgr = wlr_server_decoration_manager_create(s->wl_display);
    wlr_server_decoration_manager_set_default_mode(s->deco_mgr,
        WLR_SERVER_DECORATION_MANAGER_MODE_SERVER);

    /* ── Cursor ───────────────────────────────────────────────── */
    s->cursor = wlr_cursor_create();
    wlr_cursor_attach_output_layout(s->cursor, s->output_layout);
    s->cursor_mgr = wlr_xcursor_manager_create(NULL, 24);
    wlr_xcursor_manager_load(s->cursor_mgr, 1);

    /* ── Seat ─────────────────────────────────────────────────── */
    s->seat = wlr_seat_create(s->wl_display, "seat0");

    wl_list_init(&s->outputs);
    wl_list_init(&s->views);
    wl_list_init(&s->keyboards);

    /* ── Wire listeners ───────────────────────────────────────── */
#define LISTEN(src, sig, member, fn) \
    s->member.notify = fn; \
    wl_signal_add(&(src), &s->member)

    LISTEN(s->backend->events.new_output,        l_new_output,        on_new_output);
    LISTEN(s->xdg_shell->events.new_toplevel,    l_new_toplevel,      on_new_toplevel);
    LISTEN(s->xdg_shell->events.new_popup,       l_new_popup,         on_new_popup);
    LISTEN(s->layer_shell->events.new_surface,   l_new_layer_surface, on_new_layer_surface);
    LISTEN(s->cursor->events.motion,             l_cursor_motion,     on_cursor_motion);
    LISTEN(s->cursor->events.motion_absolute,    l_cursor_motion_abs, on_cursor_motion_abs);
    LISTEN(s->cursor->events.button,             l_cursor_button,     on_cursor_button);
    LISTEN(s->cursor->events.axis,               l_cursor_axis,       on_cursor_axis);
    LISTEN(s->cursor->events.frame,              l_cursor_frame,      on_cursor_frame);
    LISTEN(s->backend->events.new_input,         l_new_input,         on_new_input);
    LISTEN(s->seat->events.request_set_cursor,   l_request_cursor,    on_request_cursor);
    LISTEN(s->seat->events.request_set_selection,l_request_selection, on_request_selection);
#undef LISTEN

    /* ── Shell UI ─────────────────────────────────────────────── */
    s->shell = pm_shell_create(s);
    if (!s->shell) return false;

    return true;
}

/* ================================================================== *
 *  pm_server_run                                                     *
 * ================================================================== */
void pm_server_run(struct pm_server *s, const char *startup_cmd) {
    const char *sock = wl_display_add_socket_auto(s->wl_display);
    if (!sock) {
        wlr_log(WLR_ERROR, "Failed to open Wayland socket");
        return;
    }

    if (!wlr_backend_start(s->backend)) {
        wlr_log(WLR_ERROR, "Failed to start backend");
        return;
    }

    setenv("WAYLAND_DISPLAY", sock, true);
    setenv("XDG_SESSION_TYPE", "wayland", true);
    setenv("XDG_CURRENT_DESKTOP", "Parametric", true);

    wlr_log(WLR_INFO, "Parametric running on WAYLAND_DISPLAY=%s", sock);

    if (startup_cmd) {
        if (fork() == 0) {
            execl("/bin/sh", "/bin/sh", "-c", startup_cmd, NULL);
            _exit(1);
        }
    }

    wl_display_run(s->wl_display);
}

/* ================================================================== *
 *  pm_server_destroy                                                 *
 * ================================================================== */
void pm_server_destroy(struct pm_server *s) {
    pm_shell_destroy(s->shell);
    wl_display_destroy_clients(s->wl_display);
    wlr_scene_node_destroy(&s->scene->tree.node);
    wlr_xcursor_manager_destroy(s->cursor_mgr);
    wlr_cursor_destroy(s->cursor);
    wlr_allocator_destroy(s->allocator);
    wlr_renderer_destroy(s->renderer);
    wlr_backend_destroy(s->backend);
    wl_display_destroy(s->wl_display);
    parametric_config_destroy(s->config);
}
