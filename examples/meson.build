inc_dirs = []
#     '../blocks/analog/include',
#     '../blocks/audio/include',
#     '../blocks/basic/include',
#     '../blocks/zeromq/include',
# ]

zmq_dep = dependency('cppzmq', required: true)
rtaudio_dep = dependency('rtaudio', required: true)

deps = [
  gr4_dep,
  gr4_blocklib_dep,
  gr4_audio_blocks_dep,
  gr4_pfb_blocks_dep,
  gr4_analog_blocks_dep,
  gr4_basic_blocks_dep,
  gr4_zmq_blocks_dep,
  gr4_soapysdr_blocks_dep,
  zmq_dep,
  cli11_dep,
  rtaudio_dep,
]
if soapysdr_dep.found()
  deps += soapysdr_dep
endif

imgui_proj = subproject('imgui', default_options: ['opengl=enabled', 'glfw=enabled'])
imgui_dep = imgui_proj.get_variable('imgui_dep')

implot_proj = subproject('implot')
implot_dep = implot_proj.get_variable('implot_dep')
glfw_dep = dependency('glfw3', required: true, method: 'pkg-config')
opengl_dep = dependency('opengl', required: true)

exe = executable('fm_demodulator', 'fm_demodulator.cpp',
  install : true,
  include_directories: inc_dirs,
  dependencies: deps)

exe = executable('fm_demodulator_cli', 'fm_demodulator_cli.cpp',
  install : true,
  include_directories: inc_dirs,
  dependencies: deps + [dependency('ncurses', required: true)])

exe = executable('fm_demodulator_imgui', 'fm_demodulator_imgui.cpp',
  install : true,
  include_directories: inc_dirs,
  dependencies: deps + [imgui_dep, implot_dep, glfw_dep, opengl_dep])
