# Mesh emulation on the official Eglantine tree (Pascal GPUs)

This branch carries the compute-based task/mesh emulation from the Lichii fork into the official
`nx2-experimental` base, so titles that draw with NVN task/mesh programs keep their geometry on
devices without `VK_EXT_mesh_shader` (GTX 10 series, MoltenVK).

Upstream work: `b7ff181f20` "vulkan, shader: draw task/mesh pipelines through compute without
VK_EXT_mesh_shader (#569)" by Zortos (Lichii). Same GPL-3.0-or-later license.

## Why

On a GTX 1060 the official build drops ~27 draw pairs per frame with
`Draw skipped (... miss=creation-failed)`, including effect-sized targets (480x270, 960x540) and
full-screen ones. In *Ocarina of Time* the visible symptom is a quadrant of missing fog that follows
the camera. The skipped draws are task/mesh programs: without `VK_EXT_mesh_shader` the pipeline
cannot be created and the draw is discarded.

## What the emulation does

- A mesh program is emitted as a **compute shader** (`RuntimeInfo::mesh_as_compute`) that writes one
  block per workgroup to a storage buffer: `max_vertices * vertex_lines` words of vertex output
  followed by `max_primitives * 3` vertex indices (a degenerate triangle past the count).
- A generated **vertex shader** reads those blocks and draws
  `groups * max_primitives * 3` vertices as a triangle list, with the mesh program's vertex outputs.
- With a task program, the task runs as compute writing its payload, a **distribution pass** sums the
  mesh workgroup counts, maps each mesh workgroup to its task and writes the indirect dispatch and
  draw arguments, then the mesh compute shader is dispatched indirectly.
- Draws are recorded outside the render pass, so a draw with mesh emulation breaks the pass.

## Port notes (what differs from the Lichii tree)

The cherry-pick was done against this tree's newer upstream, so a few things were adapted:

- `emit_pools` / `DeferredStages` / `EmitShaderModule` / `reference_spirv` (Lichii's deferred shader
  emission) do not exist here: mesh emission uses the regular `EmitSPIRV` + `BuildShader` path.
- `PipelineBuild` (Lichii's async build handle) does not exist here: the draw waits with
  `RunBuild()` + `build_condvar`, as the rest of this tree does.
- The Intel payload-word guard in `EmitMeshPrologue` stays inside `if (!as_compute)`.
- `Device` gained `GetMaxPerStageDescriptorStorageBuffers` for the emulation's limit check.
- Lichii-only diagnostics (`DkbFrameTrace`, `TraceRenderState`, the mesh survey) were dropped.

## Build

```
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -B build/mesh -S .
ninja -C build/mesh -j10 yuzu
```

## Verifying

In the log, look for `Mesh emulation draws: N (M with no groups), G groups` and for the absence of
`Draw skipped (... 480x270 ...)` / `960x540` lines. Visually, the OoT fog quadrant is filled in.
