# Vulkan Bouncing Balls

A real-time 2D simulation of colored balls bouncing off walls. When two balls collide, they explode into a burst of fading particles. Built from scratch with Vulkan.

---

## Preview

- Balls spawn with random colors and velocities
- Elastic wall bouncing
- Ball-ball collision triggers a particle explosion effect
- Particles fade out with drag over ~1 second
- New balls respawn automatically to keep the scene alive

---

## Dependencies

### macOS

Install via [Homebrew](https://brew.sh):

```bash
brew install molten-vk vulkan-headers vulkan-loader glslang glfw glm
```

| Package          | Purpose                              |
|------------------|--------------------------------------|
| `molten-vk`      | Vulkan implementation on top of Metal |
| `vulkan-headers` | Vulkan API headers                   |
| `vulkan-loader`  | Vulkan loader library                |
| `glslang`        | GLSL → SPIR-V shader compiler        |
| `glfw`           | Window creation and input            |
| `glm`            | Math library (vectors, matrices)     |

Also required (pre-installed on macOS with Xcode Command Line Tools):

```bash
xcode-select --install
```

---

### Linux

#### Ubuntu / Debian

```bash
sudo apt update
sudo apt install -y \
  libvulkan-dev \
  vulkan-validationlayers \
  glslang-tools \
  libglfw3-dev \
  libglm-dev \
  cmake \
  build-essential
```

#### Arch Linux

```bash
sudo pacman -S vulkan-devel glslang glfw-x11 glm cmake base-devel
```

> **Note:** On Linux you also need a Vulkan-capable GPU driver.
> - NVIDIA: install the proprietary driver (`nvidia`)
> - AMD: `mesa` / `vulkan-radeon`
> - Intel: `mesa` / `vulkan-intel`

---

## Build

```bash
git clone <repo-url>
cd ai_assisted_vulkan

cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Shaders are compiled automatically to SPIR-V during the build step.

---

## Run

```bash
./build/bouncing_balls
```

Close the window to exit.

---

## Project Structure

```
.
├── CMakeLists.txt       # Build system
├── main.cpp             # Vulkan app + simulation (single translation unit)
└── shaders/
    ├── ball.vert        # Vertex shader (2D passthrough)
    └── ball.frag        # Fragment shader (color + alpha)
```

---

## How It Works

| Component          | Details                                                                 |
|--------------------|-------------------------------------------------------------------------|
| **Rendering**      | One host-visible vertex buffer, rebuilt every frame                     |
| **Geometry**       | Circles tessellated as triangle lists (32 segments for balls, 8 for particles) |
| **Blending**       | Alpha blending enabled — particles fade out smoothly                    |
| **Physics**        | NDC-space positions, elastic wall bounce, O(n²) collision detection     |
| **Explosion**      | 28 particles per collision, random burst directions, velocity drag      |

---

## Authors

Built by **inexium** with **[Claude](https://claude.ai)** (Anthropic) as co-author.

> Claude assisted in designing the Vulkan architecture, writing the simulation logic, shader pipeline setup, and this documentation.
