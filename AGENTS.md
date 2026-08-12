# Project Rules & Guidelines

## Plugin & Build Parity Rule
* **Synchronized Build Options:** All build task runners (`.mise.toml`, `scripts/build.py`, and `pixi.toml`) MUST remain synchronized in terms of plugin flags, feature flags, and CMake option names.
* **Custom Plugins:** Any custom or core plugin added to the project (e.g. `qBeaconRPCPlugin` enabled via `-DPLUGIN_STANDARD_QBEACONRPC=ON`) MUST be explicitly registered across all three build entrypoints:
  1. `pixi.toml` (`[feature.build.tasks.configure]`)
  2. `scripts/build.py` (`cmake_cmd` string)
  3. `.mise.toml` (`[tasks.build]`)
* **Option Naming Verification:** Never invent or guess CMake option names (e.g. `-DPLUGIN_STANDARD_QJSONRPC=ON` vs `-DPLUGIN_STANDARD_QBEACONRPC=ON`). Always check the target plugin's `CMakeLists.txt` file for the exact `option(...)` definition.
* **Platform Conditional Options:** Options specific to operating system requirements MUST be conditionally handled in platform-specific branches (e.g., `if system == "Windows":` in `scripts/build.py`).

