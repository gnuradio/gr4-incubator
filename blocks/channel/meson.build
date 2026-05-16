inc_dirs = include_directories('include')

install_subdir(
  'include/gnuradio-4.0/zeromq',
  install_dir: join_paths(get_option('includedir'), 'gnuradio-4.0')
)

libname = 'gr4-channel-blocks'

deps = [gr4_dep]
# Interface library
# gr4_channel_blocks = library(
#   libname,
#   sources: [],
#   include_directories: inc_dirs,
#   dependencies: deps,
#   install: true  # Set true if you plan to install it
# )

# Expose the dependency for subprojects or installation
gr4_channel_blocks_dep = declare_dependency(
  include_directories: inc_dirs
)

# Add subdirectory for tests
# gen_plugin_meson = 'plugin/meson.build'
# if not files(gen_plugin_meson).length() > 0
#   error('You must run bootstrap_plugins.sh before meson setup!')
# endif

if get_option('enable_plugins')
subdir('plugin')
endif
# subdir('examples')
if get_option('enable_testing')
subdir('test')
endif


pkgconfig = import('pkgconfig')
pkgconfig.generate(
  name : 'gnuradio4-channel-headers',
  description : 'GNU Radio 4.0 Channel Headers',
  version : meson.project_version(),
  subdirs: 'gnuradio-4.0/channel',
)

#TODO - build into plugin generator
if get_option('enable_plugins')
pkgconfig.generate(
  Gr4ChannelBlocks_shared,
  name : 'gnuradio4-channel',
  description : 'GNU Radio 4.0 Channel Shared Blocks',
  version : meson.project_version(),
  subdirs: 'gnuradio-4.0/channel',
)
endif

