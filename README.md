# Vibe coding test project

A minimal Vulkan application generated via vibe coding with Claude Code, used to test AI-assisted C++ graphics development. Displays a colored rotating cube using the Vulkan graphics API.

## What it does

Renders a 3D cube with per-vertex colors (red, green, blue, yellow, magenta, cyan, white, grey) rotating continuously on screen using a full Vulkan rendering pipeline — no helper libraries, just raw Vulkan + GLFW + GLM.

## Build & Run

```bash
make run
```

## **Dependencies**:

### Ubuntu/Debian
```bash
sudo apt-get install -y vulkan-tools libvulkan-dev glslang-tools libglfw3-dev libglm-dev
```

### Arch Linux:
```bash
sudo pacman -S vulkan-tools vulkan-headers glslang glfw-x11 glm
```
> Use `glfw-wayland` instead of `glfw-x11` if running a Wayland compositor.

## Structure

```
src/main.cpp          # full Vulkan app (~450 lines, single file)
shaders/shader.vert   # vertex shader — MVP transform
shaders/shader.frag   # fragment shader — pass-through color
Makefile              # builds shaders (SPIR-V) then compiles C++
```
