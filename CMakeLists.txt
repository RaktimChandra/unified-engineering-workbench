cmake_minimum_required(VERSION 3.20)
project(UnifiedEngineeringWorkbench VERSION 1.0.0 LANGUAGES CXX)
if(WIN32)
  enable_language(RC)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(WB_BUILD_TESTS "Build the unit/integration test suite" ON)
option(WB_STATIC_RUNTIME "Statically link the C++ runtime (Windows release packaging)" OFF)

find_package(Threads REQUIRED)

# Record the effective optimisation flags so performance reports are reproducible.
string(TOUPPER "${CMAKE_BUILD_TYPE}" _bt)
set(WB_FLAGS_STRING "${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_${_bt}}")
string(STRIP "${WB_FLAGS_STRING}" WB_FLAGS_STRING)

# Reproducible, path-independent binaries: __FILE__ (used by doctest and
# assertions) is recorded relative to the source root, so no build-machine
# path ends up inside the shipped executables.
if(NOT MSVC)
  add_compile_options("-ffile-prefix-map=${PROJECT_SOURCE_DIR}/=")
endif()

# ---------------------------------------------------------------- warnings --
add_library(wb_warnings INTERFACE)
if(MSVC)
  target_compile_options(wb_warnings INTERFACE /W4 /permissive- /utf-8)
else()
  target_compile_options(wb_warnings INTERFACE -Wall -Wextra -Wpedantic -Wshadow)
endif()

# ------------------------------------------------------------ third party --
# Header-only, vendored and pinned (see DEPENDENCIES.lock). Marked SYSTEM so
# their warnings do not drown ours.
add_library(wb_third_party INTERFACE)
target_include_directories(wb_third_party SYSTEM INTERFACE ${PROJECT_SOURCE_DIR}/third_party)

# --------------------------------------------------------------- core lib --
add_library(wb_core STATIC
  src/core/src/stl.cpp
  src/core/src/nodal_csv.cpp
  src/core/src/fea1d.cpp
  src/core/src/parallel.cpp
  src/core/src/sysinfo.cpp
  src/core/src/benchmark.cpp
  src/core/src/hw_mock.cpp
  src/core/src/backprop.cpp
  src/core/src/report.cpp)
target_include_directories(wb_core PUBLIC src/core/include)
target_link_libraries(wb_core PUBLIC Threads::Threads PRIVATE wb_warnings)
target_compile_definitions(wb_core PRIVATE
  WB_BUILD_FLAGS="${WB_FLAGS_STRING}" WB_BUILD_TYPE="${CMAKE_BUILD_TYPE}")

# -------------------------------------------------------- transport layer --
add_library(wb_api STATIC src/app/api.cpp src/app/jobs.cpp src/app/json_codec.cpp)
target_include_directories(wb_api PUBLIC src/app)
target_link_libraries(wb_api PUBLIC wb_core wb_third_party PRIVATE wb_warnings)
if(WIN32)
  # cpp-httplib needs the Windows 10 API surface; Windows 11 is the target.
  target_compile_definitions(wb_api PUBLIC _WIN32_WINNT=0x0A00 NOMINMAX WIN32_LEAN_AND_MEAN)
  target_link_libraries(wb_api PUBLIC ws2_32 crypt32)
endif()

# Launch.exe: HTTP host + browser launcher. GUI subsystem on Windows so no console window appears.
add_executable(Launch src/app/main.cpp src/app/platform.cpp)
target_link_libraries(Launch PRIVATE wb_api wb_warnings)
if(WIN32)
  target_link_libraries(Launch PRIVATE shell32)
  if(MSVC)
    target_link_options(Launch PRIVATE /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup)
  else()
    target_link_options(Launch PRIVATE -mwindows)
  endif()
  target_sources(Launch PRIVATE packaging/launch.rc)
endif()

# Headless CLI: regenerates every CSV/SVG in results/ from the same core.
add_executable(wb_cli src/cli/main.cpp)
target_link_libraries(wb_cli PRIVATE wb_core wb_warnings)

if(WB_STATIC_RUNTIME AND MINGW)
  foreach(t Launch wb_cli)
    target_link_options(${t} PRIVATE -static -static-libgcc -static-libstdc++)
  endforeach()
endif()

# ------------------------------------------------------------------ tests --
if(WB_BUILD_TESTS)
  enable_testing()
  add_executable(wb_tests
    tests/test_main.cpp tests/test_stl.cpp tests/test_csv.cpp tests/test_fea.cpp
    tests/test_parallel.cpp tests/test_hw.cpp tests/test_backprop.cpp tests/test_api.cpp)
  target_link_libraries(wb_tests PRIVATE wb_api wb_third_party)
  if(WB_STATIC_RUNTIME AND MINGW)
    target_link_options(wb_tests PRIVATE -static -static-libgcc -static-libstdc++)
  endif()
  add_test(NAME wb_tests COMMAND wb_tests WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
endif()
