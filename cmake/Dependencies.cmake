# Third-party dependencies, downloaded at configure time.
include(FetchContent)

if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()

# GLFW: window, OpenGL context, input
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glfw
  URL https://github.com/glfw/glfw/releases/download/3.4/glfw-3.4.zip
  URL_HASH SHA256=b5ec004b2712fd08e8861dc271428f048775200a2df719ccf575143ba749a3e9)

# GLEW: OpenGL loader required by the Cubism Framework renderer.
# Built from its single source file; its own CMake script is outdated.
FetchContent_Declare(glew
  URL https://github.com/nigels-com/glew/releases/download/glew-2.2.0/glew-2.2.0.zip
  URL_HASH SHA256=a9046a913774395a095edcc0b0ac2d81c3aacca61787b39839b941e9be14e0d4
  SOURCE_SUBDIR do-not-build)

# Dear ImGui: settings window
FetchContent_Declare(imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG v1.92.9
  GIT_SHALLOW TRUE)

# FreeType: sharper small text than ImGui's default rasterizer, most visible on
# ordinary (non-HiDPI) monitors. Only the core library; no PNG/zlib/HarfBuzz.
set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
set(SKIP_INSTALL_ALL ON CACHE BOOL "" FORCE)
FetchContent_Declare(freetype
  GIT_REPOSITORY https://github.com/freetype/freetype.git
  GIT_TAG VER-2-13-3
  GIT_SHALLOW TRUE)

# cpp-httplib: local event endpoint (header only)
FetchContent_Declare(httplib
  GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
  GIT_TAG v0.58.0
  GIT_SHALLOW TRUE
  SOURCE_SUBDIR do-not-build)

# nlohmann/json: hook payloads, config, settings.json editing
FetchContent_Declare(nlohmann_json
  URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
  URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa)

FetchContent_MakeAvailable(glfw glew nlohmann_json imgui httplib freetype)

find_package(OpenGL REQUIRED)
add_library(glew_s STATIC ${glew_SOURCE_DIR}/src/glew.c)
target_compile_definitions(glew_s PUBLIC GLEW_STATIC GLEW_NO_GLU)
target_include_directories(glew_s PUBLIC ${glew_SOURCE_DIR}/include)
target_link_libraries(glew_s PUBLIC OpenGL::GL)
if(UNIX AND NOT APPLE)
  find_package(X11 REQUIRED)
  target_link_libraries(glew_s PUBLIC X11::X11)
endif()

add_library(imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
  ${imgui_SOURCE_DIR}/misc/freetype/imgui_freetype.cpp)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends ${imgui_SOURCE_DIR}/misc/freetype)
target_compile_definitions(imgui PUBLIC IMGUI_ENABLE_FREETYPE)
target_link_libraries(imgui PUBLIC glfw freetype)
# The pet window's overlay runs in a legacy OpenGL 2.1 context on macOS.
# KUJIRA_OVERLAY_GL2 lets a Linux build exercise that path.
option(KUJIRA_OVERLAY_GL2 "Draw the pet overlay with the OpenGL 2 backend (always on for macOS)" OFF)
if(APPLE OR KUJIRA_OVERLAY_GL2)
  target_sources(imgui PRIVATE ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl2.cpp)
  # INTERFACE: imgui_impl_opengl2.cpp defines GL_SILENCE_DEPRECATION itself.
  target_compile_definitions(imgui INTERFACE GL_SILENCE_DEPRECATION KUJIRA_OVERLAY_GL2)
  target_link_libraries(imgui PUBLIC OpenGL::GL)
endif()

add_library(httplib_header INTERFACE)
target_include_directories(httplib_header INTERFACE ${httplib_SOURCE_DIR})
