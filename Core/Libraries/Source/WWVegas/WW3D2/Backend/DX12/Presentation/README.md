# DX12 swapchain / presentation milestone

The reusable `DX12PresentationDevice` lives one directory above this test.
It creates a D3D12 device (WARP or a supported hardware adapter), direct queue,
double-buffered flip-discard DXGI swapchain bound to a real HWND, RTV heap,
per-buffer allocators, graphics command list, and GPU fences.

`BeginFrame` transitions the current buffer into render-target state and clears
it; native DX12 W3D draws can be appended to `ActiveCommandList()`.
`EndFrame` transitions to PRESENT, executes commands, calls IDXGISwapChain::Present
and records an individual back-buffer fence. `Resize` waits for GPU idle before
releasing buffers and recreating RTVs. Zero-sized resize notifications are
ignored until the window has a usable client area.

Windows / Visual Studio 2022 standalone test:

~~~powershell
cmake -S Core/Libraries/Source/WWVegas/WW3D2/Backend/DX12/Presentation -B build/w3d-dx12-presentation -G "Visual Studio 17 2022" -A x64
cmake --build build/w3d-dx12-presentation --config Release
ctest --test-dir build/w3d-dx12-presentation -C Release --output-on-failure
~~~

The test creates a native Win32 window and uses WARP to clear and present six
frames, read back a center pixel from each *swapchain buffer before presenting*,
recycle command allocators/fences and resize from 96x96 to 128x72.
CI tests both Win32 (the game's target architecture) and x64 on WARP.
Use `W3DDX12PresentationSmoke --hardware` locally to test a physical GPU.
The workflow publishes standalone Win32 and x64 smoke executables as artifacts.

The standalone test and optional `w3d_dx12_presentation` library share the
same `.cpp` implementation. The library is deliberately NOT selected as the
game's `Renderer` backend. There is no mesh/material submission or shipped
DX12 gameplay renderer yet: the DX8 backend remains authoritative.
