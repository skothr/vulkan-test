# CLAUDE.md
Keep CLAUDE.md up to date. When changes have been made that affect the project stack, structure,
architecture, build process, conventions, or agents, update any sections of CLAUDE.md that are
out of date before committing.

ALWAYS ask the user for permission to make changes to CLAUDE.md.

## Stack
- C++ compiled with g++ using Makefile [c++23]
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
- `./lib/` — dependencies
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

The Makefile compiles `.cu` files with `nvcc` and `.cpp` files with `g++`, linking against
`-lvulkan -lglfw -ldl -lpthread -limgui -lstb`.

Shader compilation (SPIR-V):
```bash
glslangValidator -V --target-env vulkan1.2 shaders/shader.rgen -o shaders/compiled/shader.rgen.spv
# (same pattern for .rmiss, .rchit, .rint)
```

## Workflow — Proactive Agent Usage

Proactively launch specialized agents. Parallelize whenever tasks are independent: launch multiple
agents concurrently, use background mode for non-blocking work like testing, and security review.
Prefer delegating nontrivial tasks and research to agents over doing work inline to keep the main
context window clean for high-level decisions and user interaction.

- **Plan agent**: Use for complex multi-step features before writing code. After completing code
  changes, launch test-agent + security-auditor in background as standard.
- **security-auditor**: Launch in background after finishing tasks that involve external input
  handling, file I/O, memory management, or anything that involves web content.
- **tester**: Launch in background after writing/refactoring significant code.
- **ui-ux-designer**: Delegate any UI/UX work (controls, layout, popups, visual feedback).
- **senior-engineer**: Delegate substantial implementation work — new features, refactoring,
  bug fixes, and general coding tasks.
- **docs-writer**: Launch after significant code changes, new features, or structural changes
  to update documentation and CLAUDE.md.

## Code Conventions

### Organization
Think about where code logically belongs before writing it. Application should stay focused on
top-level app structure and the main loop, wiring things together. Cohesive functionality that has
state management or potential for reusability should be encapsulated in separate classes. Design
general purpose tools where possible.

Each class has its own header and source file with the same base name as the class but in camelCase
(e.g. `class ControlPanel` → `inc/*/controlPanel.hpp` and `src/*/controlPanel.cpp`).
Utility or supporting code closely tied to one class can be defined alongside or inside it.

**Classes in this codebase:**
- `Application` — top-level Vulkan setup, main loop
- `ControlPanel` — ImGui window for setting controls
- `ScreenshotManager` — handles screenshot capture and saving
- `KeyBindings` — key→action map, GLFW dispatch
- `Settings` — all runtime-editable parameters; packs values for shader
- `cfg::SettingsManager` — generic settings registry with metadata and auto ImGui rendering

### Design
- Always prefer user-configurable settings over hard-coded values, unless the. Set sensible
  defaults, min/max ranges, and handle edge cases. Keep related settings grouped and ordered
  logically into sections. Design useable interactive components for different setting data types
  to unify settings and provide better functionality (e.g. N-D point has N float inputs and a button
  to use the mouse to select a point in the scene, float angle could have a popup to select an angle
  in [0, 2pi]).
- Vulkan objects follow a strict create → use → destroy lifecycle; always clean up in reverse
  creation order.
- ImGui textures must be allocated from ImGui's own descriptor pool and layout (via
  `ImGui_ImplVulkan_AddTexture`) to be pipeline-compatible as `ImTextureID`.
- Use C++ templates, metaprogramming, and compile-time code generation to make generic code and optimize
  run time performance.
- Utilize features from modern C++ standards (e.g. concepts and constraints).

### C++ Style
- General formatting (spacing, braces, line length, alignment) is enforced by clang-format via
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

# CRITICAL INSTRUCTIONS
- Never remove files without user permission.
- Never add new dependencies without user permission.
- When in doubt ask for user input instead just of guessing, especially for critical decisions and design direction.
- Ask for clarification on anything if it would help generation.
