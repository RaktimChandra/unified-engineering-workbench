# Building from source (CMake)

The packaged `Launch.exe` needs no build. This page is for reviewers who want to rebuild.

## Requirements

* CMake >= 3.20
* A C++17 compiler: MSVC 2019/2022, MinGW-w64 GCC >= 9 (posix threads), GCC >= 9 or Clang >= 10
* No package manager and no network: every dependency is vendored in `third_party/` and
  `src/frontend/vendor/` and pinned in `DEPENDENCIES.lock`.

## Windows (Visual Studio 2022, Developer PowerShell)

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
build\Release\Launch.exe
```

A source build finds the UI in `src\frontend` and the examples in `data\`. The launcher searches
`app\web`, `..\src\frontend` and `..\..\src\frontend` relative to the executable. You can pass
`--web-root DIR` and `--data-dir DIR` to override this. `--no-browser` and `--port N` are also available.

## Linux / macOS

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
./build/Launch            # prints READY http://127.0.0.1:<port>/ and opens the browser
```

## Packaging the Windows ZIP (how the submission was produced)

The release binaries were cross-compiled on Linux with MinGW-w64 GCC 13 (posix thread model).
The C++ runtime is linked statically, so the exe imports only DLLs that ship with Windows:

```sh
cmake -S . -B build-win -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake \
      -DCMAKE_BUILD_TYPE=Release -DWB_STATIC_RUNTIME=ON
cmake --build build-win
scripts/package.sh            # assembles dist/RA2311033010038_Workbench.zip
```

`scripts/package.sh` only copies files, converts SVG plots to PNG, writes the git bundle and creates
the ZIP. It implements no application features.

## Regenerating evidence

```sh
build/wb_cli gen-examples data          # example + malformed files, manifest.json
build/wb_cli fea results                # Task C CSV + SVG
build/wb_cli backprop results           # Task F CSV + SVG
build/wb_cli bench results              # Task D CSV + host report + SVG (defaults: 4000 x 4000, workers 1,2,4)
build/wb_cli limits /tmp/limits         # file-size timing study (docs/file_limits.md)
```

Compiler flags are recorded at configure time and printed in every benchmark report (Release: `-O3 -DNDEBUG` on GCC/Clang).
