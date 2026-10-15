cmake_minimum_required(VERSION 3.22.1)
project(LASRecompAndroid C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
get_filename_component(ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../.." ABSOLUTE)
set(DEPS "${ROOT}/android/deps/arm64-v8a")
set(SDL_SRC "${ROOT}/android/deps/src/SDL")
if(NOT EXISTS "${DEPS}/lib/libavcodec.a" OR NOT EXISTS "${SDL_SRC}/CMakeLists.txt")
    message(FATAL_ERROR "Run tools/android-deps.sh with ANDROID_NDK_HOME before configuring the Android runtime")
endif()
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory("${SDL_SRC}" "${CMAKE_CURRENT_BINARY_DIR}/SDL")

# Retain the independent host probe for diagnosing device capabilities.
add_library(las_android SHARED android_probe.c)
target_include_directories(las_android PRIVATE "${ROOT}/runtime")
target_compile_definitions(las_android PRIVATE VK_USE_PLATFORM_ANDROID_KHR=1)
target_compile_options(las_android PRIVATE -Wall -Wextra -Werror)
target_link_libraries(las_android PRIVATE android vulkan log)

file(GLOB RT CONFIGURE_DEPENDS "${ROOT}/runtime/*.c" "${ROOT}/runtime/hle/*.c"
    "${ROOT}/runtime/services/*.c" "${ROOT}/runtime/nvn/*.c" "${ROOT}/runtime/nvn/maxwell/*.c"
    "${ROOT}/runtime/nvn/vulkan/*.c" "${ROOT}/runtime/nvn/vulkan/*.cpp")
file(GLOB UI CONFIGURE_DEPENDS "${ROOT}/runtime/ui/*.mm")
# Non-Apple UI is ordinary C++; use -x c++ so NDK clang does not introduce ObjC runtime dependencies.
set_source_files_properties(${UI} PROPERTIES LANGUAGE CXX COMPILE_FLAGS "-x c++")
set(IMGUI "${ROOT}/third_party/imgui")
add_library(las_platform SHARED "${ROOT}/runtime/platform/android.c" "${ROOT}/runtime/platform/android_bootstrap.c")
target_include_directories(las_platform PRIVATE "${ROOT}/runtime")
target_link_libraries(las_platform PRIVATE log dl)
add_library(lasrecomp SHARED ${RT} ${UI} "${ROOT}/runtime/platform/android_prepare.c" android_entry.c
    "${IMGUI}/imgui.cpp" "${IMGUI}/imgui_draw.cpp" "${IMGUI}/imgui_tables.cpp" "${IMGUI}/imgui_widgets.cpp"
    "${IMGUI}/backends/imgui_impl_sdl3.cpp" "${IMGUI}/backends/imgui_impl_vulkan.cpp")
# Keep HLE/reset registration sections and export runtime imports used by the separately generated game module.
# DF_1_GLOBAL makes those exports visible when Android loads the module with RTLD_NOW.
target_link_options(lasrecomp PRIVATE "-Wl,-z,global" "-Wl,-z,nostart-stop-gc" "-Wl,--no-undefined")
find_package(Python3 REQUIRED COMPONENTS Interpreter)
file(GLOB_RECURSE GAMELIB_INPUTS CONFIGURE_DEPENDS "${ROOT}/tools/*.py" "${ROOT}/tools/rando/patch_variants.json"
    "${ROOT}/tools/gamelib_entry.c" "${ROOT}/runtime/*.h" "${ROOT}/runtime/hle/hle_symbols.txt")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${GAMELIB_INPUTS})
execute_process(COMMAND "${Python3_EXECUTABLE}" "${ROOT}/tools/android/gamelib_id.py" --root "${ROOT}"
    OUTPUT_VARIABLE ANDROID_GAMELIB_ID OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
target_compile_definitions(lasrecomp PRIVATE VK_USE_PLATFORM_ANDROID_KHR=1 GAMELIB_ID="${ANDROID_GAMELIB_ID}")
target_compile_options(lasrecomp PRIVATE -fno-strict-aliasing -Wall -Wno-unused-function -Wno-format)
# Compile the existing SMAA source string without modifying the upstream shader.
file(READ "${ROOT}/third_party/smaa/SMAA.hlsl" SMAA)
string(REPLACE "\r" "" SMAA "${SMAA}")
string(REGEX REPLACE "[^\t\n -~]" "?" SMAA "${SMAA}")
string(REPLACE "\\" "\\\\" SMAA "${SMAA}")
string(REPLACE "\"" "\\\"" SMAA "${SMAA}")
string(REPLACE "\n" "\\n\"\n\"" SMAA "${SMAA}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/smaa_hlsl.inc" "\"${SMAA}\"")
target_include_directories(lasrecomp PRIVATE "${ROOT}/runtime" "${ROOT}/runtime/nvn" "${DEPS}/include"
    "${IMGUI}" "${IMGUI}/backends" "${CMAKE_CURRENT_BINARY_DIR}" "${ROOT}/third_party/smaa/Textures")
add_library(shaderc_shared SHARED IMPORTED)
set_target_properties(shaderc_shared PROPERTIES IMPORTED_LOCATION "${DEPS}/lib/libshaderc_shared.so")
foreach(lib avformat avcodec swscale avutil)
    add_library(${lib} STATIC IMPORTED)
    set_target_properties(${lib} PROPERTIES IMPORTED_LOCATION "${DEPS}/lib/lib${lib}.a")
endforeach()
target_link_libraries(lasrecomp PRIVATE las_platform SDL3::SDL3 shaderc_shared avformat avcodec swscale avutil
    android vulkan log dl m z)
