# IsometricPathTracer - Project Specifications

## 1) Project Summary

IsometricPathTracer is a real-time, GPU-driven isometric path tracing demo written in C++17 and OpenGL 4.1.  
The application renders a procedural voxel-like terrain composed of axis-aligned cubes, supports sparse procedurally generated emissive terrain cubes with elevation-driven intensity, procedural bridges synthesized from terrain analysis, applies temporal accumulation and A-Trous denoising, then displays the result with elevation-driven auto exposure, ACES tonemapping, and gamma correction.

Primary goals:

- Demonstrate progressive path tracing with a modern post-processing pipeline.
- Keep architecture simple and educational while supporting practical performance optimizations.
- Provide live controls for scene generation, camera setup, lighting, and denoiser tuning.

## 2) Scope

In scope:

- Procedural terrain generation on CPU.
- CPU-built SAH BVH acceleration structure uploaded to GPU.
- Fullscreen fragment-shader path tracing (1 spp per frame).
- Temporal accumulation across frames.
- Edge-aware A-Trous denoising using normal/depth guides.
- ImGui-based runtime controls.

Out of scope:

- Mesh/asset import pipeline.
- Advanced lighting models (MIS, sampled area lights, complex emissive-light transport).
- Distributed/network rendering.

## 3) Tech Stack and Dependencies

- Language: `C++17`
- Graphics API: `OpenGL 4.1 Core`
- Windowing/Input: `GLFW`
- OpenGL Loader: `GLAD`
- Math: `GLM`
- UI: `Dear ImGui` + GLFW/OpenGL3 backends
- Noise: `stb_perlin`
- Build system: `CMake` (minimum `3.20`)

Linked targets:

- `OpenGL::GL`
- `glfw`
- `glm::glm`
- local static `glad` target
- local static `imgui` target

## 4) Runtime and Platform Requirements

- Desktop-class GPU with OpenGL 4.1 support.
- macOS-compatible context creation is configured (`GLFW_OPENGL_FORWARD_COMPAT` on Apple).
- Shader files must be present at runtime under `shaders/` in the build directory (copied by CMake).

## 5) System Architecture

### Core modules

- `src/main.cpp`
  - Application lifecycle and render loop.
  - ImGui controls and change detection.
  - Derives target exposure from sun elevation and smooths display exposure over time.
  - Delays emissive activation after terrain edits, then fades emissive intensity in after a short inactivity window.
  - Calls generation, tracing, accumulation, denoise, and present passes in order.

- `src/Camera.h/.cpp`
  - Isometric-style camera parameterization (`azimuth`, `elevation`, `distance`, `orthoScale`).
  - Uploads camera data through UBO (`CameraUBO`) to shaders.

- `src/ProceduralGen.h/.cpp`
  - Runs a staged terrain generation pipeline:
    1. `sampleHeightField` — samples 2D Perlin noise into a `TerrainCell` grid (`active`, `height`).
    2. `emitTerrainColumns` — reads the grid and emits unit cubes with layer-based albedo and sparse emissive markers.
    3. `findBridgeSpans` (optional) — terrain analysis pass that scans cliff edges in cardinal directions, validates gap clearance, and selects up to `maxBridges` spans deterministically using seed-based hash ranking with overlap rejection.
    4. `emitBridgeCubes` (optional) — appends deck cubes and optional vertical support cubes for each selected span.
  - Builds a SAH-binned BVH over all cubes (terrain + bridges) on CPU.
  - Uploads cube data and BVH nodes to texture buffers for GPU traversal.
  - Bridge parameters are controlled by `BridgeParams` (`enabled`, `maxBridges`, `minSpan`, `maxSpan`, `width`, `supports`).

- `src/Renderer.h/.cpp`
  - Creates/owns GL programs, textures, and FBOs.
  - Executes rendering pipeline passes.
  - Handles resize and resource lifecycle.

### Shader stages

- `shaders/pathtrace.frag`
  - Primary path tracing pass.
  - BVH traversal for intersections and occlusion checks.
  - Runtime sun direction plus elevation-driven sky, ambient, and emissive lighting.
  - Outputs:
    - color (`RGBA16F`)
    - packed normal (`RG16F` via oct encoding)
    - depth (`R32F`)

- `shaders/accumulate.frag`
  - Temporal running average between current frame and history.

- `shaders/atrous.frag`
  - Multi-pass edge-aware A-Trous denoiser.
  - Uses color + normal + depth guides.

- `shaders/quad.frag`
  - Display pass with elevation-driven exposure, ACES filmic tonemapping, and gamma correction.

- `shaders/quad.vert`
  - Fullscreen triangle vertex shader.

## 6) Rendering Pipeline

Per-frame pipeline:

1. Optional scene regeneration (if generation parameters changed).
2. Path trace pass (`pathtrace.frag`) -> `colorTex_`, `normalTex_`, `depthTex_`.
3. Temporal accumulation (`accumulate.frag`) -> ping/pong `accum` texture.
4. A-Trous denoise (`atrous.frag`) -> ping/pong denoise textures.
5. Final present (`quad.frag`) with smoothed auto exposure -> swapchain framebuffer.

Notes:

- Rendering is progressive: each frame adds one noisy sample per pixel and converges over time.
- `frameIndex` is reset when scene, camera, lighting, or effective emissive intensity changes to avoid ghosted accumulation.
- Display exposure is derived from `sunElevation` on the CPU and adapts smoothly without adding a new reduction pass.
- Emissive terrain lighting is suppressed during active terrain edits, then fades in after a short debounce delay.
- Bridge cubes are standard `CubeData` axis-aligned boxes; no renderer or shader changes are required to support them.

## 7) Data Contracts

### CameraUBO

Contains inverse view-projection and camera basis data:

- `invViewProj`
- `camPos`, `camDir`, `camUp`, `camRight`
- `orthoExtents` (`halfW`, `halfH`, `near`, `far`)

### Geometry data (TBO)

Each primitive is `CubeData`:

- `bmin` (`vec4`, xyz used)
- `bmax` (`vec4`, xyz used)
- `albedo` (`vec4`, rgb used)
- `emission` (`vec4`, rgb used, zero for non-emissive cubes)

### BVH node data (TBO)

Packed in `BVHNodeGPU`:

- `d0`: `bmin.xyz`, `w` packs `rightChildIdx` (internal) or `primStart` (leaf)
- `d1`: `bmax.xyz`, `w` packs `primCount` (`0` = internal node)

## 8) User Controls (ImGui)

Scene generation:

- Grid Size
- Noise Scale
- Height Scale
- Seed

Camera:

- Azimuth
- Elevation
- Ortho Scale
- Distance

Lighting:

- Sun Azimuth
- Sun Elevation

Note: changing `Sun Elevation` also updates the automatic display exposure target.

Emissive:

- Emissive Enabled
- Emissive Density
- Emissive Intensity
- Debounce (s)
- Fade In (s)

Bridges:

- Bridges Enabled
- Max Bridges
- Min Span
- Max Span
- Bridge Width
- Supports

Denoiser:

- Sigma Color
- Sigma Normal
- Sigma Depth
- Passes

Stats:

- Frame index
- FPS
- Cube count

## 9) Build and Run

Example build:

```bash
cmake -S . -B build
cmake --build build
```

Run:

```bash
./build/IsometricPathTracer
```

## 10) Performance Characteristics

- Acceleration: SAH BVH traversal in shader reduces intersection workload compared with linear primitive scans.
- Denoiser bandwidth optimization: normals stored as `RG16F` via oct encoding.
- API overhead reduction: uniform locations are cached during renderer initialization.
- Temporal accumulation improves image quality stability at low per-frame sample counts.
- Sparse deterministic emissive placement keeps geometry overhead low while debounce/fade logic avoids unnecessary visual churn during terrain edits.
- Bridge generation is gated behind `BridgeParams::enabled`; when disabled the pipeline produces identical output to before with no extra cost.
- Bridge candidate analysis is O(gridSize²) with a constant-depth span scan, keeping CPU generation time proportional to scene size.

## 11) Known Constraints

- BVH traversal uses a fixed-size local stack in shader code.
- Scene primitives are axis-aligned cubes only (terrain columns, emissive markers, and bridge decks/supports all share the same `CubeData` layout).
- Lighting model is simple but configurable (sun direction/elevation, elevation-driven sky and ambient, elevation-driven auto exposure, sparse emissive terrain cubes, diffuse bounce).
- Bridge detection scans only in the +x and +z cardinal directions; diagonal or curved spans are not supported.
- Emissive cubes currently add radiance only when hit by traced rays; there is no dedicated direct-light sampling or MIS for emissive geometry.
- No persistence/export of generated scenes.

## 12) Non-Functional Expectations

- Startup should fail loudly when GL context or shader compilation/linking fails.
- Resizing must recreate render targets and continue rendering without restart.
- Parameter updates should trigger deterministic regeneration (same seed -> same scene).

## 13) Future Extensions

- Material variety (roughness/metalness beyond the current albedo-plus-emission cube model).
- Better GI sampling and direct-light strategies.
- Better temporal stability and history clamping.
- Alternative denoisers and quality presets.
- Mesh support and scene serialization.
- Bridge improvements: diagonal spans, arch profiles, emissive lanterns, rail geometry.
- General feature-pass architecture: roads, ruins, water, or other procedural overlays using the same terrain-analysis-first pipeline introduced for bridges.
