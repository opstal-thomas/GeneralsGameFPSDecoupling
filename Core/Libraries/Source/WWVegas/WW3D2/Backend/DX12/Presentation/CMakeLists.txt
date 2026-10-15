cmake_minimum_required(VERSION 3.20)
project(W3DDX12Presentation LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "DX12 W3D presentation smoke test requires Windows")
endif()

# Compile the same backend-owned device implementation intended for WW3D2,
# not an independent reimplementation in the test harness.
add_library(W3DDX12PresentationDevice STATIC
    ../DX12PresentationDevice.cpp
    ../DX12W3DPassGraph.cpp
    ../DX12W3DGeometryBuffers.cpp
    ../DX12PrimaryWindow.cpp
    ../DX12IndexedMeshPass.cpp
)
target_compile_features(W3DDX12PresentationDevice PUBLIC cxx_std_17)
target_compile_definitions(W3DDX12PresentationDevice PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(W3DDX12PresentationDevice PUBLIC d3d12 dxgi dxguid d3dcompiler)

add_executable(W3DDX12PresentationSmoke DX12PresentationSmoke.cpp)
target_compile_features(W3DDX12PresentationSmoke PRIVATE cxx_std_17)
target_compile_definitions(W3DDX12PresentationSmoke PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(W3DDX12PresentationSmoke PRIVATE W3DDX12PresentationDevice user32)

enable_testing()
add_test(NAME W3D_DX12_WARP_Swapchain_Present
    COMMAND W3DDX12PresentationSmoke --warp)
set_tests_properties(W3D_DX12_WARP_Swapchain_Present PROPERTIES TIMEOUT 90)

# W3D shader/material validation using the reusable indexed mesh pass.
add_test(
    NAME W3D_DX12_WARP_Indexed_Mesh
    COMMAND W3DDX12PresentationSmoke --mesh-warp
        "${CMAKE_CURRENT_SOURCE_DIR}/../W3DLegacyUnlit.hlsl"
)
set_tests_properties(W3D_DX12_WARP_Indexed_Mesh PROPERTIES TIMEOUT 90)

# DX12-B03: test the exact window-ownership adapter used by the game, on
# both Win32 and x64, with real WARP swap-chain presentations.
add_executable(W3DDX12PrimaryWindowSmoke DX12PrimaryWindowSmoke.cpp)
target_compile_features(W3DDX12PrimaryWindowSmoke PRIVATE cxx_std_17)
target_compile_definitions(W3DDX12PrimaryWindowSmoke PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(W3DDX12PrimaryWindowSmoke PRIVATE W3DDX12PresentationDevice user32)

add_test(NAME W3D_DX12_WARP_PrimaryWindow_Lifecycle
    COMMAND W3DDX12PrimaryWindowSmoke)
set_tests_properties(W3D_DX12_WARP_PrimaryWindow_Lifecycle PROPERTIES TIMEOUT 90)

# B04: compile the actual selected Renderer translation unit into a standalone
# WARP smoke test. This exercises Renderer::Init/Begin_Scene/End_Scene/Clear
# and mode/transform queries against the game's HWND, without any DX8 code.
add_executable(W3DDX12RendererFrameSmoke
    DX12RendererFrameSmoke.cpp
    ../DX12Backend.cpp
)
target_include_directories(W3DDX12RendererFrameSmoke PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../.."
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../Include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../../../Dependencies/Utility"
)
target_compile_features(W3DDX12RendererFrameSmoke PRIVATE cxx_std_17)
# WWLib's C++98 std::move compatibility shim branches on __cplusplus.
# MSVC reports 199711L by default even with /std:c++17, so expose the
# actual language version to prevent injecting that shim into the STL.
target_compile_options(W3DDX12RendererFrameSmoke PRIVATE /Zc:__cplusplus)
target_compile_definitions(W3DDX12RendererFrameSmoke PRIVATE
    W3D_RENDER_BACKEND_DX12=1 NOMINMAX WIN32_LEAN_AND_MEAN
)
target_link_libraries(W3DDX12RendererFrameSmoke PRIVATE
    W3DDX12PresentationDevice user32
)
add_test(NAME W3D_DX12_WARP_Renderer_Frame_State
    COMMAND W3DDX12RendererFrameSmoke)
set_tests_properties(W3D_DX12_WARP_Renderer_Frame_State PROPERTIES TIMEOUT 90)

# DX12-B10: a separately built Windows GUI executable, with the real native
# Renderer implementation and no engine/game bootstrap or DX8 dependencies.
# Only THIS executable receives the developer checkpoint compile definition;
# normal Generals / Zero Hour DX12 configurations remain fail-closed.
add_executable(W3DDX12LaunchCheckpoint WIN32
    DX12LaunchCheckpoint.cpp
    ../DX12Backend.cpp
)
target_include_directories(W3DDX12LaunchCheckpoint PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../.."
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../Include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../../../Dependencies/Utility"
)
target_compile_features(W3DDX12LaunchCheckpoint PRIVATE cxx_std_17)
target_compile_options(W3DDX12LaunchCheckpoint PRIVATE /Zc:__cplusplus)
target_compile_definitions(W3DDX12LaunchCheckpoint PRIVATE
    W3D_RENDER_BACKEND_DX12=1
    W3D_DX12_DEVELOPER_CHECKPOINT=1
    NOMINMAX WIN32_LEAN_AND_MEAN
)
target_link_libraries(W3DDX12LaunchCheckpoint PRIVATE
    W3DDX12PresentationDevice user32 shell32
)
add_test(NAME W3D_DX12_WARP_Developer_Launch
    COMMAND W3DDX12LaunchCheckpoint --warp --frames 12 --ci
)
set_tests_properties(W3D_DX12_WARP_Developer_Launch PROPERTIES TIMEOUT 90)

# B05: real WARP coverage for canonical world/terrain/water/effects/
# transparency/shroud/UI/postprocess order, offscreen state transitions,
# rejected aliasing/out-of-order passes, and GPU pixel readback.
add_executable(W3DDX12PassGraphSmoke DX12PassGraphSmoke.cpp)
target_compile_features(W3DDX12PassGraphSmoke PRIVATE cxx_std_17)
target_compile_definitions(W3DDX12PassGraphSmoke PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(W3DDX12PassGraphSmoke PRIVATE
    W3DDX12PresentationDevice user32
)
add_test(NAME W3D_DX12_WARP_Ordered_Render_Passes
    COMMAND W3DDX12PassGraphSmoke)
set_tests_properties(W3D_DX12_WARP_Ordered_Render_Passes PROPERTIES TIMEOUT 90)
