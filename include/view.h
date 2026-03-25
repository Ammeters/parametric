#pragma once
#include <stdbool.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_scene.h>

struct pm_server;

/* ================================================================== *
 *  pm_view — wraps a single xdg_toplevel                            *
 * ================================================================== */
struct pm_view {
    struct wl_list           link;        /* pm_server.views            */
    struct pm_server        *server;
    struct wlr_xdg_toplevel *toplevel;

    /* Root scene tree for this view (parent = layer_windows) */
    struct wlr_scene_tree   *scene_tree;

    /* Compositor-drawn decorations (title-bar, buttons) */
    struct wlr_scene_tree   *deco_tree;
    struct wlr_scene_rect   *deco_bar;    /* translucent title bar      */
    struct wlr_scene_rect   *btn_close;   /* red  close                 */
    struct wlr_scene_rect   *btn_min;     /* amber minimise             */

    /* Overview card references (rebuilt on each overview open) */
    struct wlr_scene_tree   *ov_card;
    struct wlr_scene_rect   *ov_bg;

    /* Current geometry (layout coordinates) */
    int x, y, width, height;

    bool mapped;
    bool minimised;

    /* ── Listeners ────────────────────────────────────────────── */
    struct wl_listener l_map;
    struct wl_listener l_unmap;
    struct wl_listener l_destroy;
    struct wl_listener l_commit;
    struct wl_listener l_req_move;
    struct wl_listener l_req_resize;
    struct wl_listener l_req_maximize;
    struct wl_listener l_req_fullscreen;
    struct wl_listener l_req_minimize;
    struct wl_listener l_set_title;
};

void pm_view_init(struct pm_view *v, struct pm_server *s,
                  struct wlr_xdg_toplevel *toplevel);

/* Fill the usable output area */
void pm_view_maximize(struct pm_view *v);

/* Bring to top of render + focus stack */
void pm_view_raise(struct pm_view *v);

/* Minimise / un-minimise */
void pm_view_minimise(struct pm_view *v);
void pm_view_restore(struct pm_view *v);

/* Send close request */
void pm_view_close(struct pm_view *v);

/* Sync decoration geometry to current view size */
void pm_view_update_decorations(struct pm_view *v);

/* Height added by decorations (0 when hidden) */
int  pm_view_deco_height(struct pm_view *v);
