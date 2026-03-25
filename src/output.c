#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <cairo.h>

#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/box.h>
#include <wlr/util/log.h>

#include "server.h"
#include "output.h"
#include "shell.h"
#include "config.h"

/* ── Frame handler ───────────────────────────────────────────────── */
static void on_frame(struct wl_listener *l, void *data) {
    struct pm_output *o = wl_container_of(l, o, l_frame);
    (void)data;

    wlr_scene_output_commit(o->scene_output, NULL);

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    wlr_scene_output_send_frame_done(o->scene_output, &now);
}

/* ── Mode change / resize ────────────────────────────────────────── */
static void on_request_state(struct wl_listener *l, void *data) {
    struct pm_output *o = wl_container_of(l, o, l_request_state);
    const struct wlr_output_event_request_state *ev = data;
    wlr_output_commit_state(o->wlr_output, ev->state);

    /* Re-layout after mode change */
    int ow, oh;
    wlr_output_effective_resolution(o->wlr_output, &ow, &oh);
    pm_output_repaint_bg(o);
    if (o->server->shell)
        pm_shell_on_output_resize(o->server->shell, ow, oh);
}

/* ── Destroy ─────────────────────────────────────────────────────── */
static void on_destroy(struct wl_listener *l, void *data) {
    struct pm_output *o = wl_container_of(l, o, l_destroy);
    (void)data;

    wl_list_remove(&o->l_frame.link);
    wl_list_remove(&o->l_request_state.link);
    wl_list_remove(&o->l_destroy.link);
    wl_list_remove(&o->link);

    if (o->bg_node) wlr_scene_node_destroy(&o->bg_node->node);
    if (o->bg_buf)  pm_cairo_buf_destroy(o->bg_buf);
    free(o);
}

/* ================================================================== *
 *  pm_output_usable_area                                            *
 * ================================================================== */
void pm_output_usable_area(struct pm_output *o, struct wlr_box *box) {
    int ow, oh;
    wlr_output_effective_resolution(o->wlr_output, &ow, &oh);
    box->x = 0;
    box->y = 0;
    box->width  = ow;
    box->height = oh;

    /* Reserve space for the taskbar at the bottom */
    if (o->server->shell && o->server->shell->taskbar) {
        struct pm_taskbar *tb = o->server->shell->taskbar;
        int reserve = tb->h + (tb->always_visible ? 0 : TB_MARGIN * 2);
        box->height -= reserve;
    }
}

/* ================================================================== *
 *  pm_output_repaint_bg — draw the diagonal navy gradient            *
 * ================================================================== */
void pm_output_repaint_bg(struct pm_output *o) {
    int ow, oh;
    wlr_output_effective_resolution(o->wlr_output, &ow, &oh);
    if (ow <= 0 || oh <= 0) return;

    /* Destroy old */
    if (o->bg_node) { wlr_scene_node_destroy(&o->bg_node->node); o->bg_node = NULL; }
    if (o->bg_buf)  { pm_cairo_buf_destroy(o->bg_buf); o->bg_buf = NULL; }

    o->bg_buf = pm_cairo_buf_create(ow, oh);
    if (!o->bg_buf) return;

    cairo_t *cr = pm_cairo_buf_cr(o->bg_buf);

    /* Diagonal gradient: top-left → bottom-right */
    cairo_pattern_t *pat = cairo_pattern_create_linear(0, 0, ow, oh);
    float *tl = o->server->config->bg_tl;
    float *br = o->server->config->bg_br;
    cairo_pattern_add_color_stop_rgba(pat, 0.0, tl[0], tl[1], tl[2], 1.0);
    cairo_pattern_add_color_stop_rgba(pat, 1.0, br[0], br[1], br[2], 1.0);
    cairo_set_source(cr, pat);
    cairo_paint(cr);
    cairo_pattern_destroy(pat);

    /* Subtle vignette: darken the edges slightly */
    cairo_pattern_t *vig = cairo_pattern_create_radial(
        ow * 0.5, oh * 0.5, oh * 0.2,
        ow * 0.5, oh * 0.5, oh * 0.9);
    cairo_pattern_add_color_stop_rgba(vig, 0.0, 0, 0, 0, 0.0);
    cairo_pattern_add_color_stop_rgba(vig, 1.0, 0, 0, 0, 0.35);
    cairo_set_source(cr, vig);
    cairo_paint(cr);
    cairo_pattern_destroy(vig);

    cairo_destroy(cr);

    o->bg_node = wlr_scene_buffer_create(o->server->layer_bg, &o->bg_buf->base);

    /* Position at this output's origin in layout coords */
    struct wlr_box lb;
    wlr_output_layout_get_box(o->server->output_layout, o->wlr_output, &lb);
    wlr_scene_node_set_position(&o->bg_node->node, lb.x, lb.y);
}

/* ================================================================== *
 *  pm_output_init                                                    *
 * ================================================================== */
void pm_output_init(struct pm_server *s, struct wlr_output *wlr_out) {
    struct pm_output *o = calloc(1, sizeof(*o));
    if (!o) return;

    o->server     = s;
    o->wlr_output = wlr_out;

    /* Add to layout — wlroots arranges outputs side-by-side automatically */
    struct wlr_output_layout_output *lo =
        wlr_output_layout_add_auto(s->output_layout, wlr_out);

    /* Create scene output and attach to layout */
    o->scene_output = wlr_scene_output_create(s->scene, wlr_out);
    wlr_scene_output_layout_add_output(s->scene_layout, lo, o->scene_output);

    /* Draw background */
    pm_output_repaint_bg(o);

    /* Listeners */
    o->l_frame.notify = on_frame;
    wl_signal_add(&wlr_out->events.frame, &o->l_frame);

    o->l_request_state.notify = on_request_state;
    wl_signal_add(&wlr_out->events.request_state, &o->l_request_state);

    o->l_destroy.notify = on_destroy;
    wl_signal_add(&wlr_out->events.destroy, &o->l_destroy);

    wl_list_insert(&s->outputs, &o->link);

    /* Reposition shell widgets for this output */
    int ow, oh;
    wlr_output_effective_resolution(wlr_out, &ow, &oh);
    if (s->shell) pm_shell_on_output_resize(s->shell, ow, oh);

    wlr_log(WLR_INFO, "Output '%s' connected (%dx%d)",
            wlr_out->name, ow, oh);
}
