cmake_minimum_required(VERSION 3.20)

project(Kujira VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ---------------------------------------------------------------------------
# Live2D Cubism SDK for Native (not in git, see docs/BUILD.md)
# ---------------------------------------------------------------------------
set(CUBISM_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/CubismSdkForNative"
  CACHE PATH "Path to the extracted CubismSdkForNative-5-r.x folder")

if(NOT EXISTS "${CUBISM_SDK_DIR}/Core/include/Live2DCubismCore.h")
  message(FATAL_ERROR
    "Cubism SDK not found at ${CUBISM_SDK_DIR}.\n"
    "Download 'Cubism SDK for Native' from https://www.live2d.com/sdk/download/native/ ,"
    " extract it and put (or link) the folder at third_party/CubismSdkForNative,"
    " or pass -DCUBISM_SDK_DIR=<path>.")
endif()

set(CUBISM_CORE_DIR "${CUBISM_SDK_DIR}/Core")
set(CUBISM_FRAMEWORK_DIR "${CUBISM_SDK_DIR}/Framework")

include(cmake/Dependencies.cmake)

# Cubism Core (closed-source static library)
add_library(Live2DCubismCore STATIC IMPORTED)
if(WIN32)
  if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(_core_arch x86_64)
  else()
    set(_core_arch x86)
  endif()
  if(MSVC_VERSION LESS 1920)
    set(_core_vc 141)
  elseif(MSVC_VERSION LESS 1930)
    set(_core_vc 142)
  else()
    # VS 2022 and newer share the v14x ABI
    set(_core_vc 143)
  endif()
  set(_core_lib_dir "${CUBISM_CORE_DIR}/lib/windows/${_core_arch}/${_core_vc}")
  # We build with the DLL runtime (/MD), so use the matching Core flavour.
  set_target_properties(Live2DCubismCore PROPERTIES
    IMPORTED_LOCATION_DEBUG "${_core_lib_dir}/Live2DCubismCore_MDd.lib"
    IMPORTED_LOCATION_RELEASE "${_core_lib_dir}/Live2DCubismCore_MD.lib"
    IMPORTED_LOCATION_RELWITHDEBINFO "${_core_lib_dir}/Live2DCubismCore_MD.lib"
    IMPORTED_LOCATION_MINSIZEREL "${_core_lib_dir}/Live2DCubismCore_MD.lib"
    IMPORTED_LOCATION "${_core_lib_dir}/Live2DCubismCore_MD.lib")
elseif(APPLE)
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
    set(_core_arch arm64)
  else()
    set(_core_arch x86_64)
  endif()
  set_target_properties(Live2DCubismCore PROPERTIES
    IMPORTED_LOCATION "${CUBISM_CORE_DIR}/lib/macos/${_core_arch}/libLive2DCubismCore.a")
else()
  set_target_properties(Live2DCubismCore PROPERTIES
    IMPORTED_LOCATION "${CUBISM_CORE_DIR}/lib/linux/x86_64/libLive2DCubismCore.a")
endif()
set_target_properties(Live2DCubismCore PROPERTIES
  INTERFACE_INCLUDE_DIRECTORIES "${CUBISM_CORE_DIR}/include")

# Cubism Framework (open source, OpenGL renderer)
set(FRAMEWORK_SOURCE OpenGL)
add_subdirectory("${CUBISM_FRAMEWORK_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/CubismFramework")
if(WIN32)
  target_compile_definitions(Framework PUBLIC CSM_TARGET_WIN_GL)
elseif(APPLE)
  target_compile_definitions(Framework PUBLIC CSM_TARGET_MAC_GL)
else()
  target_compile_definitions(Framework PUBLIC CSM_TARGET_LINUX_GL)
endif()
target_link_libraries(Framework PUBLIC Live2DCubismCore glew_s)
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.25)
  # Treat SDK headers as system headers: their warnings are not ours to fix.
  set_target_properties(Framework PROPERTIES SYSTEM TRUE)
endif()
if(MSVC)
  target_compile_options(Framework PRIVATE /utf-8 /W0)
else()
  target_compile_options(Framework PRIVATE -w)
endif()

find_package(OpenGL REQUIRED)

# ---------------------------------------------------------------------------
# App
# ---------------------------------------------------------------------------
set(KUJIRA_SOURCES
  src/main.cpp
  src/core/App.cpp
  src/core/Config.cpp
  src/core/Dialogue.cpp
  src/core/FileUtil.cpp
  src/core/PetModel.cpp
  src/core/StateMachine.cpp
  src/events/EventServer.cpp
  src/agents/Registry.cpp
  src/agents/claude-code/ClaudeCodeAdapter.cpp
  src/agents/claude-code/ClaudeCodeInstaller.cpp
  src/ui/PetOverlay.cpp
  src/ui/SettingsWindow.cpp
  src/ui/Theme.cpp
)
if(WIN32)
  list(APPEND KUJIRA_SOURCES src/platform/win/PlatformWin.cpp)
else()
  list(APPEND KUJIRA_SOURCES src/platform/posix/PlatformPosix.cpp)
endif()

add_executable(Kujira ${KUJIRA_SOURCES})
target_include_directories(Kujira PRIVATE
  src
  "${CUBISM_SDK_DIR}/Samples/OpenGL/thirdParty/stb")
target_compile_definitions(Kujira PRIVATE
  KUJIRA_VERSION="${PROJECT_VERSION}")
target_link_libraries(Kujira PRIVATE
  Framework glfw imgui httplib_header nlohmann_json::nlohmann_json OpenGL::GL)

if(WIN32)
  # Version info (name shown in Task Manager) and the exe icon. The icon is made at
  # build time from the local model's icon.png, so no model artwork lives in git.
  # GLFW uses the resource named GLFW_ICON for all of its windows.
  set(_rc "${CMAKE_CURRENT_BINARY_DIR}/kujira.rc")
  set(_rc_icon "")
  file(GLOB _model_icons "${CMAKE_CURRENT_SOURCE_DIR}/assets/models/*/icon.png")
  if(_model_icons)
    list(SORT _model_icons)
    list(GET _model_icons 0 _icon_png)
    add_executable(png2ico tools/png2ico.cpp)
    target_include_directories(png2ico PRIVATE "${CUBISM_SDK_DIR}/Samples/OpenGL/thirdParty/stb")
    target_link_libraries(png2ico PRIVATE shell32)
    set(_ico "${CMAKE_CURRENT_BINARY_DIR}/kujira.ico")
    add_custom_command(OUTPUT "${_ico}"
      COMMAND png2ico "${_icon_png}" "${_ico}"
      DEPENDS png2ico "${_icon_png}"
      COMMENT "Making kujira.ico from ${_icon_png}")
    set(_rc_icon "GLFW_ICON ICON \"kujira.ico\"\n")
    target_sources(Kujira PRIVATE "${_ico}")
    set_source_files_properties("${_rc}" PROPERTIES OBJECT_DEPENDS "${_ico}")
  endif()
  set(KUJIRA_RC_ICON "${_rc_icon}")
  string(REPLACE "." "," KUJIRA_VERSION_COMMA "${PROJECT_VERSION}")
  configure_file(src/platform/win/kujira.rc.in "${_rc}" @ONLY)
  target_sources(Kujira PRIVATE "${_rc}")

  target_compile_definitions(Kujira PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
  target_link_libraries(Kujira PRIVATE ws2_32 psapi shell32 ole32 uuid advapi32 winmm)
  # No console window; keep a normal main() entry point.
  set_target_properties(Kujira PROPERTIES WIN32_EXECUTABLE TRUE)
  if(MSVC)
    target_link_options(Kujira PRIVATE /ENTRY:mainCRTStartup)
  endif()
elseif(APPLE)
  target_link_libraries(Kujira PRIVATE "-framework Cocoa" "-framework IOKit")
else()
  find_package(Threads REQUIRED)
  target_link_libraries(Kujira PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
endif()

if(MSVC)
  target_compile_options(Kujira PRIVATE /utf-8 /W3 /MP)
  set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT Kujira)
else()
  target_compile_options(Kujira PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# Runtime files next to the executable: Framework shaders and local models.
set(_shader_dir "${CUBISM_FRAMEWORK_DIR}/src/Rendering/OpenGL/Shaders/Standard")
add_custom_command(TARGET Kujira POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${_shader_dir}" "$<TARGET_FILE_DIR:Kujira>/FrameworkShaders"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:Kujira>/assets"
  COMMENT "Copying shaders and assets next to Kujira")
