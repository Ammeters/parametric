# Parametric

A Wayland compositor designed for both desktop and mobile use. Built on [wlroots 0.18](https://gitlab.freedesktop.org/wlroots/wlroots) with a unified shell designed around the principle that desktop and mobile should feel coherent rather than separate.

---

## Design Philosophy

- **One shell, two modes.** Desktop and mobile share the same taskbar, homebar, and overview. The primary difference is how you invoke them (mouse vs touch).
- **Fluid, not garish.** Frosted-glass navy UI, diagonal gradient backgrounds, subtle animations.
- **Maximise by default.** Applications are full-screen unless explicitly tiled. The taskbar floats at the bottom and hides behind running apps.

---

## Features

| Feature | Notes |
|---|---|
| wlroots 0.18 scene-graph compositor | Full XDG shell, layer-shell, popups |
| Diagonal gradient background | `#07527F` top-left → `#00265C` bottom-right, with vignette |
| Floating frosted-glass taskbar | Floats above bottom edge with gap; expands to 2 rows in overview |
| Always-visible dock mode | Taskbar spans full width, bottom corners flush |
| Homebar (white pill) | Appears on mouse-near-bottom; animated fade-in/out |
| Overview mode | Card grid with app icons, titles, close buttons; slide-in animation |
| Notification/Control panel | Swipe-down or Super+H; shows clock, date, quick toggles, sliders |
| Compositor-drawn decorations | Close (red) + minimise (amber) buttons; hidden in mobile mode |
| Animated transitions | Ease-out cubic across all UI elements |
| Super+W / Super key | Toggle overview |
| Super+Q / Alt+F4 | Close focused window |
| Super+M | Minimise focused window |
| Super+Tab | Cycle windows |
| Super+H | Toggle notification panel |
| Ctrl+Alt+T | Launch terminal (tries foot, alacritty, kitty, xterm) |
| Ctrl+Alt+BackSpace | Quit compositor |

---

## Dependencies

### Arch Linux
```bash
sudo pacman -S wlroots wayland wayland-protocols libxkbcommon \
               cairo pango meson ninja pkg-config
```

### Ubuntu 24.04 / Debian 13+
```bash
sudo apt install libwlroots-dev libwayland-dev wayland-protocols \
                 libxkbcommon-dev libcairo2-dev libpango1.0-dev \
                 meson ninja-build pkg-config gcc
```

### Fedora 40+
```bash
sudo dnf install wlroots-devel wayland-devel wayland-protocols-devel \
                 libxkbcommon-devel cairo-devel pango-devel \
                 meson ninja-build pkg-config gcc
```

> **Note:** Parametric targets wlroots **0.18** specifically. If your distro ships 0.17 you will need to build wlroots from source or use a container.

---

## Building

```bash
git clone <repo> parametric
cd parametric
chmod +x build.sh
./build.sh build
```

### Install system-wide
```bash
./build.sh install          # installs binary + wayland-sessions entry
```

### Run nested (inside an existing Wayland session, for development)
```bash
./build.sh run -s foot       # with a terminal on startup
./build.sh run -m            # mobile mode
./build.sh run               # bare compositor
```

### From a TTY (as the real session)
After `./build.sh install`, select **Parametric** from your display manager (GDM, SDDM, etc.), or run directly from a TTY:
```bash
parametric -s foot
```

---

## Project Structure

```
parametric/
├── meson.build            Build system
├── build.sh               Build/install/run helper
├── parametric.desktop     Wayland session entry
├── include/
│   ├── animate.h          Animation helpers (easing, pm_anim)
│   ├── config.h           Runtime configuration struct
│   ├── input.h            Keyboard, pointer, gesture tracker
│   ├── output.h           Monitor management
│   ├── server.h           Core compositor state + scene layers
│   ├── shell.h            Taskbar, homebar, overview, notif panel
│   └── view.h             XDG toplevel window wrapper
└── src/
    ├── main.c             Entry point (-s, -m flags)
    ├── server.c           Compositor core: globals, events, focus
    ├── output.c           Output init, gradient background, frame loop
    ├── view.c             Window lifecycle, decorations, maximize
    ├── input.c            Keyboard bindings, pointer routing
    ├── shell.c            All UI: Cairo buffers + every shell widget
    ├── config.c           Default values (colours, fonts, mode flags)
    └── animate.c          Easing functions, pm_anim tick
```

---

## Keyboard Reference

| Binding | Action |
|---|---|
| `Super` | Toggle overview |
| `Super+W` | Toggle overview |
| `Super+D` | Close overview (return to desktop) |
| `Super+Q` | Close focused window |
| `Super+M` | Minimise focused window |
| `Super+Tab` | Cycle to next window |
| `Super+H` | Toggle notification/control panel |
| `Alt+F4` | Close focused window |
| `Ctrl+Alt+T` | Launch terminal |
| `Ctrl+Alt+BackSpace` | Quit compositor |

---

## Roadmap

- [ ] Touch input routing (wlr_touch → gesture tracker)
- [ ] INI config file parser (`~/.config/parametric/config.ini`)
- [ ] Side-by-side window tiling (Super+Arrow)
- [ ] XWayland support (wlr_xwayland)
- [ ] Screen lock (ext-session-lock-v1)
- [ ] Integrated app launcher in overview
- [ ] Settings application (GTK4/libadwaita)
- [ ] Per-app volume controls in notification panel
- [ ] Multi-monitor taskbar instances

---

## Licence

MIT
