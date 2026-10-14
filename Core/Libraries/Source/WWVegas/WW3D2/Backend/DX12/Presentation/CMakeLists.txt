cmake_minimum_required(VERSION 3.20)
project(W3DDX12Presentation LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "DX12 W3D presentation smoke test requires Windows")
endif()

# Compile the same backend-owned device implementation intended for WW3D2,
# not an independent reimplementation in the test harness.
add_library(W3DDX12PresentationDevice STATIC ../DX12PresentationDevice.cpp)
target_compile_features(W3DDX12PresentationDevice PUBLIC cxx_std_17)
target_compile_definitions(W3DDX12PresentationDevice PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(W3DDX12PresentationDevice PUBLIC d3d12 dxgi dxguid)

add_executable(W3DDX12PresentationSmoke DX12PresentationSmoke.cpp)
target_compile_features(W3DDX12PresentationSmoke PRIVATE cxx_std_17)
target_compile_definitions(W3DDX12PresentationSmoke PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(W3DDX12PresentationSmoke PRIVATE W3DDX12PresentationDevice user32)

enable_testing()
add_test(NAME W3D_DX12_WARP_Swapchain_Present
    COMMAND W3DDX12PresentationSmoke --warp)
set_tests_properties(W3D_DX12_WARP_Swapchain_Present PROPERTIES TIMEOUT 90)
