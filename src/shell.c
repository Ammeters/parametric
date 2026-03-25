#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>

#include <cairo.h>
#include <pango/pangocairo.h>

#include <wayland-server-core.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_output.h>
#include <wlr/util/log.h>
#include <wlr/util/box.h>

/* DRM format constant without requiring libdrm */
#ifndef DRM_FORMAT_ARGB8888
#define DRM_FORMAT_ARGB8888 0x34325241
#endif

#include "server.h"
#include "shell.h"
#include "view.h"
#include "output.h"
#include "config.h"
#include "animate.h"

/* ================================================================== *
 *  SECTION 1 — pm_cairo_buf: shm-backed wlr_buffer for Cairo        *
 * ================================================================== */

static void cb_destroy(struct wlr_buffer *b) {
    struct pm_cairo_buf *cb = wl_container_of(b, cb, base);
    munmap(cb->data, cb->size);
    close(cb->fd);
    free(cb);
}

static bool cb_begin_access(struct wlr_buffer *b, uint32_t flags,
                              void **data, uint32_t *fmt, size_t *stride) {
    struct pm_cairo_buf *cb = wl_container_of(b, cb, base);
    (void)flags;
    *data   = cb->data;
    *fmt    = DRM_FORMAT_ARGB8888;
    *stride = (size_t)cb->stride;
    return true;
}

static void cb_end_access(struct wlr_buffer *b) { (void)b; }

static const struct wlr_buffer_impl cb_impl = {
    .destroy               = cb_destroy,
    .begin_data_ptr_access = cb_begin_access,
    .end_data_ptr_access   = cb_end_access,
};

struct pm_cairo_buf *pm_cairo_buf_create(int w, int h) {
    if (w <= 0 || h <= 0) return NULL;

    struct pm_cairo_buf *cb = calloc(1, sizeof(*cb));
    if (!cb) return NULL;

    cb->width  = w;
    cb->height = h;
    cb->stride = w * 4;
    cb->size   = (size_t)(cb->stride * h);

    cb->fd = memfd_create("pm_buf", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (cb->fd < 0) { free(cb); return NULL; }
    if (ftruncate(cb->fd, (off_t)cb->size) < 0) {
        close(cb->fd); free(cb); return NULL;
    }

    cb->data = mmap(NULL, cb->size, PROT_READ | PROT_WRITE,
                    MAP_SHARED, cb->fd, 0);
    if (cb->data == MAP_FAILED) {
        close(cb->fd); free(cb); return NULL;
    }
    memset(cb->data, 0, cb->size);

    wlr_buffer_init(&cb->base, &cb_impl, w, h);
    return cb;
}

void pm_cairo_buf_destroy(struct pm_cairo_buf *cb) {
    if (cb) wlr_buffer_drop(&cb->base);
}

cairo_t *pm_cairo_buf_cr(struct pm_cairo_buf *cb) {
    cairo_surface_t *surf = cairo_image_surface_create_for_data(
        (unsigned char *)cb->data, CAIRO_FORMAT_ARGB32,
        cb->width, cb->height, cb->stride);
    cairo_t *cr = cairo_create(surf);
    cairo_surface_destroy(surf);
    return cr;
}

/* ================================================================== *
 *  SECTION 2 — Drawing primitives                                   *
 * ================================================================== */

/* Full rounded rectangle path */
static void path_round_rect(cairo_t *cr,
                             double x, double y, double w, double h, double r)
{
    r = fmin(r, fmin(w, h) / 2.0);
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r,     r, -M_PI_2, 0.0);
    cairo_arc(cr, x + w - r, y + h - r, r,  0.0,    M_PI_2);
    cairo_arc(cr, x + r,     y + h - r, r,  M_PI_2, M_PI);
    cairo_arc(cr, x + r,     y + r,     r,  M_PI,   3.0 * M_PI_2);
    cairo_close_path(cr);
}

/* Rounded rect with independent per-corner radii (NW, NE, SE, SW) */
static void path_round_rect4(cairo_t *cr,
                              double x, double y, double w, double h,
                              double rnw, double rne, double rse, double rsw)
{
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + rnw,     y + rnw,     rnw, M_PI,    3.0 * M_PI_2);
    cairo_arc(cr, x + w - rne, y + rne,     rne, -M_PI_2, 0.0);
    cairo_arc(cr, x + w - rse, y + h - rse, rse,  0.0,    M_PI_2);
    cairo_arc(cr, x + rsw,     y + h - rsw, rsw,  M_PI_2, M_PI);
    cairo_close_path(cr);
}

/* Frosted-glass surface: solid base + white highlight + subtle border */
static void draw_frosted(cairo_t *cr,
                          double x, double y, double w, double h, double r,
                          double R, double G, double B, double A)
{
    /* Base */
    path_round_rect(cr, x, y, w, h, r);
    cairo_set_source_rgba(cr, R, G, B, A);
    cairo_fill_preserve(cr);

    /* Inner highlight (top half) */
    cairo_pattern_t *hi = cairo_pattern_create_linear(x, y, x, y + h * 0.55);
    cairo_pattern_add_color_stop_rgba(hi, 0.0, 1, 1, 1, 0.13);
    cairo_pattern_add_color_stop_rgba(hi, 1.0, 1, 1, 1, 0.0);
    cairo_set_source(cr, hi);
    cairo_fill(cr);
    cairo_pattern_destroy(hi);

    /* Thin border */
    path_round_rect(cr, x + 0.5, y + 0.5, w - 1, h - 1, r - 0.5);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.20);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);
}

/* Coloured rounded-square icon with centred initial letter */
static void draw_icon(cairo_t *cr, double x, double y, double sz,
                       const char *label, double R, double G, double B)
{
    double r = sz * 0.22;
    path_round_rect(cr, x, y, sz, sz, r);
    cairo_set_source_rgba(cr, R, G, B, 0.88);
    cairo_fill(cr);

    if (label && label[0]) {
        char ch[2] = { label[0], 0 };
        cairo_set_source_rgba(cr, 1, 1, 1, 0.95);
        cairo_select_font_face(cr, "Sans",
            CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, sz * 0.44);
        cairo_text_extents_t te;
        cairo_text_extents(cr, ch, &te);
        cairo_move_to(cr,
            x + (sz - te.width)  / 2.0 - te.x_bearing,
            y + (sz + te.height) / 2.0 - te.y_bearing - te.height);
        cairo_show_text(cr, ch);
    }
}

/* Distinct palette for per-app colouring */
static void palette_color(int i, double *R, double *G, double *B) {
    static const float p[][3] = {
        {0.22f, 0.48f, 0.86f}, /* blue   */
        {0.86f, 0.28f, 0.23f}, /* red    */
        {0.18f, 0.69f, 0.42f}, /* green  */
        {0.90f, 0.60f, 0.10f}, /* amber  */
        {0.54f, 0.22f, 0.80f}, /* violet */
        {0.10f, 0.64f, 0.76f}, /* teal   */
        {0.85f, 0.40f, 0.12f}, /* orange */
        {0.36f, 0.58f, 0.78f}, /* sky    */
    };
    int n = (int)(sizeof(p) / sizeof(p[0]));
    *R = p[i % n][0]; *G = p[i % n][1]; *B = p[i % n][2];
}

/* ================================================================== *
 *  SECTION 3 — Taskbar                                              *
 * ================================================================== */

struct pm_taskbar *pm_taskbar_create(struct pm_server *s) {
    struct pm_taskbar *tb = calloc(1, sizeof(*tb));
    if (!tb) return NULL;

    tb->server = s;
    tb->h      = TB_H_NORMAL;
    tb->always_visible = s->config->taskbar_always_visible;

    tb->tree = wlr_scene_tree_create(s->layer_taskbar);
    return tb;
}

void pm_taskbar_destroy(struct pm_taskbar *tb) {
    if (!tb) return;
    if (tb->node) wlr_scene_node_destroy(&tb->node->node);
    if (tb->buf)  pm_cairo_buf_destroy(tb->buf);
    wlr_scene_node_destroy(&tb->tree->node);
    free(tb);
}

void pm_taskbar_set_expanded(struct pm_taskbar *tb, bool exp) {
    tb->expanded = exp;
    tb->h = exp ? TB_H_OVERVIEW : TB_H_NORMAL;
}

void pm_taskbar_render(struct pm_taskbar *tb) {
    if (tb->w <= 0 || tb->h <= 0) return;

    struct pm_server *s = tb->server;

    /* Recreate buffer */
    if (tb->node) { wlr_scene_node_destroy(&tb->node->node); tb->node = NULL; }
    if (tb->buf)  { pm_cairo_buf_destroy(tb->buf); tb->buf = NULL; }

    tb->buf = pm_cairo_buf_create(tb->w, tb->h);
    if (!tb->buf) return;

    cairo_t *cr = pm_cairo_buf_cr(tb->buf);
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    /* ── Background ─────────────────────────────────────────────── */
    bool always = tb->always_visible;
    float *f    = s->config->taskbar_fill;
    double cr2  = always ? 0 : TB_CORNER_R;

    if (always) {
        /* Only top corners rounded; bottom flush against screen edge */
        path_round_rect4(cr, 0, 0, tb->w, tb->h,
                         TB_CORNER_R, TB_CORNER_R, 0, 0);
    } else {
        path_round_rect(cr, 0, 0, tb->w, tb->h, cr2);
    }
    cairo_set_source_rgba(cr, f[0], f[1], f[2], f[3]);
    cairo_fill_preserve(cr);

    /* Top-edge highlight */
    cairo_pattern_t *hi = cairo_pattern_create_linear(0, 0, 0, tb->h * 0.5);
    cairo_pattern_add_color_stop_rgba(hi, 0.0, 1, 1, 1, 0.12);
    cairo_pattern_add_color_stop_rgba(hi, 1.0, 1, 1, 1, 0.00);
    cairo_set_source(cr, hi);
    cairo_fill(cr);
    cairo_pattern_destroy(hi);

    /* Border */
    if (always) {
        path_round_rect4(cr, 0.5, 0.5, tb->w - 1, tb->h - 1,
                         TB_CORNER_R, TB_CORNER_R, 0, 0);
    } else {
        path_round_rect(cr, 0.5, 0.5, tb->w - 1, tb->h - 1, cr2);
    }
    cairo_set_source_rgba(cr, 1, 1, 1, 0.16);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);

    /* ── App icons ──────────────────────────────────────────────── */
    int count = 0;
    struct pm_view *v;
    wl_list_for_each(v, &s->views, link) {
        if (v->mapped || v->minimised) count++;
    }

    if (count > 0) {
        int sz    = TB_ICON_SZ;
        int gap   = TB_ICON_GAP;
        int rows  = tb->expanded ? 2 : 1;
        int cols  = (count + rows - 1) / rows;
        int tot_w = cols * sz + (cols - 1) * gap;
        int ox    = (tb->w - tot_w) / 2;

        int i = 0;
        wl_list_for_each(v, &s->views, link) {
            if (!v->mapped && !v->minimised) continue;
            if (i >= rows * cols) break;

            int row = i / cols;
            int col = i % cols;

            int ix = ox + col * (sz + gap);
            int iy;
            if (rows == 1) {
                iy = (tb->h - sz) / 2;
            } else {
                int tot_h = rows * sz + (rows - 1) * gap;
                iy = (tb->h - tot_h) / 2 + row * (sz + gap);
            }

            double R, G, B;
            palette_color(i, &R, &G, &B);
            const char *title = v->toplevel->title
                              ? v->toplevel->title : "App";
            draw_icon(cr, ix, iy, sz, title, R, G, B);

            /* Active indicator dot below icon */
            if (!v->minimised) {
                cairo_arc(cr, ix + sz * 0.5, iy + sz + 5.5, 2.8, 0, 2 * M_PI);
                cairo_set_source_rgba(cr, 1, 1, 1, 0.85);
                cairo_fill(cr);
            }

            i++;
        }
    }

    cairo_destroy(cr);

    tb->node = wlr_scene_buffer_create(tb->tree, &tb->buf->base);
}

void pm_taskbar_reposition(struct pm_taskbar *tb, int ow, int oh) {
    tb->out_w = ow;
    tb->out_h = oh;

    bool always = tb->always_visible;
    tb->w = always ? ow : (ow - TB_MARGIN * 2);
    tb->x = always ? 0  : TB_MARGIN;
    tb->y = oh - tb->h - (always ? 0 : TB_MARGIN);

    wlr_scene_node_set_position(&tb->tree->node, tb->x, tb->y);
    pm_taskbar_render(tb);
}

bool pm_taskbar_handle_click(struct pm_taskbar *tb, double lx, double ly) {
    /* Convert from layout coords to taskbar-local */
    double tx = lx - tb->x;
    double ty = ly - tb->y;
    if (tx < 0 || tx > tb->w || ty < 0 || ty > tb->h) return false;

    struct pm_server *s = tb->server;
    int count = 0;
    struct pm_view *v;
    wl_list_for_each(v, &s->views, link) {
        if (v->mapped || v->minimised) count++;
    }
    if (count == 0) return false;

    int sz   = TB_ICON_SZ;
    int gap  = TB_ICON_GAP;
    int rows = tb->expanded ? 2 : 1;
    int cols = (count + rows - 1) / rows;
    int ox   = (tb->w - (cols * sz + (cols - 1) * gap)) / 2;

    int i = 0;
    wl_list_for_each(v, &s->views, link) {
        if (!v->mapped && !v->minimised) continue;
        if (i >= rows * cols) break;

        int row = i / cols, col = i % cols;
        int ix = ox + col * (sz + gap);
        int iy = (rows == 1) ? (tb->h - sz) / 2
                             : (tb->h - (rows * sz + (rows - 1) * gap)) / 2
                               + row * (sz + gap);

        if (tx >= ix && tx <= ix + sz && ty >= iy && ty <= iy + sz) {
            if (v->minimised) pm_view_restore(v);
            else {
                pm_server_focus_view(s, v, v->toplevel->base->surface);
                if (s->shell->overview->active)
                    pm_overview_hide(s->shell->overview);
            }
            return true;
        }
        i++;
    }
    return false;
}

/* ================================================================== *
 *  SECTION 4 — Homebar                                              *
 * ================================================================== */

static int hb_timer_cb(void *data) {
    struct pm_homebar *hb = data;

    float v = pm_anim_tick(&hb->anim);

    /* Render pill */
    struct pm_server *s = hb->server;
    int ow = hb->out_w, oh = hb->out_h;
    int pw = (int)(ow * HB_W_FRAC);
    if (pw < 80) pw = 80;
    int ph = HB_H;

    /* Recreate buffer */
    if (hb->node) { wlr_scene_node_destroy(&hb->node->node); hb->node = NULL; }
    if (hb->buf)  { pm_cairo_buf_destroy(hb->buf); hb->buf = NULL; }

    hb->buf = pm_cairo_buf_create(pw, ph);
    if (!hb->buf) return 0;

    cairo_t *cr = pm_cairo_buf_cr(hb->buf);
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR); cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    float *hbc = s->config->homebar;
    path_round_rect(cr, 0, 0, pw, ph, ph / 2.0);
    cairo_set_source_rgba(cr, hbc[0], hbc[1], hbc[2], hbc[3] * v);
    cairo_fill(cr);
    cairo_destroy(cr);

    hb->node = wlr_scene_buffer_create(hb->tree, &hb->buf->base);
    wlr_scene_node_set_position(&hb->node->node,
        (ow - pw) / 2,
        oh - ph - HB_MARGIN_BOT);

    wlr_scene_node_set_enabled(&hb->tree->node, v > 0.01f);

    if (hb->anim.running)
        wl_event_source_timer_update(hb->timer, PM_FRAME_MS);

    return 0;
}

struct pm_homebar *pm_homebar_create(struct pm_server *s) {
    struct pm_homebar *hb = calloc(1, sizeof(*hb));
    if (!hb) return NULL;
    hb->server = s;
    hb->tree   = wlr_scene_tree_create(s->layer_taskbar);
    wlr_scene_node_set_enabled(&hb->tree->node, false);
    hb->timer  = wl_event_loop_add_timer(s->event_loop, hb_timer_cb, hb);
    return hb;
}

void pm_homebar_destroy(struct pm_homebar *hb) {
    if (!hb) return;
    if (hb->timer) wl_event_source_remove(hb->timer);
    if (hb->node)  wlr_scene_node_destroy(&hb->node->node);
    if (hb->buf)   pm_cairo_buf_destroy(hb->buf);
    wlr_scene_node_destroy(&hb->tree->node);
    free(hb);
}

void pm_homebar_reposition(struct pm_homebar *hb, int ow, int oh) {
    hb->out_w = ow;
    hb->out_h = oh;
    wlr_scene_node_set_position(&hb->tree->node, 0, 0);
}

void pm_homebar_show(struct pm_homebar *hb) {
    if (hb->anim.to >= 0.99f) return;
    pm_anim_start(&hb->anim, 1.0f, PM_DUR_HOMEBAR_MS);
    wl_event_source_timer_update(hb->timer, 1);
}

void pm_homebar_hide(struct pm_homebar *hb) {
    if (hb->anim.to <= 0.01f) return;
    pm_anim_start(&hb->anim, 0.0f, PM_DUR_HOMEBAR_MS);
    wl_event_source_timer_update(hb->timer, 1);
}

/* ================================================================== *
 *  SECTION 5 — Overview                                             *
 * ================================================================== */

static void ov_apply_reveal(struct pm_overview *ov, float v) {
    /* Dim overlay opacity */
    if (ov->dim_rect)
        wlr_scene_rect_set_color(ov->dim_rect,
            (float[]){0.0f, 0.04f, 0.12f, v * 0.58f});

    /* Cards slide in from the right */
    if (ov->cards_tree) {
        int offset = (int)((1.0f - v) * (float)ov->out_w * 0.35f);
        wlr_scene_node_set_position(&ov->cards_tree->node, offset, 0);
    }
}

static int ov_timer_cb(void *data) {
    struct pm_overview *ov = data;
    float v = pm_anim_tick(&ov->anim);
    ov_apply_reveal(ov, v);

    if (!ov->anim.running) {
        /* Animation complete */
        if (ov->anim.to <= 0.0f) {
            /* Hide layers */
            wlr_scene_node_set_enabled(
                &ov->server->layer_shell_bg->node, false);
            wlr_scene_node_set_enabled(
                &ov->server->layer_overview->node, false);
            /* Collapse taskbar */
            pm_taskbar_set_expanded(ov->server->shell->taskbar, false);
            pm_taskbar_reposition(ov->server->shell->taskbar,
                                   ov->out_w, ov->out_h);
        }
        return 0;
    }

    wl_event_source_timer_update(ov->timer, PM_FRAME_MS);
    return 0;
}

struct pm_overview *pm_overview_create(struct pm_server *s) {
    struct pm_overview *ov = calloc(1, sizeof(*ov));
    if (!ov) return NULL;
    ov->server = s;

    /* Fullscreen dim overlay */
    ov->dim_rect = wlr_scene_rect_create(s->layer_shell_bg,
                                          1, 1,
                                          (float[]){0,0,0,0});

    /* Cards container */
    ov->cards_tree = wlr_scene_tree_create(s->layer_overview);

    ov->timer = wl_event_loop_add_timer(s->event_loop, ov_timer_cb, ov);
    return ov;
}

void pm_overview_destroy(struct pm_overview *ov) {
    if (!ov) return;
    if (ov->timer) wl_event_source_remove(ov->timer);
    free(ov);
}

void pm_overview_rebuild(struct pm_overview *ov) {
    struct pm_server *s = ov->server;

    /* Destroy all existing card children */
    struct wlr_scene_node *n, *tmp;
    wl_list_for_each_safe(n, tmp, &ov->cards_tree->children, link)
        wlr_scene_node_destroy(n);

    /* Clear card refs on views */
    struct pm_view *v;
    wl_list_for_each(v, &s->views, link)
        v->ov_card = NULL, v->ov_bg = NULL;

    int count = 0;
    wl_list_for_each(v, &s->views, link) {
        if (v->mapped || v->minimised) count++;
    }
    if (count == 0) return;

    int ow = ov->out_w > 0 ? ov->out_w : 1280;
    int oh = ov->out_h > 0 ? ov->out_h : 800;

    /* Lay out cards horizontally, vertically centred (above taskbar) */
    int cw = OV_CARD_W, ch = OV_CARD_H, gap = OV_CARD_GAP;
    int tb_reserve = TB_H_OVERVIEW + TB_MARGIN * 2;
    int area_h     = oh - tb_reserve;

    int total_w = count * cw + (count - 1) * gap;
    int start_x = (ow - total_w) / 2;
    if (start_x < gap) start_x = gap;
    int start_y = (area_h - ch) / 2;
    if (start_y < gap) start_y = gap;

    int i = 0;
    wl_list_for_each(v, &s->views, link) {
        if (!v->mapped && !v->minimised) continue;

        int cx = start_x + i * (cw + gap);
        int cy = start_y;

        /* Card container tree */
        v->ov_card = wlr_scene_tree_create(ov->cards_tree);
        v->ov_card->node.data = v;  /* back-reference for click testing */
        wlr_scene_node_set_position(&v->ov_card->node, cx, cy);

        /* Card body */
        float *cf = s->config->card_fill;
        v->ov_bg = wlr_scene_rect_create(v->ov_card, cw, ch, cf);

        /* App icon in card */
        int icon_sz = 60;
        struct pm_cairo_buf *ib = pm_cairo_buf_create(cw, ch - 34);
        if (ib) {
            cairo_t *cr = pm_cairo_buf_cr(ib);
            cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR); cairo_paint(cr);
            cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

            double R, G, B;
            palette_color(i, &R, &G, &B);
            const char *title = v->toplevel->title
                              ? v->toplevel->title : "App";
            int ix = (cw - icon_sz) / 2;
            int iy = ((ch - 34) - icon_sz) / 2;
            draw_icon(cr, ix, iy, icon_sz, title, R, G, B);
            cairo_destroy(cr);

            struct wlr_scene_buffer *sn =
                wlr_scene_buffer_create(v->ov_card, &ib->base);
            wlr_scene_node_set_position(&sn->node, 0, 0);
        }

        /* Title label */
        struct pm_cairo_buf *lb = pm_cairo_buf_create(cw, 30);
        if (lb) {
            cairo_t *cr = pm_cairo_buf_cr(lb);
            cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR); cairo_paint(cr);
            cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

            const char *title = v->toplevel->title
                              ? v->toplevel->title : "Application";
            cairo_set_source_rgba(cr, 1, 1, 1, 0.88);
            cairo_select_font_face(cr, "Sans",
                CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
            cairo_set_font_size(cr, 12.0);
            cairo_text_extents_t te;
            cairo_text_extents(cr, title, &te);
            /* Truncate visually by clamping draw position */
            double tx = (cw - te.width) / 2.0 - te.x_bearing;
            if (tx < 4.0) tx = 4.0;
            cairo_move_to(cr, tx, 20.0);
            cairo_show_text(cr, title);
            cairo_destroy(cr);

            struct wlr_scene_buffer *sn =
                wlr_scene_buffer_create(v->ov_card, &lb->base);
            wlr_scene_node_set_position(&sn->node, 0, ch - 30);
        }

        /* Close button (× in top-right) */
        struct pm_cairo_buf *xb = pm_cairo_buf_create(OV_CLOSE_HIT, OV_CLOSE_HIT);
        if (xb) {
            cairo_t *cr = pm_cairo_buf_cr(xb);
            cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR); cairo_paint(cr);
            cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

            /* Pill background */
            path_round_rect(cr, 2, 2, OV_CLOSE_HIT - 4, OV_CLOSE_HIT - 4, 6);
            cairo_set_source_rgba(cr, 0.9, 0.25, 0.25, 0.85);
            cairo_fill(cr);
            /* × mark */
            cairo_set_source_rgba(cr, 1, 1, 1, 0.95);
            cairo_set_line_width(cr, 2.0);
            double m = OV_CLOSE_HIT / 2.0, d = 4.5;
            cairo_move_to(cr, m - d, m - d); cairo_line_to(cr, m + d, m + d);
            cairo_move_to(cr, m + d, m - d); cairo_line_to(cr, m - d, m + d);
            cairo_stroke(cr);
            cairo_destroy(cr);

            struct wlr_scene_buffer *sn =
                wlr_scene_buffer_create(v->ov_card, &xb->base);
            wlr_scene_node_set_position(&sn->node,
                cw - OV_CLOSE_HIT - 2, 2);
        }

        i++;
    }
}

void pm_overview_show(struct pm_overview *ov) {
    if (ov->active) return;
    ov->active = true;

    struct pm_server *s = ov->server;

    /* Capture output dimensions */
    struct pm_output *out;
    wl_list_for_each(out, &s->outputs, link) {
        wlr_output_effective_resolution(out->wlr_output,
                                        &ov->out_w, &ov->out_h);
        break;
    }

    /* Resize dim rect to cover full output */
    wlr_scene_rect_set_size(ov->dim_rect, ov->out_w, ov->out_h);

    /* Expand taskbar to 2 rows */
    pm_taskbar_set_expanded(s->shell->taskbar, true);
    pm_taskbar_reposition(s->shell->taskbar, ov->out_w, ov->out_h);

    /* Rebuild card grid */
    pm_overview_rebuild(ov);

    /* Enable layers */
    wlr_scene_node_set_enabled(&s->layer_shell_bg->node, true);
    wlr_scene_node_set_enabled(&s->layer_overview->node,  true);

    /* Kick off slide-in animation */
    pm_anim_start(&ov->anim, 1.0f, PM_DUR_OVERVIEW_MS);
    ov_apply_reveal(ov, 0.0f);
    wl_event_source_timer_update(ov->timer, 1);
}

void pm_overview_hide(struct pm_overview *ov) {
    if (!ov->active) return;
    ov->active = false;

    pm_anim_start(&ov->anim, 0.0f, PM_DUR_OVERVIEW_MS);
    wl_event_source_timer_update(ov->timer, 1);
}

void pm_overview_toggle(struct pm_overview *ov) {
    if (ov->active) pm_overview_hide(ov);
    else            pm_overview_show(ov);
}

bool pm_overview_handle_click(struct pm_overview *ov, double lx, double ly) {
    if (!ov->active) return false;

    struct pm_server *s = ov->server;

    struct pm_view *v;
    wl_list_for_each(v, &s->views, link) {
        if (!v->ov_card) continue;

        int cx = 0, cy = 0;
        wlr_scene_node_coords(&v->ov_card->node, &cx, &cy);

        /* Adjust for cards_tree offset (animation shift) */
        int tx = 0, ty = 0;
        wlr_scene_node_coords(&ov->cards_tree->node, &tx, &ty);

        double card_lx = lx - tx;
        double card_ly = ly - ty;

        int local_x = (int)card_lx - cx + tx;
        int local_y = (int)card_ly - cy + ty;
        (void)local_x; (void)local_y;

        /* Simpler: just test against absolute coords */
        if (lx >= cx && lx <= cx + OV_CARD_W &&
            ly >= cy && ly <= cy + OV_CARD_H)
        {
            /* Top-right corner = close button */
            if (lx >= cx + OV_CARD_W - OV_CLOSE_HIT - 2 &&
                ly <= cy + OV_CLOSE_HIT + 2)
            {
                pm_view_close(v);
                pm_overview_rebuild(ov);
                return true;
            }
            /* Anywhere else in card = activate */
            pm_overview_hide(ov);
            if (v->minimised) pm_view_restore(v);
            pm_server_focus_view(s, v, v->toplevel->base->surface);
            return true;
        }
    }
    return false;
}

/* ================================================================== *
 *  SECTION 6 — Notification / Control Panel                         *
 * ================================================================== */

static void np_render(struct pm_notif_panel *np, float reveal) {
    struct pm_server *s = np->server;
    int ow = np->out_w > 0 ? np->out_w : 1280;
    int oh = np->out_h > 0 ? np->out_h : 800;

    int pw = (int)(ow * 0.88f);
    if (pw > 580) pw = 580;
    int ph = 290;

    if (np->node) { wlr_scene_node_destroy(&np->node->node); np->node = NULL; }
    if (np->buf)  { pm_cairo_buf_destroy(np->buf); np->buf = NULL; }

    if (reveal < 0.01f) {
        wlr_scene_node_set_enabled(&np->tree->node, false);
        return;
    }

    np->buf = pm_cairo_buf_create(pw, ph);
    if (!np->buf) return;

    cairo_t *cr = pm_cairo_buf_cr(np->buf);
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR); cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    /* Panel body */
    float *nb = s->config->notif_fill;
    draw_frosted(cr, 0, 0, pw, ph, 20,
                 nb[0], nb[1], nb[2], nb[3] * reveal);

    /* ── Clock ──────────────────────────────────────────────── */
    time_t now_t = time(NULL);
    struct tm *tm_now = localtime(&now_t);
    char time_str[8], date_str[32];
    strftime(time_str, sizeof(time_str), "%H:%M", tm_now);
    strftime(date_str, sizeof(date_str), "%A, %d %B", tm_now);

    cairo_select_font_face(cr, "Sans",
        CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 52.0);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.96 * reveal);
    cairo_text_extents_t te;
    cairo_text_extents(cr, time_str, &te);
    cairo_move_to(cr, (pw - te.width) / 2.0 - te.x_bearing, 76.0);
    cairo_show_text(cr, time_str);

    cairo_set_font_size(cr, 15.0);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.65 * reveal);
    cairo_text_extents(cr, date_str, &te);
    cairo_move_to(cr, (pw - te.width) / 2.0 - te.x_bearing, 102.0);
    cairo_show_text(cr, date_str);

    /* ── Divider ─────────────────────────────────────────────── */
    cairo_set_source_rgba(cr, 1, 1, 1, 0.18 * reveal);
    cairo_set_line_width(cr, 1.0);
    cairo_move_to(cr, 22, 120); cairo_line_to(cr, pw - 22, 120);
    cairo_stroke(cr);

    /* ── Quick-toggle tiles ───────────────────────────────────── */
    typedef struct { const char *label; const char *icon; } Toggle;
    Toggle toggles[] = {
        {"Wi-Fi", ""},
        {"Bluetooth", ""},
        {"Do Not Disturb", ""},
        {"Dark Mode", ""},
    };
    int nt = 4;
    int bw = 54, bh = 44, bg = 10;
    int bstart = (pw - (nt * bw + (nt - 1) * bg)) / 2;
    int by = 134;
    for (int i = 0; i < nt; i++) {
        draw_frosted(cr, bstart + i * (bw + bg), by, bw, bh, 10,
                     0.06, 0.28, 0.55, 0.78 * reveal);
        cairo_set_source_rgba(cr, 1, 1, 1, 0.85 * reveal);
        cairo_select_font_face(cr, "Sans",
            CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 9.5);
        cairo_text_extents_t te2;
        cairo_text_extents(cr, toggles[i].label, &te2);
        double lx2 = bstart + i * (bw + bg) + (bw - te2.width) / 2.0
                     - te2.x_bearing;
        double ly2 = by + (bh + te2.height) / 2.0 - te2.y_bearing
                     - te2.height;
        cairo_move_to(cr, lx2, ly2);
        cairo_show_text(cr, toggles[i].label);
    }

    /* ── Brightness bar ──────────────────────────────────────── */
    int sl_x = 22, sl_y = 198, sl_w = pw - 44, sl_h = 7;
    /* Track */
    path_round_rect(cr, sl_x, sl_y, sl_w, sl_h, sl_h / 2.0);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.18 * reveal);
    cairo_fill(cr);
    /* Fill (65 %) */
    path_round_rect(cr, sl_x, sl_y, (int)(sl_w * 0.65), sl_h, sl_h / 2.0);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.72 * reveal);
    cairo_fill(cr);

    /* ── Volume bar ───────────────────────────────────────────── */
    int vl_y = sl_y + 22;
    path_round_rect(cr, sl_x, vl_y, sl_w, sl_h, sl_h / 2.0);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.18 * reveal);
    cairo_fill(cr);
    path_round_rect(cr, sl_x, vl_y, (int)(sl_w * 0.45), sl_h, sl_h / 2.0);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.72 * reveal);
    cairo_fill(cr);

    cairo_destroy(cr);

    np->node = wlr_scene_buffer_create(np->tree, &np->buf->base);

    /* Position: centred, slide down from behind top edge */
    int px = (ow - pw) / 2;
    int py = (int)(-(ph + 20) * (1.0f - reveal)) + 8;
    if (py < 8) py = 8;
    wlr_scene_node_set_position(&np->node->node, px, py);
    wlr_scene_node_set_enabled(&np->tree->node, true);
}

static int np_anim_timer(void *data) {
    struct pm_notif_panel *np = data;
    float v = pm_anim_tick(&np->anim);
    np_render(np, v);
    if (np->anim.running)
        wl_event_source_timer_update(np->timer, PM_FRAME_MS);
    return 0;
}

static int np_dismiss_timer(void *data) {
    pm_notif_panel_hide((struct pm_notif_panel *)data);
    return 0;
}

struct pm_notif_panel *pm_notif_panel_create(struct pm_server *s) {
    struct pm_notif_panel *np = calloc(1, sizeof(*np));
    if (!np) return NULL;
    np->server = s;
    np->tree   = wlr_scene_tree_create(s->layer_overlay);
    wlr_scene_node_set_enabled(&np->tree->node, false);
    np->timer         = wl_event_loop_add_timer(s->event_loop, np_anim_timer, np);
    np->dismiss_timer = wl_event_loop_add_timer(s->event_loop, np_dismiss_timer, np);
    return np;
}

void pm_notif_panel_destroy(struct pm_notif_panel *np) {
    if (!np) return;
    if (np->timer)         wl_event_source_remove(np->timer);
    if (np->dismiss_timer) wl_event_source_remove(np->dismiss_timer);
    if (np->node) wlr_scene_node_destroy(&np->node->node);
    if (np->buf)  pm_cairo_buf_destroy(np->buf);
    wlr_scene_node_destroy(&np->tree->node);
    free(np);
}

void pm_notif_panel_reposition(struct pm_notif_panel *np, int ow, int oh) {
    np->out_w = ow;
    np->out_h = oh;
    wlr_scene_node_set_position(&np->tree->node, 0, 0);
}

void pm_notif_panel_show(struct pm_notif_panel *np) {
    pm_anim_start(&np->anim, 1.0f, PM_DUR_NOTIF_MS);
    wl_event_source_timer_update(np->timer, 1);
    /* Auto-dismiss after 8 s */
    wl_event_source_timer_update(np->dismiss_timer, 8000);
}

void pm_notif_panel_hide(struct pm_notif_panel *np) {
    pm_anim_start(&np->anim, 0.0f, PM_DUR_NOTIF_MS);
    wl_event_source_timer_update(np->timer, 1);
}

/* ================================================================== *
 *  SECTION 7 — Shell container                                      *
 * ================================================================== */

struct pm_shell *pm_shell_create(struct pm_server *s) {
    struct pm_shell *sh = calloc(1, sizeof(*sh));
    if (!sh) return NULL;

    sh->taskbar = pm_taskbar_create(s);
    if (!sh->taskbar) goto err;

    sh->homebar = pm_homebar_create(s);
    if (!sh->homebar) goto err;

    sh->overview = pm_overview_create(s);
    if (!sh->overview) goto err;

    sh->notif = pm_notif_panel_create(s);
    if (!sh->notif) goto err;

    return sh;
err:
    pm_shell_destroy(sh);
    return NULL;
}

void pm_shell_destroy(struct pm_shell *sh) {
    if (!sh) return;
    pm_taskbar_destroy(sh->taskbar);
    pm_homebar_destroy(sh->homebar);
    pm_overview_destroy(sh->overview);
    pm_notif_panel_destroy(sh->notif);
    free(sh);
}

void pm_shell_on_output_resize(struct pm_shell *sh, int ow, int oh) {
    pm_taskbar_reposition(sh->taskbar, ow, oh);
    pm_homebar_reposition(sh->homebar, ow, oh);
    pm_notif_panel_reposition(sh->notif, ow, oh);

    if (sh->overview->active) {
        sh->overview->out_w = ow;
        sh->overview->out_h = oh;
        pm_overview_rebuild(sh->overview);
    }
}

void pm_shell_view_mapped(struct pm_shell *sh, struct pm_view *v) {
    (void)v;
    pm_taskbar_render(sh->taskbar);
    if (sh->overview->active) pm_overview_rebuild(sh->overview);
}

void pm_shell_view_unmapped(struct pm_shell *sh, struct pm_view *v) {
    (void)v;
    pm_taskbar_render(sh->taskbar);
    if (sh->overview->active) pm_overview_rebuild(sh->overview);
}
