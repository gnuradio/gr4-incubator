inc_dirs = include_directories('include')

gr4_sigmf_blocks = library(
  'gr4-sigmf-blocks',
  sources: [],
  include_directories: inc_dirs,
  dependencies: [gr4_dep, nlohmann_json_dep],
  install: true
)

gr4_sigmf_blocks_dep = declare_dependency(
  link_with: gr4_sigmf_blocks,
  include_directories: inc_dirs
)

install_subdir(
  'include/gnuradio-4.0/sigmf',
  install_dir: join_paths(get_option('includedir'), 'gnuradio-4.0')
)

if get_option('enable_testing')
  subdir('test')
endif

pkgconfig = import('pkgconfig')
pkgconfig.generate(
  name : 'gnuradio4-sigmf-headers',
  description : 'GNU Radio 4.0 SigMF Headers',
  version : meson.project_version(),
  subdirs: 'gnuradio-4.0/sigmf',
)

