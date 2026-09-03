# Mesart host shader compiler

`mesart_glsl_to_ir3` is an offline WSL-only tool.  It uses the repository's
pinned Mesa source to link a GLSL ES 3.10 vertex/fragment pair and compile
each stage into an `MIR3` artifact plus one `MPIP` pipeline-reflection
artifact for the Adreno X1-85. `MIR3` contains A7xx IR3 code plus a small,
kernel-validated header; it is GPU instruction data, never CPU code. `MPIP`
contains only the corresponding Mesa-derived shader footprint, system-value
registers, and VPC varying layout—never a GPU address, register number, or
PM4 packet.
The fixed header also carries bounded compiler metadata (constant vec4
capacity, sampler count, UBO count, and stage IO).  The future kernel draw
broker uses those limits to validate number and texture bindings; it never
trusts a resource count supplied by an EL0 caller.

The tool is copied into the private `~/mesart-mesa-source` snapshot by
`tools/setup_mesart_mesa_host.sh` and built there with Meson.  DihOS never
loads the host executable.  Before an artifact can be used, it must be placed
in the Mesa runtime bundle, hashed into `manifest.msrt`, and authenticated by
the existing Mesart signing key.  On Windows, `build_mesart_stub.ps1` does
that staging and signing from host output paths supplied through
`-KernelShaderSourcePath` and `-KernelPipelineSourcePath`; it copies them to
`OS/MesaRuntime/shaders/` and `OS/MesaRuntime/pipelines/` respectively.

`triangle.*` is deliberately input-free: the vertex stage chooses the triangle
position from `gl_VertexID`, while the fragment stage writes one fixed purple
colour. `tint_triangle.*` declares ordinary numeric uniforms, and
`textured_triangle.*` adds a sampler and inter-stage UV varying. They are all
compiler inputs only. The DihOS draw broker is still responsible for pipeline
state, uniform/texture descriptor allocation, and draw submission; no
generated shader is admitted to execution merely by compiling it.

When the WSL host is available, build all three pairs without wiping Mesa:

```sh
bash tools/compile_mesart_triangle_tests.sh
```

Then, from PowerShell, stage exactly one signed test package at a time:

```powershell
.\tools\stage_mesart_triangle_test.ps1 -ProjectRoot . -Test purple
```

Use `tint` or `textured` for the later tests.  Keeping only one VS/FS pair in
the signed runtime at a time is intentional for this first draw broker: it
makes each visual result unambiguous and keeps its pipeline authority small.
