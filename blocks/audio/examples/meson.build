inc_dirs = '../include'

zmq_dep = dependency('cppzmq', required: true)
rtaudio_dep = dependency('rtaudio', required: true)

# exe = executable('zmq_loopback', 'zmq_loopback.cpp',
#   install : true,
#   include_directories: inc_dirs,
#   dependencies: [gr4_dep, gr4_blocklib_dep, pmt_conv_dep, zmq_dep, gr4_zeromq_headers])

exe = executable('audio_playback', 'audio_playback.cpp',
  install : true,
  include_directories: inc_dirs,
  dependencies: [gr4_dep, gr4_blocklib_dep, gr4_zmq_blocks_dep, zmq_dep, cli11_dep, rtaudio_dep])

