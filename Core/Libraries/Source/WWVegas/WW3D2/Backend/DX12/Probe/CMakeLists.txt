cmake_minimum_required(VERSION 3.20)
project(W3DDX12Probe LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "W3D DX12 smoke probe requires Windows")
endif()

# This standalone opt-in target is NOT linked into Generals or Zero Hour.
# The original DX8 backend remains the only game runtime backend.
add_executable(W3DDX12Probe DX12DrawProbe.cpp)
target_compile_features(W3DDX12Probe PRIVATE cxx_std_17)
target_compile_definitions(W3DDX12Probe PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(W3DDX12Probe PRIVATE d3d12 dxgi dxguid d3dcompiler)

enable_testing()
add_test(
    NAME W3D_DX12_WARP_Offscreen_Triangle
    COMMAND W3DDX12Probe "${CMAKE_CURRENT_SOURCE_DIR}/../W3DLegacyUnlit.hlsl"
)
set_tests_properties(W3D_DX12_WARP_Offscreen_Triangle PROPERTIES TIMEOUT 45)

# A separate pass checks WARP clears/readback with no texture copy or draw.
# If both tests report PnP device removal, the CI VM does not support
# executing software GPU commands; a physical Windows run is then needed.
add_test(
    NAME W3D_DX12_WARP_Offscreen_Clear
    COMMAND W3DDX12Probe "${CMAKE_CURRENT_SOURCE_DIR}/../W3DLegacyUnlit.hlsl" --clear-only
)
set_tests_properties(W3D_DX12_WARP_Offscreen_Clear PROPERTIES TIMEOUT 45)

# Isolate draw/vertex shaders without uploading or sampling textures.
add_test(
    NAME W3D_DX12_WARP_Offscreen_Untextured
    COMMAND W3DDX12Probe "${CMAKE_CURRENT_SOURCE_DIR}/../W3DLegacyUnlit.hlsl" --untextured
)
set_tests_properties(W3D_DX12_WARP_Offscreen_Untextured PROPERTIES TIMEOUT 45)

# Minimal vertex/pixel shader without W3D combiner or textures, to isolate
# basic rasterization failures from W3D-specific shader semantics.
add_test(
    NAME W3D_DX12_WARP_Offscreen_Solid
    COMMAND W3DDX12Probe "${CMAKE_CURRENT_SOURCE_DIR}/../W3DLegacyUnlit.hlsl" --solid
)
set_tests_properties(W3D_DX12_WARP_Offscreen_Solid PROPERTIES TIMEOUT 45)

# Validate W3D vertex shader, constant buffer, and vertex signature while
# using the known-good minimal solid pixel shader. Failure isolated here
# points to W3D vertex shader constants or signature rather than combiner.
add_test(
    NAME W3D_DX12_WARP_W3D_Vertex_Shader
    COMMAND W3DDX12Probe "${CMAKE_CURRENT_SOURCE_DIR}/../W3DLegacyUnlit.hlsl" --w3d-vs
)
set_tests_properties(W3D_DX12_WARP_W3D_Vertex_Shader PROPERTIES TIMEOUT 45)

# Draw with a minimal pixel shader sampling one texture. This verifies the
# SRV heap, sampler heap, upload/copy and shading independently of the W3D
# combiner interpreter.
add_test(
    NAME W3D_DX12_WARP_Basic_Texture_Draw
    COMMAND W3DDX12Probe "${CMAKE_CURRENT_SOURCE_DIR}/../W3DLegacyUnlit.hlsl" --basic-texture
)
set_tests_properties(W3D_DX12_WARP_Basic_Texture_Draw PROPERTIES TIMEOUT 45)
