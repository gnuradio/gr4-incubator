inc_dirs = include_directories('include')

libname = 'gr4-basic-blocks'

# Interface library
gr4_basic_blocks = library(
  libname,
  sources: [],
  include_directories: inc_dirs,
  dependencies: [gr4_dep],
  install: true  # Set true if you plan to install it
)

# Expose the dependency for subprojects or installation
gr4_basic_blocks_dep = declare_dependency(
  link_with: gr4_basic_blocks,
  include_directories: inc_dirs
)

# Add subdirectory for tests
# gen_plugin_meson = 'plugin/meson.build'
# if not files(gen_plugin_meson).length() > 0
#   error('You must run bootstrap_plugins.sh before meson setup!')
# endif

subdir('plugin')
subdir('test')


