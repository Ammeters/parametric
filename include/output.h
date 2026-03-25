#pragma once
#include <wayland-server-core.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/box.h>

struct pm_server;
struct pm_cairo_buf;

struct pm_output {
    struct wl_list           link;
    struct pm_server        *server;
    struct wlr_output       *wlr_output;
    struct wlr_scene_output *scene_output;

    /* Gradient background buffer (per-output) */
    struct pm_cairo_buf     *bg_buf;
    struct wlr_scene_buffer *bg_node;

    struct wl_listener l_frame;
    struct wl_listener l_request_state;
    struct wl_listener l_destroy;
};

void pm_output_init(struct pm_server *s, struct wlr_output *wlr_output);

/* Usable area: full output minus taskbar strip */
void pm_output_usable_area(struct pm_output *o, struct wlr_box *box_out);

/* Repaint the gradient background (call on init and on output resize) */
void pm_output_repaint_bg(struct pm_output *o);
