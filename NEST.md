# NEST framework

DeepSea uses the 15-466 course's NEST C++/SDL3/OpenGL base code. The original hexapod demo and its 3D asset pipeline have been removed.

## Retained code

- `main.cpp`, `Mode.*`: window, event loop, timing, and mode dispatch.
- `PlayMode.*`: starting point for the DeepSea sandbox.
- `GL.*`, `gl_compile_program.*`, `gl_errors.hpp`: OpenGL setup and shader helpers.
- `ColorTextureProgram.*`: unlit textured geometry for sprites and layered visuals.
- `ColorProgram.*`, `DrawLines.*`, `PathFont*`: debug shapes and text.
- `Load.*`, `data_path.*`, `load_save_png.*`: asset loading, executable-relative paths, and PNG I/O.
- `Maekfile.js`: cross-platform build and dependency tracking.
- `make-GL.py`, `glcorearb.h`, `make-PathFont-font.py`, `PathFont-font.svg`: source generators for retained GL bindings and debug font; not needed for normal builds.

Using sprites does not require a Blender scene or binary mesh loader. Visual layers can later add depth while gameplay remains on a 2D plane.

## Setup

1. Install Node.js.
2. Install a C++20 compiler: Xcode command line tools on macOS, g++ on Linux, or Visual Studio C++ build tools on Windows.
3. Extract the matching course [nest-libs v0.15 release](https://github.com/15-466/nest-libs/releases/tag/v0.15) alongside this repository:

```text
parent/
  nest-libs/
    macos/       # or linux/ or windows/
  deapsea/
    Maekfile.js
```

## Build

Run commands from the repository directory. On Windows, use the Visual Studio x64 Native Tools command prompt.

```sh
node Maekfile.js          # build the game and copy dependency notices
node Maekfile.js -j1 -q   # one job at a time; stop on the first error
node Maekfile.js -v       # show full commands
```

Run `./dist/game` on macOS/Linux or `dist/game.exe` on Windows. Maek caches by content hash in `maek-cache.json`. Compiled objects go into `objs/`. Keep dependency notices in packaged builds and retain the repository license.
