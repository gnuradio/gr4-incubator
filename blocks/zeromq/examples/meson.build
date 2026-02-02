inc_dirs = '../include'

exe = executable('zmq_loopback', 'zmq_loopback.cpp',
  install : true,
  include_directories: inc_dirs,
  dependencies: [zmq_dep, gr4_dep, gr4_blocklib_dep, gr4_basic_blocks_dep, pmt_converter_dep])
