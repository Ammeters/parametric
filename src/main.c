#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wlr/util/log.h>
#include "server.h"
#include "config.h"

static void usage(const char *prog) {
    fprintf(stderr,
        "Parametric Wayland Compositor v0.1\n"
        "\n"
        "Usage: %s [options]\n"
        "  -s CMD   Run CMD on startup (e.g. -s 'foot')\n"
        "  -m       Mobile mode (no CSD decorations by default)\n"
        "  -h       Show this help\n",
        prog);
}

int main(int argc, char *argv[]) {
    wlr_log_init(WLR_DEBUG, NULL);

    const char *startup_cmd = NULL;
    bool mobile_mode = false;
    int opt;

    while ((opt = getopt(argc, argv, "s:mh")) != -1) {
        switch (opt) {
        case 's': startup_cmd = optarg; break;
        case 'm': mobile_mode = true;   break;
        case 'h': usage(argv[0]); return EXIT_SUCCESS;
        default:  usage(argv[0]); return EXIT_FAILURE;
        }
    }

    struct pm_server server = {0};

    server.config = parametric_config_create();
    if (!server.config) {
        wlr_log(WLR_ERROR, "Failed to create config");
        return EXIT_FAILURE;
    }

    server.config->desktop_mode = !mobile_mode;

    if (startup_cmd) {
        snprintf(server.config->startup_cmd,
                 sizeof(server.config->startup_cmd), "%s", startup_cmd);
    }

    if (!pm_server_init(&server)) {
        wlr_log(WLR_ERROR, "Server init failed");
        parametric_config_destroy(server.config);
        return EXIT_FAILURE;
    }

    pm_server_run(&server, startup_cmd);
    pm_server_destroy(&server);
    return EXIT_SUCCESS;
}
