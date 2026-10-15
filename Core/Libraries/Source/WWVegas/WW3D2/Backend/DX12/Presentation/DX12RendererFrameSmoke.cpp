// DX12-B04: exercise the actual Renderer entry points against the primary
// game HWND on the Windows software adapter. Never link a D3D8 implementation.
#include "WW3D2/Renderer.h"
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
    Renderer::Set_Transform(RENDER_TRANSFORM_WORLD, world);
    Matrix4x4 fetched(true);
    Renderer::Get_Transform(RENDER_TRANSFORM_WORLD, fetched);
    float raw[16] = {};
    Renderer::Get_Transform_Elements(RENDER_TRANSFORM_WORLD, raw);
    if (!Check(std::fabs(fetched[0][3] - 4.f) < 0.001f &&
               std::fabs(raw[7] + 2.f) < 0.001f, "Native transform round-trip"))
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

    Renderer::Clear(true, true, Vector3(0.2f, 0.4f, 0.6f), 1.f);
    Renderer::Begin_Scene();
    Renderer::Bind_Vertex_Buffer(gpuVertex, 0, 12);
    Renderer::Bind_Index_Buffer(gpuIndex, 0);
    Renderer::Clear(true, false, Vector3(0.3f, 0.5f, 0.7f), 1.f);
    Renderer::Release_Vertex_Buffer(gpuVertex); // fence pins in-flight D3D12 buffers
    Renderer::Release_Index_Buffer(gpuIndex);
    Renderer::End_Scene(false); // command list submission without presentation
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
    std::cout << "B04/R01 Renderer frame, native geometry handles and game HWND: PASS\\n";
    return 0;
}
