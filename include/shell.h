#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <cairo.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_buffer.h>
#include "animate.h"

struct pm_server;
struct pm_view;

/* ================================================================== *
 *  pm_cairo_buf — shm-backed wlr_buffer drawable with Cairo          *
 * ================================================================== */
struct pm_cairo_buf {
    struct wlr_buffer base;   /* must be first */
    int    width, height, stride;
    int    fd;
    void  *data;
    size_t size;
};

struct pm_cairo_buf *pm_cairo_buf_create(int w, int h);
void                 pm_cairo_buf_destroy(struct pm_cairo_buf *buf);
cairo_t             *pm_cairo_buf_cr(struct pm_cairo_buf *buf);  /* caller must cairo_destroy */

/* ================================================================== *
 *  Taskbar                                                           *
 * ================================================================== */
#define TB_H_NORMAL    72    /* 1-row height (px)   */
#define TB_H_OVERVIEW  148   /* 2-row height (px)   */
#define TB_MARGIN      14    /* gap from screen edge */
#define TB_ICON_SZ     48
#define TB_ICON_GAP    12
#define TB_CORNER_R    18

struct pm_taskbar {
    struct pm_server       *server;
    struct wlr_scene_tree  *tree;
    struct pm_cairo_buf    *buf;
    struct wlr_scene_buffer *node;

    int   w, h;           /* rendered dimensions  */
    int   x, y;           /* position on output   */
    int   out_w, out_h;   /* last known output size */
    bool  expanded;       /* overview: 2-row mode */
    bool  always_visible; /* dock style           */
};

struct pm_taskbar *pm_taskbar_create(struct pm_server *s);
void               pm_taskbar_destroy(struct pm_taskbar *tb);
void               pm_taskbar_reposition(struct pm_taskbar *tb, int ow, int oh);
void               pm_taskbar_render(struct pm_taskbar *tb);
void               pm_taskbar_set_expanded(struct pm_taskbar *tb, bool exp);

/* Returns true if (lx,ly) in layout coords was a taskbar icon click */
bool               pm_taskbar_handle_click(struct pm_taskbar *tb, double lx, double ly);

/* ================================================================== *
 *  Homebar                                                           *
 * ================================================================== */
#define HB_H           5     /* pill height (px)         */
#define HB_W_FRAC      0.28f /* fraction of screen width */
#define HB_MARGIN_BOT  10

struct pm_homebar {
    struct pm_server       *server;
    struct wlr_scene_tree  *tree;
    struct pm_cairo_buf    *buf;
    struct wlr_scene_buffer *node;

    int   out_w, out_h;
    struct pm_anim anim;
    struct wl_event_source *timer;
};

struct pm_homebar *pm_homebar_create(struct pm_server *s);
void               pm_homebar_destroy(struct pm_homebar *hb);
void               pm_homebar_reposition(struct pm_homebar *hb, int ow, int oh);
void               pm_homebar_show(struct pm_homebar *hb);
void               pm_homebar_hide(struct pm_homebar *hb);

/* ================================================================== *
 *  Overview mode                                                     *
 * ================================================================== */
#define OV_CARD_W    260
#define OV_CARD_H    170
#define OV_CARD_GAP   20
#define OV_CARD_R     14
#define OV_CLOSE_HIT  28   /* px: top-right hit area for close btn  */

struct pm_overview {
    struct pm_server       *server;
    bool                    active;

    struct wlr_scene_rect  *dim_rect;    /* fullscreen dark overlay   */
    struct wlr_scene_tree  *cards_tree;  /* parent of all card trees  */

    int   out_w, out_h;
    struct pm_anim anim;
    struct wl_event_source *timer;
};

struct pm_overview *pm_overview_create(struct pm_server *s);
void                pm_overview_destroy(struct pm_overview *ov);
void                pm_overview_show(struct pm_overview *ov);
void                pm_overview_hide(struct pm_overview *ov);
void                pm_overview_toggle(struct pm_overview *ov);
void                pm_overview_rebuild(struct pm_overview *ov);
bool                pm_overview_handle_click(struct pm_overview *ov, double lx, double ly);

/* ================================================================== *
 *  Notification / control panel (swipe down from top)               *
 * ================================================================== */
struct pm_notif_panel {
    struct pm_server       *server;
    struct wlr_scene_tree  *tree;
    struct pm_cairo_buf    *buf;
    struct wlr_scene_buffer *node;

    int   out_w, out_h;
    struct pm_anim anim;
    struct wl_event_source *timer;
    struct wl_event_source *dismiss_timer;
};

struct pm_notif_panel *pm_notif_panel_create(struct pm_server *s);
void                   pm_notif_panel_destroy(struct pm_notif_panel *np);
void                   pm_notif_panel_show(struct pm_notif_panel *np);
void                   pm_notif_panel_hide(struct pm_notif_panel *np);
void                   pm_notif_panel_reposition(struct pm_notif_panel *np, int ow, int oh);

/* ================================================================== *
 *  Shell — container for all UI widgets                             *
 * ================================================================== */
struct pm_shell {
    struct pm_taskbar     *taskbar;
    struct pm_homebar     *homebar;
    struct pm_overview    *overview;
    struct pm_notif_panel *notif;
};

struct pm_shell *pm_shell_create(struct pm_server *s);
void             pm_shell_destroy(struct pm_shell *sh);
void             pm_shell_on_output_resize(struct pm_shell *sh, int ow, int oh);
void             pm_shell_view_mapped(struct pm_shell *sh, struct pm_view *v);
void             pm_shell_view_unmapped(struct pm_shell *sh, struct pm_view *v);
