inc_dirs = include_directories('include')

install_subdir(
  'include/gnuradio-4.0/zeromq',
  install_dir: join_paths(get_option('includedir'), 'gnuradio-4.0')
)

libname = 'gr4-basic-blocks'

deps = [gr4_dep, pmt_conv_dep]
# Interface library
# gr4_basic_blocks = library(
#   libname,
#   sources: [],
#   include_directories: inc_dirs,
#   dependencies: deps,
#   install: true  # Set true if you plan to install it
# )

# Expose the dependency for subprojects or installation
gr4_basic_blocks_dep = declare_dependency(
  include_directories: inc_dirs
)

# Add subdirectory for tests
# gen_plugin_meson = 'plugin/meson.build'
# if not files(gen_plugin_meson).length() > 0
#   error('You must run bootstrap_plugins.sh before meson setup!')
# endif

subdir('plugin')
subdir('test')


pkgconfig = import('pkgconfig')
pkgconfig.generate(
  name : 'gnuradio4-basic-headers',
  description : 'GNU Radio 4.0 Basic Headers',
  version : meson.project_version(),
  subdirs: 'gnuradio-4.0/basic',
)

#TODO - build into plugin generator
pkgconfig.generate(
  Gr4BasicBlocks_shared,
  name : 'gnuradio4-basic',
  description : 'GNU Radio 4.0 Basic Shared Blocks',
  version : meson.project_version(),
  subdirs: 'gnuradio-4.0/basic',
)

