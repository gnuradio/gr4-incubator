inc_dirs = []
#     '../blocks/analog/include',
#     '../blocks/audio/include',
#     '../blocks/basic/include',
#     '../blocks/zeromq/include',
# ]

pmt_conv_dep = dependency('pmt_converter', required: true)
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
  pmt_conv_dep,
  zmq_dep,
  cli11_dep,
  rtaudio_dep,
]
if soapysdr_dep.found()
  deps += soapysdr_dep
endif

exe = executable('fm_demodulator', 'fm_demodulator.cpp',
  install : true,
  include_directories: inc_dirs,
  dependencies: deps)
