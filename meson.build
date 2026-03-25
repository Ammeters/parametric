project('parametric', 'c',
  version: '0.1.0',
  license: 'MIT',
  meson_version: '>= 0.58.0',
  default_options: [
    'c_std=c11',
    'warning_level=2',
  ],
)

add_project_arguments([
  '-DWLR_USE_UNSTABLE',
  '-D_POSIX_C_SOURCE=200809L',
  '-D_GNU_SOURCE',
  '-Wno-unused-parameter',
], language: 'c')

cc = meson.get_compiler('c')

# ── Core dependencies ────────────────────────────────────────────
wlroots      = dependency('wlroots-0.18',     version: '>= 0.18.0')
wayland_srv  = dependency('wayland-server',   version: '>= 1.21')
wayland_prot = dependency('wayland-protocols',version: '>= 1.32')
xkbcommon    = dependency('xkbcommon')
cairo        = dependency('cairo')
pangocairo   = dependency('pangocairo')
pixman       = dependency('pixman-1')
libm         = cc.find_library('m', required: true)

# ── Wayland protocol code generation ────────────────────────────
wayland_scanner_dep  = dependency('wayland-scanner', native: true)
wayland_scanner_prog = find_program(
  wayland_scanner_dep.get_variable('wayland_scanner'),
  native: true,
)
wl_protocol_dir = wayland_prot.get_variable('pkgdatadir')

protocols = [
  [wl_protocol_dir + '/stable/xdg-shell/xdg-shell.xml',
   'xdg-shell'],
  [wl_protocol_dir + '/unstable/xdg-output/xdg-output-unstable-v1.xml',
   'xdg-output-unstable-v1'],
]

proto_src = []
foreach proto : protocols
  xml  = proto[0]
  name = proto[1]
  proto_src += custom_target(name + '_c',
    input:   xml,
    output:  '@BASENAME@-protocol.c',
    command: [wayland_scanner_prog, 'private-code', '@INPUT@', '@OUTPUT@'],
  )
endforeach

# ── Sources ──────────────────────────────────────────────────────
parametric_sources = files(
  'src/main.c',
  'src/server.c',
  'src/output.c',
  'src/view.c',
  'src/input.c',
  'src/shell.c',
  'src/config.c',
  'src/animate.c',
)

executable('parametric',
  parametric_sources + proto_src,
  include_directories: include_directories('include'),
  dependencies: [
    wlroots,
    wayland_srv,
    xkbcommon,
    cairo,
    pangocairo,
    pixman,
    libm,
  ],
  install: true,
)
