// DX12-B04: exercise the actual Renderer entry points against the primary
// game HWND on the Windows software adapter. Never link a D3D8 implementation.
#include "WW3D2/Renderer.h"
#include "WW3D2/Backend/DX12/DX12W3DTextureStages.h"
#include "WWMath/matrix4.h"
#include "WWMath/vector3.h"
#include "WWMath/vector4.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cmath>
#include <iostream>
#include <cstring>

namespace
{
    constexpr wchar_t ClassName[] = L"W3DDX12B04NativeRendererSmoke";

    struct GameWindow
    {
        HINSTANCE instance = GetModuleHandleW(nullptr);
        HWND hwnd = nullptr;
        bool registered = false;

        bool Create()
        {
            WNDCLASSW cls = {};
            cls.hInstance = instance;
            cls.lpfnWndProc = DefWindowProcW;
            cls.lpszClassName = ClassName;
            registered = RegisterClassW(&cls) != 0;
            if (!registered)
                return false;
            hwnd = CreateWindowExW(0, ClassName, L"W3D DX12 B04 native Renderer",
                WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                380, 300, nullptr, nullptr, instance, nullptr);
            if (!hwnd)
                return false;
            ShowWindow(hwnd, SW_SHOWNOACTIVATE);
            UpdateWindow(hwnd);
            return true;
        }

        void Pump() const
        {
            MSG message = {};
            while (PeekMessageW(&message, hwnd, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }

        ~GameWindow()
        {
            if (hwnd && IsWindow(hwnd))
                DestroyWindow(hwnd);
            if (registered)
                UnregisterClassW(ClassName, instance);
        }
    };

    bool Check(bool value, const char * operation)
    {
        if (!value)
            std::cerr << "B04 Renderer smoke FAILED at " << operation << "\\n";
        return value;
    }
}

int wmain()
{
    GameWindow game;
    if (!Check(game.Create(), "Create game-owned HWND"))
        return 1;

    SetEnvironmentVariableA("W3D_DX12_GAME_WARP", "1");
    SetEnvironmentVariableA("W3D_DX12_B04_DIAGNOSTIC", nullptr);
    if (!Check(!Renderer::Init(game.hwnd, false), "Normal gameplay fails closed") ||
        !Check(!Renderer::Has_Active_Device(), "No GPU retained after fail closed") ||
        !Check(IsWindow(game.hwnd) != FALSE, "Main HWND remains game owned"))
        return 1;

    SetEnvironmentVariableA("W3D_DX12_B04_DIAGNOSTIC", "1");
    if (!Check(!Renderer::Init(game.hwnd, true), "Headless is not a native game renderer") ||
        !Check(Renderer::Init(game.hwnd, false), "Persistent native renderer init") ||
        !Check(Renderer::Is_Initialized(), "Renderer initialized") ||
        !Check(Renderer::Has_Active_Device(), "Live WARP D3D12 device") ||
        !Check(Renderer::Get_Render_Thread_ID() == GetCurrentThreadId(), "Thread identity") ||
        !Check(Renderer::Get_Device_Status() == Renderer::DEVICE_READY, "DX12 ready") ||
        !Check(Renderer::Get_Render_Device_Count() == 1, "One adapter") ||
        !Check(Renderer::Get_Render_Device() == 0, "Selected adapter") ||
        !Check(std::strcmp(Renderer::Get_Render_Device_Name(0), "Direct3D 12") == 0,
               "Native adapter name"))
        return 1;

    RenderAdapterIdentification adapter;
    if (!Check(Renderer::Get_Adapter_Identification(adapter),
               "Read native DXGI adapter identity"))
        return 1;

    Matrix4x4 world(true);
    world[0][3] = 4.f;
    world[1][3] = -2.f;
    // M01: CPU transparency sorting must use D3DX's row-vector convention.
    // Translation followed by anisotropic scaling is intentionally
    // noncommutative, and the center test must not divide by W.
    RenderSortingMatrix sortWorld = {};
    RenderSortingMatrix sortView = {};
    for (unsigned i = 0; i < 4; ++i)
        sortWorld.m[i][i] = sortView.m[i][i] = 1.f;
    sortWorld.m[3][0] = 4.f;
    sortWorld.m[3][1] = 5.f;
    sortWorld.m[3][2] = 6.f;
    sortView.m[0][0] = 2.f;
    sortView.m[1][1] = 3.f;
    sortView.m[2][2] = 4.f;
    const auto sortCombined = Renderer::Multiply_Sorting_World_View(sortWorld, sortView);
    const auto sortCenter = Renderer::Transform_Sorting_Center(
        sortWorld, sortView, Vector3(1.f, 2.f, 3.f));
    if (!Check(std::fabs(sortCombined.m[3][0] - 8.f) < 1.e-5f &&
               std::fabs(sortCombined.m[3][1] - 15.f) < 1.e-5f &&
               std::fabs(sortCombined.m[3][2] - 24.f) < 1.e-5f &&
               std::fabs(sortCenter.X - 10.f) < 1.e-5f &&
               std::fabs(sortCenter.Y - 21.f) < 1.e-5f &&
               std::fabs(sortCenter.Z - 36.f) < 1.e-5f,
               "DX12 M01 sorted W3D transform order and center"))
        return 1;
    Renderer::Set_Transform(RENDER_TRANSFORM_WORLD, world);
    Matrix4x4 fetched(true);
    Renderer::Get_Transform(RENDER_TRANSFORM_WORLD, fetched);
    float raw[16] = {};
    Renderer::Get_Transform_Elements(RENDER_TRANSFORM_WORLD, raw);
    // Get_Transform returns Westwood convention; Get_Transform_Elements
    // returns the DX8-compatible row-vector transform stored on the GPU.
    if (!Check(std::fabs(fetched[0][3] - 4.f) < 0.001f &&
               std::fabs(raw[12] - 4.f) < 0.001f &&
               std::fabs(raw[13] + 2.f) < 0.001f &&
               std::fabs(raw[7]) < 0.001f, "Native transform round-trip"))
        return 1;
    Renderer::Set_World_Identity();
    Renderer::Get_Transform_Elements(RENDER_TRANSFORM_WORLD, raw);
    if (!Check(std::fabs(raw[3]) < 0.001f, "Reset world transform"))
        return 1;

    if (!Check(Renderer::Pack_Color_ARGB(Vector3(1.f, 0.f, 0.f), 1.f) ==
               0xFFFF0000u, "Color packing"))
        return 1;

    RenderViewport viewport = {0, 0, 0, 0, 0.f, 1.f};
    Renderer::Set_Viewport(viewport);
    Renderer::Set_Swap_Interval(1);
    if (!Check(Renderer::Get_Swap_Interval() == 1, "DXGI sync interval") ||
        !Check(!Renderer::Set_Device_Resolution(320, 200, 16, 1, true),
               "Reject unsupported 16-bit back buffer") ||
        !Check(!Renderer::Set_Render_Device(1, 320, 200, 32, 1, true, false, false),
               "Reject out-of-range adapter") ||
        !Check(!Renderer::Set_Device_Resolution(320, 200, 32, 0, true),
               "Reject unsupported exclusive fullscreen") ||
        !Check(Renderer::Set_Render_Device(0, 320, 200, 32, 1, true, false, false),
               "Resize native HWND and swap chain"))
        return 1;

    int width = 0, height = 0, bits = 0;
    bool windowed = false;
    Renderer::Get_Device_Resolution(width, height, bits, windowed);
    if (!Check(width == 320 && height == 200 && bits == 32 && windowed,
               "Real client size reported"))
        return 1;

    // R04: exercise the public Renderer texture APIs on the game-owned
    // HWND, not only the private DX12W3DTextures WARP test.
    RenderTexture2DDescription texDesc;
    texDesc.width = texDesc.height = 4;
    texDesc.pixelFormat = 2; // W3D A8R8G8B8
    texDesc.mipLevels = 3;
    const auto texture = Renderer::Create_Texture_2D(texDesc);
    const auto copiedTexture = Renderer::Create_Texture_2D(texDesc);
    if (!Check(texture && copiedTexture,
               "R04 native 2D texture handles created through Renderer") ||
        !Check(Renderer::Get_Texture_Mip_Count(
                   reinterpret_cast<RenderTextureHandle>(texture)) == 3,
               "Public Renderer reports native texture mip count"))
        return 1;
    RenderSurfaceMap textureMap;
    Renderer::Map_Texture_2D(texture, 0, textureMap);
    if (!Check(textureMap.data && textureMap.rowPitch == 16,
               "Renderer maps native DDS/TGA pixel staging rows"))
        return 1;
    for (unsigned y = 0; y < 4; ++y)
        std::memset(static_cast<unsigned char *>(textureMap.data) + y * textureMap.rowPitch,
                    0x56, 16);
    Renderer::Unmap_Texture_2D(texture, 0);
    Renderer::Update_Texture(reinterpret_cast<RenderTextureHandle>(texture),
                             reinterpret_cast<RenderTextureHandle>(copiedTexture));

    RenderTextureLevelDescription texLevel;
    Renderer::Describe_Texture_Level_2D(copiedTexture, 2, texLevel);
    if (!Check(texLevel.width == 1 && texLevel.height == 1,
               "Texture metadata survives native 2D update") ||
        !Check(Renderer::Supports_Texture_Format(20),
               "Native Renderer negotiates BC1/DXT1 compression"))
        return 1;

    RenderSurfaceDescription surfaceDesc;
    surfaceDesc.width = surfaceDesc.height = 4;
    surfaceDesc.pixelFormat = 2;
    const auto cpuSurface = Renderer::Create_Surface(surfaceDesc);
    RenderSurfaceMap cpuMap;
    Renderer::Lock_Surface(cpuSurface, cpuMap);
    if (!Check(cpuSurface && cpuMap.data && cpuMap.rowPitch == 16,
               "DX8-independent W3D CPU image surface lock"))
        return 1;
    std::memset(cpuMap.data, 0x88, 64);
    Renderer::Unlock_Surface(cpuSurface);
    const auto surfaceTexture = Renderer::Create_Texture_From_Surface(cpuSurface, 0);
    Renderer::Release_Surface(cpuSurface);
    if (!Check(surfaceTexture, "Renderer generates native texture/mips from surface"))
        return 1;

    RenderTextureCubeDescription cubeDesc;
    cubeDesc.edgeLength = 4;
    cubeDesc.pixelFormat = 2;
    cubeDesc.mipLevels = 2;
    const auto cubeTexture = Renderer::Create_Texture_Cube(cubeDesc);
    Renderer::Map_Texture_Cube(cubeTexture, 5, 1, textureMap);
    if (!Check(cubeTexture && textureMap.data && textureMap.rowPitch == 8,
               "Renderer maps final cubemap face and second mip"))
        return 1;
    std::memset(textureMap.data, 0x66, 16);
    Renderer::Unmap_Texture_Cube(cubeTexture, 5, 1);

    RenderTextureVolumeDescription volumeDesc;
    volumeDesc.width = volumeDesc.height = 4;
    volumeDesc.depth = 2;
    volumeDesc.pixelFormat = 2;
    volumeDesc.mipLevels = 2;
    const auto volumeTexture = Renderer::Create_Texture_Volume(volumeDesc);
    RenderVolumeMap volumeMap;
    Renderer::Map_Texture_Volume(volumeTexture, 0, volumeMap);
    if (!Check(volumeTexture && volumeMap.data &&
               volumeMap.rowPitch == 16 && volumeMap.slicePitch == 64,
               "Public Renderer maps a DX12 volume mip with slice stride"))
        return 1;
    std::memset(volumeMap.data, 0x77, 128);
    Renderer::Unmap_Texture_Volume(volumeTexture, 0);

    // Stage bindings may precede Begin_Scene. The DX12 backend queues GPU
    // uploads on the first real command list, without invoking DX8Wrapper.
    Renderer::Bind_Texture(0, reinterpret_cast<RenderTextureHandle>(texture));
    Renderer::Bind_Texture(1, reinterpret_cast<RenderTextureHandle>(surfaceTexture));

    // This call ordering mirrors WW3D::Begin_Render: Clear comes before
    // Begin_Scene. The separate primary-window test validates GPU pixels.
    // R01: these are the *production neutral Renderer methods*, not a
    // mock DX12 geometry API. Lock before scene, then bind native IA views.
    RenderBufferDescription bufferDesc;
    bufferDesc.byteSize = 36; // XYZ-only * 3
    bufferDesc.legacyVertexFormat = 0x002;
    auto gpuVertex = Renderer::Create_Vertex_Buffer(bufferDesc);
    RenderBufferDescription indexDesc;
    indexDesc.byteSize = 6; // three R16 indices
    auto gpuIndex = Renderer::Create_Index_Buffer(indexDesc);
    if (!Check(gpuVertex && gpuIndex, "Create native W3D vertex/index handles"))
        return 1;
    const float vertices[] = {0.f, 0.f, 0.f, 0.f, 0.5f, 0.f, 0.5f, 0.f, 0.f};
    const unsigned short indices[] = {0, 1, 2};
    void * vertexMap = Renderer::Map_Vertex_Buffer(gpuVertex, 0, 0);
    void * indexMap = Renderer::Map_Index_Buffer(gpuIndex, 0, 0);
    if (!Check(vertexMap && indexMap, "Map static W3D byte buffers"))
        return 1;
    std::memcpy(vertexMap, vertices, sizeof(vertices));
    std::memcpy(indexMap, indices, sizeof(indices));
    Renderer::Unmap_Vertex_Buffer(gpuVertex);
    Renderer::Unmap_Index_Buffer(gpuIndex);

    // R07: the public, backend-neutral W3D surface API must operate on
    // the same native command list as the game-owned primary HWND.
    RenderSurfaceDescription rttDesc;
    rttDesc.width = rttDesc.height = 64;
    rttDesc.pixelFormat = 2;
    const auto rtt = Renderer::Create_Offscreen_Texture(rttDesc);
    RenderTexture2DDescription depthTargetDesc;
    depthTargetDesc.width = depthTargetDesc.height = 64;
    depthTargetDesc.pixelFormat = 4; // WW3D_ZFORMAT_D24S8
    depthTargetDesc.mipLevels = 1;
    const auto depthRTT = Renderer::Create_Depth_Texture_2D(depthTargetDesc);
    if (!Check(rtt && depthRTT, "R07 Renderer creates GPU RTV and D24S8"))
        return 1;

    Renderer::Clear(true, true, Vector3(0.2f, 0.4f, 0.6f), 1.f);
    Renderer::Begin_Scene();
    const auto oldRT = Renderer::Acquire_Current_Color_Surface();
    const auto oldDepth = Renderer::Acquire_Current_Depth_Surface();
    const auto rttSurface = Renderer::Acquire_Texture_Surface_Level(rtt, 0);
    const auto dsvSurface = Renderer::Acquire_Texture_Surface_Level(depthRTT, 0);
    if (!Check(oldRT && oldDepth && rttSurface && dsvSurface,
               "R07 Renderer acquires primary and GPU texture surface views") ||
        !Check(Renderer::Set_Render_Target_Surfaces(rttSurface, dsvSurface),
               "R07 sets 64x64 offscreen target/depth through Renderer"))
        return 1;
    Renderer::Clear(true, true, Vector3(0.25f, 0.5f, 0.75f), 1.f, 0.33f, 7);
    RenderSurfaceDescription rttInfo;
    Renderer::Describe_Surface(rttSurface, rttInfo);
    if (!Check(rttInfo.width == 64 && rttInfo.height == 64,
               "R07 offscreen surface describes its GPU dimensions"))
        return 1;
    RenderSurfaceMap gpuMap;
    Renderer::Lock_Surface(rttSurface, gpuMap, nullptr, true);
    if (!Check(gpuMap.data && gpuMap.rowPitch == 64 * 4,
               "R08 GPU RTT read-only lock maps fenced D3D12 pixels"))
        return 1;
    const unsigned char * pixel = static_cast<const unsigned char *>(gpuMap.data);
    if (!Check(pixel[0] > 174 && pixel[0] < 209 &&
               pixel[1] > 110 && pixel[1] < 148 &&
               pixel[2] > 52 && pixel[2] < 84,
               "R08 BGRA8 color lock matches native RTV clear"))
        return 1;
    Renderer::Unlock_Surface(rttSurface);
    unsigned char centerBGRA[4] = {};
    if (!Check(Renderer::Read_Current_Color_Region(
                   centerBGRA, sizeof(centerBGRA), 32, 32, 1, 1) == 4 &&
               centerBGRA[0] > 174 && centerBGRA[1] > 110 && centerBGRA[2] > 52,
               "R08 Renderer screenshot region uses current offscreen GPU target"))
        return 1;
    const auto capturedRTT = Renderer::Create_Texture_From_Surface(rttSurface, 1);
    if (!Check(capturedRTT != nullptr, "R08 GPU surface to native texture conversion"))
        return 1;
    Renderer::Release_Texture(reinterpret_cast<RenderTextureHandle>(capturedRTT));
    const auto copiedRTT = Renderer::Create_Offscreen_Texture(rttInfo);
    if (!Check(copiedRTT != nullptr, "allocate R08 texture-copy destination"))
        return 1;
    Renderer::Update_Texture(
        reinterpret_cast<RenderTextureHandle>(rtt),
        reinterpret_cast<RenderTextureHandle>(copiedRTT));
    const auto copiedSurface = Renderer::Acquire_Texture_Surface_Level(copiedRTT, 0);
    if (!Check(copiedSurface != nullptr, "R08 copy GPU RTT to another RTT"))
        return 1;
    RenderSurfaceMap copiedMap;
    Renderer::Lock_Surface(copiedSurface, copiedMap, nullptr, true);
    if (!Check(copiedMap.data && copiedMap.rowPitch == 64 * 4,
               "R08 gpu-to-gpu Update_Texture preserves copied pixels"))
        return 1;
    const auto * copiedPixel = static_cast<const unsigned char *>(copiedMap.data);
    if (!Check(copiedPixel[0] > 174 && copiedPixel[1] > 110 &&
               copiedPixel[2] > 52,
               "R08 Update_Texture reads correct BGRA after GPU copy"))
        return 1;
    Renderer::Unlock_Surface(copiedSurface);
    Renderer::Release_Surface(copiedSurface);
    Renderer::Release_Texture(reinterpret_cast<RenderTextureHandle>(copiedRTT));
    if (!Check(Renderer::Set_Render_Target_Surfaces(nullptr, dsvSurface),
               "R07 Renderer supports a depth-only shadow-map pass"))
        return 1;
    auto depthOnlyColor = Renderer::Acquire_Current_Color_Surface();
    if (!Check(depthOnlyColor == nullptr, "Depth-only pass exposes no active color") ||
        !Check(DX12W3DRendererTextureStages().Bound(2) == nullptr,
               "Texture stage 2 starts unbound"))
        return 1;
    Renderer::Bind_Texture(2, reinterpret_cast<RenderTextureHandle>(depthRTT));
    if (!Check(DX12W3DRendererTextureStages().Bound(2) == nullptr,
               "R07 prevents binding currently writable depth as an SRV"))
        return 1;
    Renderer::Clear(false, true, Vector3(0.f, 0.f, 0.f), 1.f, 0.2f, 0xA5);
    if (!Check(Renderer::Set_Render_Target_Surfaces(oldRT, oldDepth),
               "R07 restores game HWND RTV and original viewport"))
        return 1;
    Renderer::Release_Surface(rttSurface);
    Renderer::Release_Surface(dsvSurface);
    Renderer::Release_Surface(oldRT);
    Renderer::Release_Surface(oldDepth);
    Renderer::Release_Texture(reinterpret_cast<RenderTextureHandle>(rtt));
    Renderer::Release_Texture(reinterpret_cast<RenderTextureHandle>(depthRTT));
    Renderer::Bind_Vertex_Buffer(gpuVertex, 0, 12);
    Renderer::Bind_Index_Buffer(gpuIndex, 0);
    Renderer::Clear(true, false, Vector3(0.3f, 0.5f, 0.7f), 1.f);
    Renderer::Release_Vertex_Buffer(gpuVertex); // fence pins in-flight D3D12 buffers
    Renderer::Release_Index_Buffer(gpuIndex);
    Renderer::End_Scene(false); // command list submission without presentation
    Renderer::Bind_Texture(0, nullptr);
    Renderer::Bind_Texture(1, nullptr);
    Renderer::Release_Texture(reinterpret_cast<RenderTextureHandle>(texture));
    Renderer::Release_Texture(reinterpret_cast<RenderTextureHandle>(copiedTexture));
    Renderer::Release_Texture(reinterpret_cast<RenderTextureHandle>(surfaceTexture));
    Renderer::Release_Texture(reinterpret_cast<RenderTextureHandle>(cubeTexture));
    Renderer::Release_Texture(reinterpret_cast<RenderTextureHandle>(volumeTexture));
    Renderer::Clear(true, true, Vector3(0.1f, 0.3f, 0.5f), 1.f);
    Renderer::Begin_Scene();
    Renderer::End_Scene(true);
    if (!Check(Renderer::Get_Device_Status() == Renderer::DEVICE_READY,
               "Ready after two native frames"))
        return 1;

    ShowWindow(game.hwnd, SW_MINIMIZE);
    game.Pump();
    if (!Check(Renderer::Get_Device_Status() == Renderer::DEVICE_LOST,
               "Minimized surface reports suspended") ||
        !Check(Renderer::Is_Device_Unavailable_For_Rendering(),
               "No rendering while suspended"))
        return 1;
    ShowWindow(game.hwnd, SW_RESTORE);
    game.Pump();
    if (!Check(Renderer::Get_Device_Status() == Renderer::DEVICE_READY,
               "Restore native swap chain"))
        return 1;

    Renderer::Shutdown();
    if (!Check(!Renderer::Is_Initialized() && !Renderer::Has_Active_Device(),
               "Shutdown releases DX12 resources") ||
        !Check(IsWindow(game.hwnd) != FALSE, "Renderer does not destroy HWND"))
        return 1;
    SetEnvironmentVariableA("W3D_DX12_B04_DIAGNOSTIC", nullptr);
    SetEnvironmentVariableA("W3D_DX12_GAME_WARP", nullptr);
    std::cout << "B04/R01/R04/R07 Renderer frame, native geometry/texture/RTV handles and game HWND: PASS\\n";
    return 0;
}
