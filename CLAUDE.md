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

## Build Commands
```bash
make          # build the project
make run      # build and run
make clean    # clean build artifacts
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
together. Cohesive functionality with its own state and behavior — especially anything that
would otherwise spread many related members and methods across a larger class — should be
encapsulated in its own class.

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

### C++ Style
- GNU style spacing. Prefer horizontal space; keep related code on one line when readable.
  Keep lines under 140 characters; split rather than exceed.
- Align `=`, parentheses, braces, and arguments across related lines where it aids readability.
  When aligning across lines, offset for unary operators/signs so the value starts are aligned.
- Always put spaces around and inside curly braces: `A a { x, y };`
- Block curly braces go on a new line for `if`, `for`, `while`, `struct`, `class`, etc.,
  unless the entire statement fits on one line.
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
