# DX12 W3D geometry smoke probe

This is an isolated opt-in Direct3D 12 test executable. It is **not linked
into Generals or Zero Hour** and does not enable DX12 in gameplay.

To build and execute it on a Windows 10/11 PC with Windows SDK and MSVC:

```powershell
cmake -S Core/Libraries/Source/WWVegas/WW3D2/Backend/DX12/Probe -B build/w3d-dx12-probe -G "Visual Studio 17 2022" -A x64
cmake --build build/w3d-dx12-probe --config Release
ctest --test-dir build/w3d-dx12-probe -C Release --output-on-failure
```

The probe runs on the DX12 WARP software adapter. It creates an actual D3D12
device, direct command queue, allocator, command list, root signature,
graphics PSO, 64x64 RGBA8 render target, texture upload, vertex upload,
per-draw CBV, shader-visible SRV and sampler descriptor heaps, and fence.

It compiles the existing W3DLegacyUnlit.hlsl VS/PS shaders at runtime,
uploads a 1x1 colored texture, draws a three-vertex W3D-style triangle,
copies the GPU image to a readback buffer, and asserts the center pixel
matches the texture-modulated vertex color while the outside stays clear.

**Scope:** this is real D3D12 GPU rendering, but still an offscreen isolated
prototype. It does not have a swapchain, window integration, W3D mesh
streaming, device-lost recovery, or normal gameplay selection. Only the
legacy DX8 renderer ships in the actual game executable.

The probe loads shader source only in this developer test. A future
runtime DX12 backend must embed compiled shader bytecode; this test does
not add files to the game's deployment.

## Shader permutation isolation

The WARP regression was localized to dynamic combiner-key interpretation
(DXGI_ERROR_DEVICE_REMOVED). Compiling equivalent W3D combiner keys as shader
constants passed on the same Windows runner. Every W3D pixel shader probe now
requires `W3D_STATIC_COMBINER_KEY`. This is a shader permutation identifier
for the future material PSO cache; no production DX12 backend is active yet.

The probe validates clear/readback, geometry submission, W3D vertex shading,
texture sampling, an untextured material, a two-stage texture modulate
combiner, and alpha-test rejection with GPU pixel readback. Unsupported
permutations must be handled explicitly rather than silently substituted.
