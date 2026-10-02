cmake_minimum_required(VERSION 3.20)
project(Voxual LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Static CRT so the executables are fully standalone (no VC++ redistributable needed)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/third_party/imgui)
set(COMMON_DEFS UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
set(COMMON_LIBS d3d11 dxgi d3dcompiler dwmapi winhttp shell32 ole32 comdlg32 uuid psapi user32 gdi32 advapi32 ws2_32 shlwapi windowscodecs)

# ---- Dear ImGui (shared by the app and the installer) ------------------------------
add_library(imgui_lib STATIC
    ${IMGUI_DIR}/imgui.cpp ${IMGUI_DIR}/imgui_draw.cpp ${IMGUI_DIR}/imgui_tables.cpp ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_stdlib.cpp ${IMGUI_DIR}/imgui_impl_win32.cpp ${IMGUI_DIR}/imgui_impl_dx11.cpp)
target_include_directories(imgui_lib PUBLIC ${IMGUI_DIR})
target_compile_definitions(imgui_lib PRIVATE ${COMMON_DEFS})

# ---- the app ----------------------------------------------------------------------------
file(GLOB APP_SOURCES ${CMAKE_SOURCE_DIR}/src/*.cpp)
add_executable(Voxual WIN32 ${APP_SOURCES} ${CMAKE_SOURCE_DIR}/res/app.rc)
target_include_directories(Voxual PRIVATE ${CMAKE_SOURCE_DIR}/third_party ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(Voxual PRIVATE ${COMMON_DEFS})
target_link_libraries(Voxual PRIVATE imgui_lib ${COMMON_LIBS})
set_source_files_properties(${CMAKE_SOURCE_DIR}/res/app.rc PROPERTIES OBJECT_DEPENDS
    "${CMAKE_SOURCE_DIR}/res/app.ico;${CMAKE_SOURCE_DIR}/res/app.manifest;${CMAKE_SOURCE_DIR}/src/version.h")

# ---- the installer (embeds Voxual.exe as a resource) -------------------------------
set(PAYLOAD_EXE ${CMAKE_BINARY_DIR}/Voxual.exe)
configure_file(${CMAKE_SOURCE_DIR}/res/setup.rc.in ${CMAKE_BINARY_DIR}/setup.rc @ONLY)
add_executable(VoxualSetup WIN32
    ${CMAKE_SOURCE_DIR}/setup/setup_main.cpp
    ${CMAKE_SOURCE_DIR}/src/ui.cpp ${CMAKE_SOURCE_DIR}/src/theme.cpp ${CMAKE_SOURCE_DIR}/src/util.cpp ${CMAKE_SOURCE_DIR}/src/logo.cpp
    ${CMAKE_SOURCE_DIR}/src/shot.cpp
    ${CMAKE_BINARY_DIR}/setup.rc)
target_include_directories(VoxualSetup PRIVATE ${CMAKE_SOURCE_DIR}/third_party ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(VoxualSetup PRIVATE ${COMMON_DEFS})
target_link_libraries(VoxualSetup PRIVATE imgui_lib ${COMMON_LIBS})
add_dependencies(VoxualSetup Voxual)
set_source_files_properties(${CMAKE_BINARY_DIR}/setup.rc PROPERTIES OBJECT_DEPENDS "${PAYLOAD_EXE};${CMAKE_SOURCE_DIR}/res/app.ico")

foreach(t Voxual VoxualSetup)
  if(MSVC)
    target_compile_options(${t} PRIVATE /utf-8 /W3 /permissive- /MP)
    target_link_options(${t} PRIVATE /SUBSYSTEM:WINDOWS /MANIFEST:NO)
  endif()
endforeach()
