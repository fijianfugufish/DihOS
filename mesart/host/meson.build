# Host-only tool injected into the private WSL Mesa snapshot by
# tools/setup_mesart_mesa_host.sh.  It deliberately consumes Mesa's internal
# compiler libraries as a build-time tool; no host runtime is ever copied into
# DihOS or admitted by the kernel.
executable(
  'mesart_glsl_to_ir3',
  'mesart_glsl_to_ir3.cpp',
  include_directories: [
    inc_include,
    inc_src,
    inc_mesa,
    inc_gallium,
    inc_gallium_aux,
    inc_freedreno,
    include_directories('.'),
  ],
  link_with: [
    libglsl_standalone,
    libfreedreno_ir3,
  ],
  dependencies: [
    dep_clock,
    dep_thread,
    idep_getopt,
    idep_mesautil,
    idep_nir,
  ],
  build_by_default: false,
  install: false,
)
