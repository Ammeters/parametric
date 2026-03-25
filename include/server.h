#pragma once
#include <stdbool.h>
#include <wayland-server-core.h>

#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_gamma_control_v1.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_presentation_time.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_server_decoration.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_viewporter.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>

struct pm_view;
struct pm_output;
struct pm_keyboard;
struct pm_shell;
struct parametric_config;

/* ─── cursor interaction modes ───────────────────────────────────── */
enum pm_cursor_mode {
    PM_CURSOR_PASSTHROUGH = 0,
    PM_CURSOR_MOVE,
    PM_CURSOR_RESIZE,
};

/* ================================================================== *
 *  Central compositor state                                          *
 * ================================================================== */
struct pm_server {
    /* ── Wayland core ─────────────────────────────────────────── */
    struct wl_display        *wl_display;
    struct wl_event_loop     *event_loop;

    /* ── wlroots backend / renderer / allocator ───────────────── */
    struct wlr_backend       *backend;
    struct wlr_renderer      *renderer;
    struct wlr_allocator     *allocator;

    /* ── Wayland protocol objects ─────────────────────────────── */
    struct wlr_compositor    *compositor;
    struct wlr_subcompositor *subcompositor;
    struct wlr_data_device_manager *data_device_mgr;
    struct wlr_xdg_shell     *xdg_shell;
    struct wlr_layer_shell_v1 *layer_shell;
    struct wlr_xdg_output_manager_v1 *xdg_output_mgr;
    struct wlr_presentation  *presentation;
    struct wlr_viewporter    *viewporter;
    struct wlr_gamma_control_manager_v1 *gamma_mgr;
    struct wlr_server_decoration_manager *deco_mgr;

    /* ── Output layout + scene graph ─────────────────────────── */
    struct wlr_output_layout       *output_layout;
    struct wlr_scene               *scene;
    struct wlr_scene_output_layout *scene_layout;

    /* ── Scene layer tree ordering (back → front) ────────────── */
    struct wlr_scene_tree *layer_bg;        /* gradient background         */
    struct wlr_scene_tree *layer_windows;   /* application windows         */
    struct wlr_scene_tree *layer_shell_bg;  /* overview dim overlay         */
    struct wlr_scene_tree *layer_overview;  /* overview cards               */
    struct wlr_scene_tree *layer_top;       /* layer-shell TOP surfaces     */
    struct wlr_scene_tree *layer_taskbar;   /* taskbar + homebar            */
    struct wlr_scene_tree *layer_overlay;   /* notifications / lock screen  */

    /* ── Lists ────────────────────────────────────────────────── */
    struct wl_list outputs;    /* pm_output  */
    struct wl_list views;      /* pm_view, front = topmost */
    struct wl_list keyboards;  /* pm_keyboard */

    /* ── Seat + cursor ────────────────────────────────────────── */
    struct wlr_seat          *seat;
    struct wlr_cursor        *cursor;
    struct wlr_xcursor_manager *cursor_mgr;

    /* ── Cursor interaction state ─────────────────────────────── */
    enum pm_cursor_mode       cursor_mode;
    struct pm_view           *grabbed_view;
    double                    grab_x, grab_y;
    struct wlr_box            grab_geo;
    uint32_t                  resize_edges;

    /* ── Shell UI ─────────────────────────────────────────────── */
    struct pm_shell          *shell;

    /* ── Configuration ────────────────────────────────────────── */
    struct parametric_config *config;

    /* ── Listeners ────────────────────────────────────────────── */
    struct wl_listener l_new_output;
    struct wl_listener l_new_toplevel;
    struct wl_listener l_new_popup;
    struct wl_listener l_new_layer_surface;
    struct wl_listener l_cursor_motion;
    struct wl_listener l_cursor_motion_abs;
    struct wl_listener l_cursor_button;
    struct wl_listener l_cursor_axis;
    struct wl_listener l_cursor_frame;
    struct wl_listener l_new_input;
    struct wl_listener l_request_cursor;
    struct wl_listener l_request_selection;
};

/* ── Public API ──────────────────────────────────────────────────── */
bool pm_server_init(struct pm_server *s);
void pm_server_run(struct pm_server *s, const char *startup_cmd);
void pm_server_destroy(struct pm_server *s);

void       pm_server_focus_view(struct pm_server *s, struct pm_view *v,
                                struct wlr_surface *surface);
struct pm_view *pm_server_view_at(struct pm_server *s, double lx, double ly,
                                  struct wlr_surface **surf_out,
                                  double *sx, double *sy);
