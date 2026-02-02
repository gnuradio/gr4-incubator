zmq_dep = dependency('cppzmq', required: true)

inc_dirs = include_directories('include')

install_subdir(
  'include/gnuradio-4.0/zeromq',
  install_dir: join_paths(get_option('includedir'), 'gnuradio-4.0')
)

deps = [gr4_dep, zmq_dep, pmt_converter_dep]
# # Interface library
# gr4_zmq_blocks = library(
#   'gr4-zmq-blocks',
#   sources: [],
#   include_directories: inc_dirs,
#   dependencies: deps,
#   install: true  # Set true if you plan to install it
# )

# Expose the dependency for subprojects or installation
gr4_zmq_blocks_dep = declare_dependency(
  include_directories: inc_dirs,
  dependencies: deps
)

if get_option('enable_plugins')
subdir('plugin')
endif
subdir('examples')
if get_option('enable_testing')
subdir('test')
endif

pkgconfig = import('pkgconfig')
pkgconfig.generate(
  name : 'gnuradio4-zeromq-headers',
  description : 'GNU Radio 4.0 ZeroMQ Headers',
  version : meson.project_version(),
  subdirs: 'gnuradio-4.0/zeromq',
)

#TODO - build into plugin generator
if get_option('enable_plugins')
pkgconfig.generate(
  Gr4ZeromqBlocks_shared,
  name : 'gnuradio4-zeromq',
  description : 'GNU Radio 4.0 ZeroMQ Shared Blocks',
  version : meson.project_version(),
  subdirs: 'gnuradio-4.0/zeromq',
)
endif
