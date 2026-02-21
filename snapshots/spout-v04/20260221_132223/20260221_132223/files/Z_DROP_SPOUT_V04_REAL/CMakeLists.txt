cmake_minimum_required(VERSION 3.20)

project(SpoutMaskSenderDX11 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(SPOUT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../third_party/SpoutSDK")
set(THIRD_PARTY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../third_party")

add_executable(SpoutMaskSenderDX11
  main.cpp
  SpoutFrameCount_fix.cpp

  ${SPOUT_ROOT}/SpoutDX/SpoutDX.cpp
  ${SPOUT_ROOT}/SpoutGL/SpoutDirectX.cpp
  ${SPOUT_ROOT}/SpoutGL/SpoutUtils.cpp
  ${SPOUT_ROOT}/SpoutGL/SpoutSharedMemory.cpp
  ${SPOUT_ROOT}/SpoutGL/SpoutSenderNames.cpp
  ${SPOUT_ROOT}/SpoutGL/SpoutCopy.cpp
)

target_include_directories(SpoutMaskSenderDX11 PRIVATE
  ${SPOUT_ROOT}/SpoutDX
  ${SPOUT_ROOT}/SpoutGL
  ${THIRD_PARTY_ROOT}
)

# --- OpenVINO paths come from setupvars.ps1 via OPENVINO_ROOT_DIR ---
if(NOT DEFINED ENV{OPENVINO_ROOT_DIR})
  message(FATAL_ERROR "OPENVINO_ROOT_DIR is not set. Did you run setupvars.ps1?")
endif()

set(OPENVINO_ROOT "$ENV{OPENVINO_ROOT_DIR}")

target_include_directories(SpoutMaskSenderDX11 PRIVATE
  "${OPENVINO_ROOT}/runtime/include"
)

target_link_directories(SpoutMaskSenderDX11 PRIVATE
  "${OPENVINO_ROOT}/runtime/lib/intel64/Release"
)

target_link_libraries(SpoutMaskSenderDX11 PRIVATE
  openvino
  d3d11
  dxgi
  psapi
  winmm
)

# Override injected OV_THREAD macro
target_compile_definitions(SpoutMaskSenderDX11 PRIVATE OV_THREAD=OV_THREAD_SEQ)

target_compile_definitions(SpoutMaskSenderDX11 PRIVATE
  _CRT_SECURE_NO_WARNINGS
  NOMINMAX
  WIN32_LEAN_AND_MEAN
)