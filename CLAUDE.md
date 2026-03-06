# CLAUDE.md

## Stack
- C++ compiled with gcc using Makefile
- Vulkan for graphics
- CUDA for compute
- GLFW for window management

## Structure
- `./src/` — C++ source files (`.cpp`)
- `./src/cuda/` — CUDA source files (`.cu`)
- `./include/` — header files
- `./lib/` — external dependencies
- `./shaders/` — GLSL shaders (`.vert`, `.frag`, etc.)
- `./shaders/compiled/` — compiled SPIR-V shaders (output of `glslc`)

## Build Commands
```bash
make          # build the project
make run      # build and run
make clean    # clean build artifacts
```

Shader compilation (SPIR-V):
```bash
glslc shaders/shader.vert -o shaders/compiled/shader.vert.spv
glslc shaders/shader.frag -o shaders/compiled/shader.frag.spv
```

The Makefile should compile `.cu` files with `nvcc` and `.cpp` files with `g++`, linking against `-lvulkan -lglfw -ldl -lpthread`.


## Conventions
- GNU style spacing
- Keep code on the same line when logical and practical (prefer horizontal space)
- Vulkan objects follow init/create → use → destroy lifecycle; clean up in reverse order
- UBO layout: `model`, `view`, `proj` matrices (binding = 0)


## Shader Architecture
Current shaders (`shaders/shader.vert`, `shaders/shader.frag`) implement:
- Vertex input: `location=0` vec3 position, `location=1` vec3 color
- UBO at `binding=0`: model/view/proj MVP matrices
- Output: per-vertex color passed to fragment shader
