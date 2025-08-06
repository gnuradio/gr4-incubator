zmq_dep = dependency('cppzmq', required: true)

inc_dirs = include_directories('include')

# Interface library
gr4_zmq_blocks = library(
  'gr4-zmq-blocks',
  sources: [],
  include_directories: inc_dirs,
  dependencies: [gr4_dep, pmt_conv_dep],
  install: true  # Set true if you plan to install it
)

# Expose the dependency for subprojects or installation
gr4_zmq_blocks_dep = declare_dependency(
  link_with: gr4_zmq_blocks,
  include_directories: inc_dirs
)

subdir('examples')
subdir('test')