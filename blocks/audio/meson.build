inc_dirs = include_directories('include')

# Interface library
gr4_audio_blocks = library(
  'gr4-audio-blocks',
  sources: [],
  include_directories: inc_dirs,
  dependencies: [gr4_dep],
  install: true  # Set true if you plan to install it
)

# Expose the dependency for subprojects or installation
gr4_audio_blocks_dep = declare_dependency(
  link_with: gr4_audio_blocks,
  include_directories: inc_dirs
)

# Add subdirectory for tests
subdir('test')
subdir('examples')