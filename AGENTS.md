# AGENTS.md

## Build (CMake)

Toolchain: STM32 Arm Clang (`starm-clang`) from STM32CubeCLT, generator Ninja.
Toolchain file: `cmake/starm-clang.cmake`. Presets in `CMakePresets.json`.

Configure + build (Debug):

```
cmake --preset Debug
cmake --build --preset Debug
```

Release:

```
cmake --preset Release
cmake --build --preset Release
```

Build output: `build/<preset>/PLCJS_ETH_MODULE_12DQ_D4MG_STM32F407VGT6.{elf,hex,bin,map}`.

Clean rebuild: delete `build/<preset>` and re-run the configure step.

### Tools required
- CMake >= 3.22
- Ninja
- `starm-clang` (STM32CubeCLT) on PATH. Alternative GCC toolchain available at `cmake/gcc-arm-none-eabi.cmake`.
