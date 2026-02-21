cmake_minimum_required(VERSION 3.20)
project(SpoutMaskSenderDX11 CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SPOUT_DX_DIR "${CMAKE_SOURCE_DIR}/../../third_party/SpoutSDK/SpoutDX")
set(SPOUT_GL_DIR "${CMAKE_SOURCE_DIR}/../../third_party/SpoutSDK/SpoutGL")
set(THIRD_PARTY_DIR "${CMAKE_SOURCE_DIR}/../../third_party")

# --- OpenVINO ---
# Ensure OpenVINO environment is set OR provide OpenVINO_DIR to CMake:
#   - Run:  . "C:\Deps\openvino\...\setupvars.ps1"
#   - Or pass: -DOpenVINO_DIR="C:/Deps/openvino/<...>/runtime/cmake"
find_package(OpenVINO CONFIG REQUIRED)

add_executable(SpoutMaskSenderDX11
  main.cpp

  # DirectX sender/receiver wrapper
  "${SPOUT_DX_DIR}/SpoutDX.cpp"

  # Dependencies SpoutDX.cpp actually needs
  "${SPOUT_GL_DIR}/SpoutDirectX.cpp"
  "${SPOUT_GL_DIR}/SpoutUtils.cpp"
  "${SPOUT_GL_DIR}/SpoutSharedMemory.cpp"
  "${SPOUT_GL_DIR}/SpoutSenderNames.cpp"
  "${SPOUT_GL_DIR}/SpoutFrameCount.cpp"
  "${SPOUT_GL_DIR}/SpoutCopy.cpp"
)

target_include_directories(SpoutMaskSenderDX11 PRIVATE
  "${SPOUT_DX_DIR}"
  "${SPOUT_GL_DIR}"
  "${THIRD_PARTY_DIR}"   # enables: #include "json.hpp"
)

target_link_libraries(SpoutMaskSenderDX11 PRIVATE
  d3d11
  dxgi
  psapi
  openvino::runtime
)

# Optional: make debugging easier on Windows (nicer stack traces)
if (MSVC)
  target_compile_definitions(SpoutMaskSenderDX11 PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
endif()