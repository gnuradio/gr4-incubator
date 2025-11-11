inc_dirs = include_directories('include')

install_subdir(
  'include/gnuradio-4.0/pfb',
  install_dir: join_paths(get_option('includedir'), 'gnuradio-4.0')
)

libname = 'gr4-pfb-blocks'

deps = [gr4_dep]

# Expose the dependency for subprojects or installation
gr4_pfb_blocks_dep = declare_dependency(
  include_directories: inc_dirs
)

if get_option('enable_plugins')
subdir('plugin')
endif

if get_option('enable_testing')
subdir('test')
endif

pkgconfig = import('pkgconfig')
pkgconfig.generate(
  name : 'gnuradio4-pfb-headers',
  description : 'GNU Radio 4.0 PFB Headers',
  version : meson.project_version(),
  subdirs: 'gnuradio-4.0/pfb',
)

if get_option('enable_plugins')
pkgconfig.generate(
  Gr4PfbBlocks_shared,
  name : 'gnuradio4-pfb',
  description : 'GNU Radio 4.0 PFB Shared Blocks',
  version : meson.project_version(),
  subdirs: 'gnuradio-4.0/pfb',
)
endif
