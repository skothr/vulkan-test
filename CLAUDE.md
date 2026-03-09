# CLAUDE.md

Keep this file up to date. When making changes that affect the stack, structure, build process,
architecture, or conventions described here, update the relevant section before finishing the task.

## Stack
- C++ compiled with g++ using Makefile
- Vulkan for graphics (ray tracing pipeline)
- GLFW for window management
- Dear ImGui for UI panels and popups [v1.92.6-docking]
- GLM for math (vectors, matrices)
- stb_image_write for PNG output
- CUDA for compute (future)

## Structure
- `./src/` — C++ source files (`.cpp`)
- `./src/cuda/` — CUDA source files (`.cu`)
- `./include/` — header files
- `./lib/` — external dependencies
- `./shaders/` — GLSL ray tracing shaders
- `./shaders/compiled/` — compiled SPIR-V shaders
  
## Shader Architecture
Five ray tracing stages compiled to SPIR-V:
| File                    | Stage        | Role                                          |
|-------------------------|--------------|-----------------------------------------------|
| `shader.rgen`           | Ray gen      | Fires primary camera rays; payload loc 0      |
| `shader.rmiss`          | Miss [0]     | Floor/sky; fires shadow + caustic rays        |
| `shader_shadow.rmiss`   | Miss [1]     | Sun-direction check or occlusion passthrough  |
| `shader.rchit`          | Hit [0]      | Glass BSDF (reflect + refract)                |
| `shader_shadow.rchit`   | Hit [1]      | Glass shadow/caustic transmittance            |
| `shader.rint`           | Intersection | Analytic sphere (AABB BLAS, procedural hit)   |
Descriptor set bindings: 0 = TLAS, 1 = storage image, 2 = UBO, 3 = ParamsUBO.

## Build Commands
```bash
make          # build the project
make run      # build and run
make clean    # clean build artifacts
make format      # format all C++ sources with clang-format
make format-check  # check formatting (no changes, exit 1 on violations)
```

Shader compilation (SPIR-V):
```bash
glslangValidator -V --target-env vulkan1.2 shaders/shader.rgen -o shaders/compiled/shader.rgen.spv
# (same pattern for .rmiss, .rchit, .rint)
```

The Makefile compiles `.cu` files with `nvcc` and `.cpp` files with `g++`, linking against
`-lvulkan -lglfw -ldl -lpthread -limgui -lstb`.


## Code Conventions

### Organization
Think about where code logically belongs before writing it. Application should stay focused on
top-level app structure: Vulkan init, the main loop, swapchain management, and wiring things
together. Cohesive functionality with state management or potential for reusability should be
encapsulated in separate classes. Make these general puropose tools where possible.

Each class has its own header and source file named after the class in camelCase. Utility or
supporting types closely tied to one class can be defined alongside it.

**Classes in this codebase:**
- `Application` — top-level Vulkan setup, main loop, swapchain, ray tracing pipeline
- `ControlPanel` — ImGui context, render pass, framebuffers, settings UI
- `ScreenshotManager` — capture state, preview images, staging buffers, popups
- `KeyBindings` — key→action map, GLFW dispatch
- `Settings` — all runtime-editable parameters; `toParamsUBO()` packs them for the shader

### Design
- Prefer user-configurable settings over hard-coded values. Use sensible defaults, min/max
  ranges, and handle edge cases. Keep related settings grouped and ordered logically.
- Vulkan objects follow a strict create → use → destroy lifecycle; always clean up in reverse
  creation order.
- ImGui textures must be allocated from ImGui's own descriptor pool and layout (via
  `ImGui_ImplVulkan_AddTexture`) to be pipeline-compatible as `ImTextureID`.
- Use C++ templates and compile-time code generation and metaprogramming to optimize run time
  performance or make generic code.
- Utilize features of modern C++ standards (c++20+), e.g. concepts and constraints.

### C++ Style
Formatting (spacing, braces, line length, alignment) is enforced by clang-format via
`.clang-format`. Run `make format` before committing. Remaining conventions not covered
by the formatter:
- Prefer horizontal space; keep related code on one line when readable.
- When aligning across lines, offset for unary operators/signs so the value starts are aligned.
- Always use curly braces, even for one-liners. Two-liners use braces on the second line only:
  ```cpp
  if (condition)
    { doSomething(); }
  ```
- Enum values are `ALL_CAPS`; enum type names are `PascalCase`. Use the enum type instead of
  raw ints for any categorically distinct set of options or states.
- Short variable names are fine where purpose is clear or matches math/physics conventions.
- Name functions for what they do, not how: `saveScreenshot()` not `doSaveScreenshot()`.
- Boolean accessors: use a clear descriptive name, or prefix with `is` if needed for clarity.

