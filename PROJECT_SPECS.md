# IsometricPathTracer - Project Specifications

## 1) Project Summary

IsometricPathTracer is a real-time, GPU-driven isometric path tracing demo written in C++17 and OpenGL 4.1.  
The application renders a procedural voxel-like terrain composed of axis-aligned cubes, applies temporal accumulation and A-Trous denoising, then displays the result with ACES tonemapping.

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
- Material system beyond per-cube albedo.
- Advanced lighting models (MIS, emissive geometry, area lights).
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
  - Calls generation, tracing, accumulation, denoise, and present passes in order.

- `src/Camera.h/.cpp`
  - Isometric-style camera parameterization (`azimuth`, `elevation`, `distance`, `orthoScale`).
  - Uploads camera data through UBO (`CameraUBO`) to shaders.

- `src/ProceduralGen.h/.cpp`
  - Generates cube terrain using Perlin-based height values.
  - Builds a SAH-binned BVH on CPU.
  - Uploads cube data and BVH nodes to texture buffers for GPU traversal.

- `src/Renderer.h/.cpp`
  - Creates/owns GL programs, textures, and FBOs.
  - Executes rendering pipeline passes.
  - Handles resize and resource lifecycle.

### Shader stages

- `shaders/pathtrace.frag`
  - Primary path tracing pass.
  - BVH traversal for intersections and occlusion checks.
  - Runtime sun direction plus elevation-driven sky and ambient lighting.
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
  - Display pass with ACES filmic tonemapping and gamma correction.

- `shaders/quad.vert`
  - Fullscreen triangle vertex shader.

## 6) Rendering Pipeline

Per-frame pipeline:

1. Optional scene regeneration (if generation parameters changed).
2. Path trace pass (`pathtrace.frag`) -> `colorTex_`, `normalTex_`, `depthTex_`.
3. Temporal accumulation (`accumulate.frag`) -> ping/pong `accum` texture.
4. A-Trous denoise (`atrous.frag`) -> ping/pong denoise textures.
5. Final present (`quad.frag`) -> swapchain framebuffer.

Notes:

- Rendering is progressive: each frame adds one noisy sample per pixel and converges over time.
- `frameIndex` is reset when scene, camera, or lighting changes to avoid ghosted accumulation.

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

## 11) Known Constraints

- BVH traversal uses a fixed-size local stack in shader code.
- Scene primitives are axis-aligned cubes only.
- Lighting model is simple but configurable (sun direction/elevation, elevation-driven sky and ambient, diffuse bounce).
- No persistence/export of generated scenes.

## 12) Non-Functional Expectations

- Startup should fail loudly when GL context or shader compilation/linking fails.
- Resizing must recreate render targets and continue rendering without restart.
- Parameter updates should trigger deterministic regeneration (same seed -> same scene).

## 13) Future Extensions

- Material variety (roughness/metalness/emission).
- Better GI sampling and direct-light strategies.
- Better temporal stability and history clamping.
- Alternative denoisers and quality presets.
- Mesh support and scene serialization.
