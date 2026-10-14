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
