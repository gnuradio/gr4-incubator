inc_dirs = include_directories('include')

install_subdir(
  'include/gnuradio-4.0/soapysdr',
  install_dir: join_paths(get_option('includedir'), 'gnuradio-4.0')
)

deps = [gr4_dep, soapysdr_dep]

gr4_soapysdr_blocks_dep = declare_dependency(
  include_directories: inc_dirs,
  dependencies: deps
)

if get_option('enable_examples')
subdir('apps')
endif
