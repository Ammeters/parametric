#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/util/box.h>
#include <wlr/util/log.h>

#include "server.h"
#include "view.h"
#include "output.h"
#include "shell.h"
#include "config.h"

/* ================================================================== *
 *  Decoration constants                                              *
 * ================================================================== */
#define DECO_H         30    /* title-bar height px                   */
#define BTN_SZ         13    /* button side length px                 */
#define BTN_MARGIN_L   10    /* left margin from window edge          */
#define BTN_GAP         9    /* gap between close and min buttons     */
#define BTN_VTOP        9    /* offset from top of title bar          */

/* ── Utility: get first output ───────────────────────────────────── */
static struct pm_output *primary_output(struct pm_server *s) {
    struct pm_output *o;
    wl_list_for_each(o, &s->outputs, link) return o;
    return NULL;
}

/* ================================================================== *
 *  Decoration management                                             *
 * ================================================================== */
int pm_view_deco_height(struct pm_view *v) {
    struct parametric_config *c = v->server->config;
    bool show = c->show_decorations &&
                (c->desktop_mode || c->decorations_on_mobile);
    return show ? DECO_H : 0;
}

void pm_view_update_decorations(struct pm_view *v) {
    if (!v->deco_tree) return;

    int dh = pm_view_deco_height(v);
    wlr_scene_node_set_enabled(&v->deco_tree->node, dh > 0);

    if (dh > 0 && v->deco_bar && v->width > 0) {
        wlr_scene_rect_set_size(v->deco_bar, v->width, dh);
    }
}

static void create_decorations(struct pm_view *v) {
    struct pm_server *s = v->server;

    v->deco_tree = wlr_scene_tree_create(v->scene_tree);

    /* Title bar */
    float bar_col[] = {0.03f, 0.13f, 0.30f, 0.80f};
    v->deco_bar = wlr_scene_rect_create(v->deco_tree,
                                         1, DECO_H, bar_col);
    wlr_scene_node_set_position(&v->deco_bar->node, 0, -DECO_H);

    /* Close button — red */
    float *cl = s->config->btn_close;
    v->btn_close = wlr_scene_rect_create(v->deco_tree,
                                          BTN_SZ, BTN_SZ, cl);
    wlr_scene_node_set_position(&v->btn_close->node,
                                BTN_MARGIN_L, -DECO_H + BTN_VTOP);

    /* Minimise button — amber */
    float *mn = s->config->btn_min;
    v->btn_min = wlr_scene_rect_create(v->deco_tree,
                                        BTN_SZ, BTN_SZ, mn);
    wlr_scene_node_set_position(&v->btn_min->node,
                                BTN_MARGIN_L + BTN_SZ + BTN_GAP,
                                -DECO_H + BTN_VTOP);

    int dh = pm_view_deco_height(v);
    wlr_scene_node_set_enabled(&v->deco_tree->node, dh > 0);
}

/* ── Check if (lx,ly) hit the close or min button ───────────────── */
static bool check_deco_buttons(struct pm_view *v, double lx, double ly) {
    if (pm_view_deco_height(v) <= 0) return false;

    /* Convert layout → window-local coords */
    double wx = lx - v->x;
    double wy = ly - v->y;

    /* Title bar occupies y in [-DECO_H, 0) relative to window origin */
    if (wy < -DECO_H || wy >= 0) return false;

    /* Close */
    if (wx >= BTN_MARGIN_L && wx < BTN_MARGIN_L + BTN_SZ) {
        pm_view_close(v);
        return true;
    }
    /* Min */
    double min_x = BTN_MARGIN_L + BTN_SZ + BTN_GAP;
    if (wx >= min_x && wx < min_x + BTN_SZ) {
        pm_view_minimise(v);
        return true;
    }
    return false;
}

/* ================================================================== *
 *  XDG surface callbacks                                            *
 * ================================================================== */
static void on_map(struct wl_listener *l, void *data) {
    struct pm_view *v = wl_container_of(l, v, l_map);
    (void)data;
    v->mapped = true;
    wlr_scene_node_set_enabled(&v->scene_tree->node, true);
    pm_view_maximize(v);
    pm_server_focus_view(v->server, v, v->toplevel->base->surface);
    pm_shell_view_mapped(v->server->shell, v);
    wlr_log(WLR_DEBUG, "View mapped: '%s'",
            v->toplevel->title ? v->toplevel->title : "(untitled)");
}

static void on_unmap(struct wl_listener *l, void *data) {
    struct pm_view *v = wl_container_of(l, v, l_unmap);
    (void)data;
    v->mapped = false;
    wlr_scene_node_set_enabled(&v->scene_tree->node, false);
    pm_shell_view_unmapped(v->server->shell, v);

    /* Transfer focus to the next visible window */
    struct pm_view *nxt;
    wl_list_for_each(nxt, &v->server->views, link) {
        if (nxt != v && nxt->mapped && !nxt->minimised) {
            pm_server_focus_view(v->server, nxt,
                                  nxt->toplevel->base->surface);
            return;
        }
    }
}

static void on_destroy(struct wl_listener *l, void *data) {
    struct pm_view *v = wl_container_of(l, v, l_destroy);
    (void)data;

    wl_list_remove(&v->l_map.link);
    wl_list_remove(&v->l_unmap.link);
    wl_list_remove(&v->l_destroy.link);
    wl_list_remove(&v->l_commit.link);
    wl_list_remove(&v->l_req_move.link);
    wl_list_remove(&v->l_req_resize.link);
    wl_list_remove(&v->l_req_maximize.link);
    wl_list_remove(&v->l_req_fullscreen.link);
    wl_list_remove(&v->l_req_minimize.link);
    wl_list_remove(&v->l_set_title.link);
    wl_list_remove(&v->link);
    free(v);
}

/* ── wlroots 0.18: must send configure on initial_commit ─────────── */
static void on_commit(struct wl_listener *l, void *data) {
    struct pm_view *v = wl_container_of(l, v, l_commit);
    (void)data;

    if (v->toplevel->base->initial_commit) {
        /* First commit: tell the client its intended size now */
        struct pm_output *out = primary_output(v->server);
        if (out) {
            struct wlr_box usable;
            pm_output_usable_area(out, &usable);
            int dh = pm_view_deco_height(v);
            wlr_xdg_toplevel_set_size(v->toplevel,
                                      usable.width,
                                      usable.height - dh);
            wlr_xdg_toplevel_set_maximized(v->toplevel, true);
        }
        wlr_xdg_surface_schedule_configure(v->toplevel->base);
        return;
    }

    if (v->mapped) pm_view_update_decorations(v);
}

static void on_req_move(struct wl_listener *l, void *data) {
    /* Windows stay maximised — ignore interactive move requests */
    (void)l; (void)data;
}

static void on_req_resize(struct wl_listener *l, void *data) {
    /* Windows stay maximised — ignore interactive resize requests */
    (void)l; (void)data;
}

static void on_req_maximize(struct wl_listener *l, void *data) {
    struct pm_view *v = wl_container_of(l, v, l_req_maximize);
    (void)data;
    pm_view_maximize(v);
}

static void on_req_fullscreen(struct wl_listener *l, void *data) {
    struct pm_view *v = wl_container_of(l, v, l_req_fullscreen);
    (void)data;
    wlr_xdg_toplevel_set_fullscreen(v->toplevel, true);
    pm_view_maximize(v);
}

static void on_req_minimize(struct wl_listener *l, void *data) {
    struct pm_view *v = wl_container_of(l, v, l_req_minimize);
    (void)data;
    pm_view_minimise(v);
}

static void on_set_title(struct wl_listener *l, void *data) {
    struct pm_view *v = wl_container_of(l, v, l_set_title);
    (void)data;
    /* Refresh overview cards if open */
    if (v->server->shell && v->server->shell->overview->active)
        pm_overview_rebuild(v->server->shell->overview);
}

/* ================================================================== *
 *  pm_view_init                                                      *
 * ================================================================== */
void pm_view_init(struct pm_view *v, struct pm_server *s,
                  struct wlr_xdg_toplevel *toplevel)
{
    v->server   = s;
    v->toplevel = toplevel;

    /* Create the scene tree for this window inside the windows layer */
    v->scene_tree = wlr_scene_xdg_surface_create(
        s->layer_windows, toplevel->base);
    if (!v->scene_tree) { free(v); return; }

    /* Tag the tree node so hit-testing can walk back to the view */
    v->scene_tree->node.data = v;
    toplevel->base->data = v->scene_tree;

    /* Build compositor decorations */
    create_decorations(v);

    /* Hidden until the surface is mapped */
    wlr_scene_node_set_enabled(&v->scene_tree->node, false);

    wl_list_insert(&s->views, &v->link);

    /* ── Listeners ────────────────────────────────────────────── */
#define LISTEN(src, sig, member, fn) \
    v->member.notify = fn; \
    wl_signal_add(&(src)->events.sig, &v->member)

    LISTEN(toplevel->base, map,     l_map,     on_map);
    LISTEN(toplevel->base, unmap,   l_unmap,   on_unmap);
    LISTEN(toplevel,       destroy, l_destroy, on_destroy);

    v->l_commit.notify = on_commit;
    wl_signal_add(&toplevel->base->surface->events.commit, &v->l_commit);

    LISTEN(toplevel, request_move,       l_req_move,       on_req_move);
    LISTEN(toplevel, request_resize,     l_req_resize,     on_req_resize);
    LISTEN(toplevel, request_maximize,   l_req_maximize,   on_req_maximize);
    LISTEN(toplevel, request_fullscreen, l_req_fullscreen, on_req_fullscreen);
    LISTEN(toplevel, request_minimize,   l_req_minimize,   on_req_minimize);
    LISTEN(toplevel, set_title,          l_set_title,      on_set_title);
#undef LISTEN
}

/* ================================================================== *
 *  pm_view_maximize                                                  *
 * ================================================================== */
void pm_view_maximize(struct pm_view *v) {
    struct pm_output *out = primary_output(v->server);
    if (!out) return;

    struct wlr_box usable;
    pm_output_usable_area(out, &usable);

    int dh = pm_view_deco_height(v);

    struct wlr_box lb;
    wlr_output_layout_get_box(v->server->output_layout,
                               out->wlr_output, &lb);

    v->x = lb.x + usable.x;
    v->y = lb.y + usable.y + dh;
    v->width  = usable.width;
    v->height = usable.height - dh;

    wlr_xdg_toplevel_set_size(v->toplevel, v->width, v->height);
    wlr_xdg_toplevel_set_maximized(v->toplevel, true);
    wlr_scene_node_set_position(&v->scene_tree->node, v->x, v->y);
    pm_view_update_decorations(v);
}

/* ================================================================== *
 *  pm_view_raise                                                     *
 * ================================================================== */
void pm_view_raise(struct pm_view *v) {
    wlr_scene_node_raise_to_top(&v->scene_tree->node);
    wl_list_remove(&v->link);
    wl_list_insert(&v->server->views, &v->link);
}

/* ================================================================== *
 *  pm_view_minimise / restore                                        *
 * ================================================================== */
void pm_view_minimise(struct pm_view *v) {
    if (v->minimised) return;
    v->minimised = true;
    wlr_scene_node_set_enabled(&v->scene_tree->node, false);
    wlr_xdg_toplevel_set_minimized(v->toplevel, true);

    struct pm_view *nxt;
    wl_list_for_each(nxt, &v->server->views, link) {
        if (nxt != v && nxt->mapped && !nxt->minimised) {
            pm_server_focus_view(v->server, nxt,
                                  nxt->toplevel->base->surface);
            return;
        }
    }
}

void pm_view_restore(struct pm_view *v) {
    if (!v->minimised) return;
    v->minimised = false;
    wlr_scene_node_set_enabled(&v->scene_tree->node, true);
    wlr_xdg_toplevel_set_minimized(v->toplevel, false);
    pm_server_focus_view(v->server, v, v->toplevel->base->surface);
}

/* ================================================================== *
 *  pm_view_close                                                     *
 * ================================================================== */
void pm_view_close(struct pm_view *v) {
    wlr_xdg_toplevel_send_close(v->toplevel);
}
