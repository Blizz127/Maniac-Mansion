cmake_minimum_required(VERSION 3.16)
project(mm_port C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()
add_compile_options(-Wall -Wextra -Wno-unused-parameter)

add_library(mmcore STATIC
  src/core/cpu.c src/core/bus.c src/core/ppu.c src/core/apu.c
  src/core/mmc1.c src/core/nes.c src/core/cart.c src/core/sha256.c
  src/core/trace.c)

add_library(mmhost_common STATIC src/host/png.c src/host/palette.c)

add_executable(mm-headless src/headless/main.c)
target_link_libraries(mm-headless mmcore mmhost_common)

find_package(SDL2 REQUIRED)
set(MM_VERSION "dev" CACHE STRING "Version string baked into the port")
add_library(mmhost STATIC src/host/config.c src/host/pads.c)
target_include_directories(mmhost PUBLIC ${SDL2_INCLUDE_DIRS})

add_executable(maniac-mansion-port src/host/main.c)
target_compile_definitions(maniac-mansion-port PRIVATE MM_VERSION="${MM_VERSION}")
target_include_directories(maniac-mansion-port PRIVATE ${SDL2_INCLUDE_DIRS})
target_link_libraries(maniac-mansion-port mmhost mmhost_common mmcore ${SDL2_LIBRARIES})

enable_testing()
add_executable(test-pads tests/test_pads.c)
target_include_directories(test-pads PRIVATE ${SDL2_INCLUDE_DIRS})
target_link_libraries(test-pads mmhost ${SDL2_LIBRARIES})
add_test(NAME pads COMMAND test-pads)
