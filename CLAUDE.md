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

  
## General Code Conventions
- Structure functionality into relevant classes, utilizing modern OOP practices.
- Each class should have its own header and source file called the class name in camel case.
  - Utility classes and structs can be grouped into their utility files.
  - Supporting classes and structs can be defined in a class's files.
- Group files that logically go together into matching subdirectories where it makes sense.
  - Consider if parts of the code should be compiled as utility libraries for potential reusability.
- Implement code in the right file for its scope. E.g. code for whole application-level stuff should reside or hook into the Application class.


## Design Conventions
- Whenever adding code that uses certain values or magic numbers that would be hard coded, prefer to add each as a user-configurable setting so everything configurable at runtime.
  - Use sensical max/min values and defaults based on the context, considering usability, standard conventions, and handling any edge cases.
  - Organize settings into appliocation-level groups for different tabs of the setting menu, and organize related settings into ordered subgroups for sub-tabs or sections.
  - Keep related settings together and order them sensibly into hierarchies for optimal accessibility and human readability.

  
## C++ Style Conventions
- GNU style spacing
  - Keep code on the same line when logical and practical (prefer horizontal space).
    - Try to keep lines under 140 characters, prefer splitting up lines to going over.
    - Use spaces to align equal signs, parentheses, braces, and members/arguments between lines where it makes sense.
      - For aligning elements across lines, offset for prefixed operators or negative sign so start of variable/number is aligned. Number literals should align by decimal places or first digits.
  - Always put spaces around and inside curly braces. E.g. A a { x, y };
  - Always put block curly braces on a new line (e.g. for if, for, while, struct, class definitions), unless it's a one-liner.              
  - Always use curly braces even on one liners.
  - Two-liners should have curly braces around the second line instead of separate lines.
- Use enums named descriptively in ALL_CAPS for type indices, error codes, statically defined options, etc. and use that enum type instead of just passing/checking ints.
- Make code human-readable wherever possible!
  - Short variable names are fine where purpose is clear/standard, or matches math/physics symbolic conventions when working with relvant equations.
- Utilize C++ templates where possible to handle different types and define generic usage. E.g. A type-agnostic function that hooks an object of the given type or its member data into the proper Dear Imgui input function according to its data type.

  
## Other Conventions
- Vulkan objects follow init/create → use → destroy lifecycle; clean up in reverse order
- UBO layout: `model`, `view`, `proj` matrices (binding = 0)


## Shader Architecture
Current shaders (`shaders/shader.vert`, `shaders/shader.frag`) implement:
- Vertex input: `location=0` vec3 position, `location=1` vec3 color
- UBO at `binding=0`: model/view/proj MVP matrices
- Output: per-vertex color passed to fragment shader

