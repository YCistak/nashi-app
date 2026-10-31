project('nashi-wheel', 'c',
  version : '0.1.0',
  license : 'MIT',
  default_options : ['c_std=gnu11', 'warning_level=2', 'werror=false'])

gtk = dependency('gtk+-3.0')
layer_shell = dependency('gtk-layer-shell-0')
gio_unix = dependency('gio-unix-2.0')
json = dependency('json-glib-1.0')
m = meson.get_compiler('c').find_library('m', required : false)

wheel_lib = static_library('wheel-core', ['src/draw.c', 'src/uris.c', 'src/hypr.c'],
  dependencies : [gtk, gio_unix, json, m])
wheel_dep = declare_dependency(link_with : wheel_lib,
  include_directories : include_directories('src'))

wheel_exe = executable('nashi-wheel',
  ['src/main.c', 'src/surface.c', 'src/trigger.c'],
  dependencies : [gtk, layer_shell, gio_unix, json, m, wheel_dep],
  install : true)

geometry_test = executable('test-geometry', 'tests/test_geometry.c',
  dependencies : [gtk, m, wheel_dep])
test('geometry', geometry_test)

# Headless smoke test: renders every mode to a PNG without any display.
foreach mode : ['convert', 'tools', 'actions']
  test('render-' + mode, find_program('sh'),
    args : ['-c', '"$0" --render-png "$1" --mode ' + mode + ' --highlight 1 --theme dark && test -s "$1"',
            wheel_exe.full_path(), meson.current_build_dir() / ('smoke-' + mode + '.png')],
    depends : wheel_exe,
    env : {'WAYLAND_DISPLAY': '', 'DISPLAY': ''})
endforeach

# Pixel checks: gold petal stays inside the ring, ring contrasts in both themes.
render_test = executable('test-render', 'tests/test_render.c',
  dependencies : [gtk, m, wheel_dep])
test('render-bounds', render_test,
  env : {'WAYLAND_DISPLAY': '', 'DISPLAY': ''})

# URI list parsing and JSON output.
uris_test = executable('test-uris', 'tests/test_uris.c',
  dependencies : [gtk, m, wheel_dep])
test('uris', uris_test)

# Hyprland trigger: socket2 parsing, version gate, Lua hook safety rules.
hypr_test = executable('test-hypr', 'tests/test_hypr.c',
  dependencies : [gtk, gio_unix, json, m, wheel_dep])
test('hypr', hypr_test)

# Outside Hyprland the trigger must do nothing and exit 3.
test('hypr-trigger-no-session', find_program('sh'),
  args : ['-c', '"$0" --hypr-trigger; test $? -eq 3', wheel_exe.full_path()],
  depends : wheel_exe,
  env : {'HYPRLAND_INSTANCE_SIGNATURE': '', 'WAYLAND_DISPLAY': '', 'DISPLAY': ''})
