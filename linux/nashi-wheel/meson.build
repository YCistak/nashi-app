project('nashi-wheel', 'c',
  version : '0.1.0',
  license : 'MIT',
  default_options : ['c_std=gnu11', 'warning_level=2', 'werror=false'])

gtk = dependency('gtk+-3.0')
layer_shell = dependency('gtk-layer-shell-0')
m = meson.get_compiler('c').find_library('m', required : false)

wheel_lib = static_library('wheel-core', 'src/draw.c',
  dependencies : [gtk, m])
wheel_dep = declare_dependency(link_with : wheel_lib,
  include_directories : include_directories('src'))

executable('nashi-wheel',
  ['src/main.c', 'src/surface.c'],
  dependencies : [gtk, layer_shell, m, wheel_dep],
  install : true)

geometry_test = executable('test-geometry', 'tests/test_geometry.c',
  dependencies : [gtk, m, wheel_dep])
test('geometry', geometry_test)
