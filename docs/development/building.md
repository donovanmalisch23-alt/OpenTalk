# Building and testing

## Windows (primary platform)

**Requirements**

- Windows 10 (version 1809 or later) or Windows 11, x64.
- Visual Studio 2022 (17.8 or later) with the "Desktop development with C++" workload. It includes MSVC, CMake and Ninja.
- Qt 6.8 LTS or later, MSVC 2022 64-bit build, from the Qt online installer or `aqtinstall`. CI uses **Qt 6.8.3**.
- [vcpkg](https://github.com/microsoft/vcpkg) for OpenSSL and libsodium. Clone it, run `bootstrap-vcpkg.bat`, and set the `VCPKG_ROOT` environment variable to its folder. The presets use vcpkg's toolchain. `vcpkg.json` pins the exact versions, and the first configure builds them, which takes about 10 minutes.

**Build**

From a "x64 Native Tools Command Prompt for VS 2022":

```bat
set CMAKE_PREFIX_PATH=C:\Qt\6.8.3\msvc2022_64
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug
```

To run the programs, add `C:\Qt\6.8.3\msvc2022_64\bin` and `build\windows-msvc-debug\vcpkg_installed\x64-windows\bin` to `PATH`, or run `windeployqt`. With Qt 6.8 the executables are placed at the top of the build folder (`build\windows-msvc-debug\OpenTalk.exe`).

**Portable mode:** create an empty file named `OpenTalk.portable` next to `OpenTalk.exe`. Settings and logs then go to a `data` folder beside it. You can also run `OpenTalk.exe --data-dir <folder>`.

**Ready-made build:** every CI run on GitHub uploads **OpenTalk-portable-windows-x64**, a zip that needs no installation. It contains `OpenTalk.exe`, `opentalk-server.exe`, `opentalk-ping.exe` and the OpenSSL DLLs. Find it on the run's page in the repository's Actions tab.

## Linux (development and CI only, not a supported user platform yet)

```sh
sudo apt install qt6-base-dev libqt6sql6-sqlite libsodium-dev libssl-dev pkg-config \
    cmake ninja-build g++ clang clang-format libclang-rt-18-dev
cmake --preset linux-debug && cmake --build --preset linux-debug && ctest --preset linux-debug
cmake --preset linux-asan  && cmake --build --preset linux-asan  && ctest --preset linux-asan
cmake --preset linux-fuzz  && cmake --build --preset linux-fuzz
build/linux-fuzz/tests/fuzz/fuzz_frame_decoder -max_total_time=60 corpus/
```

GUI tests run headless with `QT_QPA_PLATFORM=offscreen`, which the test presets set.

## Before you open a pull request

- `clang-format -i` on changed C++ files. CI checks formatting.
- Build with warnings as errors (on in all presets).
- Run `ctest`. New logic needs unit tests, and new parsers of untrusted input need a fuzz harness.
- Update the relevant documents and [status.md](../status.md).
