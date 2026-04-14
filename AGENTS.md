# AGENTS.md

## Build & Run
- Build: `cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build`
- Run: `./build/bouncing_balls`

## Key Technicals
- **Single Unit**: All logic (Vulkan, physics, simulation) is in `main.cpp`.
- **Shaders**: `shaders/*.vert/frag` are compiled to SPIR-V automatically via `glslangValidator` during the CMake build.
- **Shader Paths**: The binary expects SPIR-V files in the directory specified by the `SHADER_DIR` compile definition (defaults to `build/shaders/`).

## Verification
- No test suite. Verify changes by running the binary.
