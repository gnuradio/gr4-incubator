inc_dirs = include_directories('include')

install_subdir(
  'include/gnuradio-4.0/analog',
  install_dir: join_paths(get_option('includedir'), 'gnuradio-4.0')
)

libname = 'gr4-analog-blocks'

deps = [gr4_dep]
# Interface library
# gr4_analog_blocks = library(
#   libname,
#   sources: [],
#   include_directories: inc_dirs,
#   dependencies: deps,
#   install: true  # Set true if you plan to install it
# )

# Expose the dependency for subprojects or installation
gr4_analog_blocks_dep = declare_dependency(
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
  name : 'gnuradio4-analog-headers',
  description : 'GNU Radio 4.0 analog Headers',
  version : meson.project_version(),
  subdirs: 'gnuradio-4.0/analog',
)

#TODO - build into plugin generator
pkgconfig.generate(
  Gr4AnalogBlocks_shared,
  name : 'gnuradio4-analog',
  description : 'GNU Radio 4.0 analog Shared Blocks',
  version : meson.project_version(),
  subdirs: 'gnuradio-4.0/analog',
)

