/*
** Command & Conquer Generals Zero Hour(tm)
** Copyright 2026 TheSuperHackers
** Licensed under the GNU General Public License, version 3 or later.
*/

// DX12-B10: independently linked, native DX12-only developer launch.
//
// This executable uses the *same DX12Backend.cpp Renderer methods* as the
// exclusive game backend, and creates its own application-owned primary HWND.
// Unlike the main Generals/Zero Hour games, it does not initialize unported
// WW3D resource, UI, terrain, or DX8 subsystems. No D3D8 binary is linked.
//
// The compile-only W3D_DX12_DEVELOPER_CHECKPOINT gate is private to THIS
// target. It must never be set for the real game's Renderer translation unit.
#include "WW3D2/Renderer.h"
#include "WWMath/vector3.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cwchar>

namespace
{
    constexpr wchar_t WindowClassName[] = L"W3DDX12DeveloperLaunchCheckpoint";
    constexpr wchar_t WindowTitle[] =
        L"Generals / Zero Hour - Direct3D 12 developer checkpoint (no gameplay)";

    struct LaunchOptions
    {
        bool warp = false;
        bool ci = false;
        bool help = false;
        unsigned maxFrames = 0; // zero means interactive / until close
    };

    void Log(const char * message)
    {
        OutputDebugStringA("[DX12-B10] ");
        OutputDebugStringA(message);
        OutputDebugStringA("\n");
        std::fprintf(stderr, "[DX12-B10] %s\n", message);
        std::fflush(stderr);
    }

    int Fail(const char * message, bool ci)
    {
        Log(message);
        if (!ci)
            MessageBoxA(nullptr, message, "DX12-only developer launch failed",
                        MB_OK | MB_ICONERROR);
        return 1;
    }

    bool ParseCommandLine(LaunchOptions & options)
    {
        int count = 0;
        wchar_t ** args = CommandLineToArgvW(GetCommandLineW(), &count);
        if (!args)
            return false;
        bool valid = true;
        for (int i = 1; i < count; ++i)
        {
            if (std::wcscmp(args[i], L"--warp") == 0)
                options.warp = true;
            else if (std::wcscmp(args[i], L"--ci") == 0)
                options.ci = true;
            else if (std::wcscmp(args[i], L"--help") == 0)
                options.help = true;
            else if (std::wcscmp(args[i], L"--frames") == 0 && i + 1 < count)
            {
                wchar_t * end = nullptr;
                errno = 0;
                const unsigned long requested = std::wcstoul(args[++i], &end, 10);
                if (errno == ERANGE || *end || requested < 1 || requested > 60000)
                    valid = false;
                else
                    options.maxFrames = static_cast<unsigned>(requested);
            }
            else
                valid = false;
        }
        LocalFree(args);
        // Automated runs must exit, and must not depend on a physical GPU.
        return valid && (!options.ci || (options.warp && options.maxFrames > 0));
    }

    LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM w, LPARAM l)
    {
        switch (message)
        {
            case WM_CLOSE:
                DestroyWindow(window);
                return 0;
            case WM_DESTROY:
                PostQuitMessage(0);
                return 0;
            case WM_ERASEBKGND:
                return 1; // prevent GDI from repainting over the DX12 swap chain
            case WM_PAINT:
            {
                PAINTSTRUCT paint = {};
                BeginPaint(window, &paint);
                EndPaint(window, &paint);
                return 0;
            }
            case WM_KEYDOWN:
                if (w == VK_ESCAPE)
                {
                    DestroyWindow(window);
                    return 0;
                }
                break;
        }
        return DefWindowProcW(window, message, w, l);
    }

    struct ApplicationWindow
    {
        HINSTANCE instance = nullptr;
        HWND window = nullptr;
        bool registered = false;

        bool Create(HINSTANCE app)
        {
            instance = app;
            WNDCLASSW cls = {};
            cls.hInstance = instance;
            cls.lpfnWndProc = WindowProc;
            cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            cls.lpszClassName = WindowClassName;
            registered = RegisterClassW(&cls) != 0;
            if (!registered)
                return false;

            RECT outer = {0, 0, 960, 540};
            const DWORD style = WS_OVERLAPPEDWINDOW;
            if (!AdjustWindowRectEx(&outer, style, FALSE, 0))
                return false;
            window = CreateWindowExW(
                0, WindowClassName, WindowTitle, style,
                CW_USEDEFAULT, CW_USEDEFAULT,
                outer.right - outer.left, outer.bottom - outer.top,
                nullptr, nullptr, instance, nullptr);
            if (!window)
                return false;
            ShowWindow(window, SW_SHOW);
            UpdateWindow(window);
            return true;
        }

        ~ApplicationWindow()
        {
            if (window && IsWindow(window))
                DestroyWindow(window);
            if (registered)
                UnregisterClassW(WindowClassName, instance);
        }
    };

    bool D3D8IsAbsent()
    {
        // In combination with the CI PE import audit, this ensures that the
        // B10 process never constructs a D3D8 device through this checkpoint.
        return GetModuleHandleW(L"d3d8.dll") == nullptr &&
               GetModuleHandleW(L"d3dx8.dll") == nullptr;
    }
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    LaunchOptions options;
    if (!ParseCommandLine(options))
        return Fail("Usage: W3DDX12LaunchCheckpoint.exe [--warp] [--frames N] [--ci]", true);
    if (options.help)
    {
        Log("Use --warp for software GPU, --frames N to exit after N frames; "
            "--ci requires both --warp and --frames N. Escape closes the window.");
        if (!options.ci)
            MessageBoxW(nullptr,
                        L"Native DX12 checkpoint; NO Generals/Zero Hour gameplay.\n"
                        L"Use --warp to force the software adapter.\n"
                        L"Use --frames N to exit after N presented frames.\n"
                        L"Press Escape to close the interactive window.",
                        WindowTitle, MB_OK);
        return 0;
    }

    if (!D3D8IsAbsent())
        return Fail("Legacy d3d8/d3dx8 module loaded before native launch", options.ci);

    ApplicationWindow app;
    if (!app.Create(instance))
        return Fail("Failed to create the application-owned DX12 primary HWND", options.ci);

    // The Renderer entry point uses this developer option only to select
    // WARP; its compile-time B10 gate enables the native frame lifecycle.
    // This environment change is process-local and never modifies the host.
    SetEnvironmentVariableA("W3D_DX12_GAME_WARP", options.warp ? "1" : nullptr);
    if (!Renderer::Init(app.window, false))
        return Fail("Renderer::Init rejected DX12-only developer checkpoint", options.ci);

    int result = 0;
    if (!Renderer::Has_Active_Device() ||
        Renderer::Get_Device_Status() != Renderer::DEVICE_READY ||
        !D3D8IsAbsent())
    {
        result = Fail("Renderer did not initialize a DX12-only GPU device", options.ci);
    }
    else
    {
        RenderAdapterIdentification adapter;
        if (!Renderer::Get_Adapter_Identification(adapter))
            result = Fail("Unable to read the native DXGI adapter", options.ci);
        else
        {
            char report[180] = {};
            std::snprintf(report, sizeof(report),
                "Native DX12 adapter: vendor=0x%04X device=0x%04X; %s",
                adapter.vendorId, adapter.deviceId,
                options.warp ? "WARP software adapter" : "hardware adapter");
            Log(report);
        }
    }

    if (!result)
    {
        Renderer::Set_Swap_Interval(options.ci ? 0 : 1);
        RenderViewport viewport = {0, 0, 0, 0, 0.f, 1.f};
        Renderer::Set_Viewport(viewport);

        unsigned frames = 0;
        bool running = true;
        while (running)
        {
            MSG message = {};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
            {
                if (message.message == WM_QUIT)
                {
                    running = false;
                    break;
                }
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            if (!running)
                break;

            // B04 synchronizes resize/restoration via Get_Device_Status.
            const Renderer::DeviceStatus status = Renderer::Get_Device_Status();
            if (status == Renderer::DEVICE_LOST)
            {
                // A minimized or zero-sized window is suspended, not a
                // crashed device. Wait for window messages, then retry.
                MsgWaitForMultipleObjectsEx(0, nullptr, 50, QS_ALLINPUT,
                                            MWMO_INPUTAVAILABLE);
                continue;
            }
            if (status != Renderer::DEVICE_READY || !Renderer::Has_Active_Device() ||
                !D3D8IsAbsent())
            {
                result = Fail("Native device failed, or a DX8 module was loaded",
                              options.ci);
                break;
            }

            // Deterministic WARP-compatible clear that changes each frame.
            // W3D calls Clear before Begin_Scene, which B04 now supports.
            const float phase = static_cast<float>(frames % 120) / 120.f;
            Renderer::Clear(true, true, Vector3(0.06f + phase * 0.38f,
                                               0.12f + phase * 0.16f,
                                               0.30f + phase * 0.32f), 1.f);
            Renderer::Begin_Scene();
            Renderer::End_Scene(true);
            if (Renderer::Get_Device_Status() != Renderer::DEVICE_READY)
            {
                result = Fail("Native DX12 frame or DXGI Present failed", options.ci);
                break;
            }
            ++frames;
            if (options.maxFrames && frames >= options.maxFrames)
                break;
        }

        if (!result && options.ci && frames != options.maxFrames)
            result = Fail("CI exited before all native frames were presented", true);
        if (!result && (!options.ci || frames == options.maxFrames))
        {
            char report[120] = {};
            std::snprintf(report, sizeof(report),
                          "DX12-only developer launch: PASS (%u native frames; no D3D8 module)",
                          frames);
            Log(report);
        }
    }

    Renderer::Shutdown();
    if (!D3D8IsAbsent())
        result = Fail("DX8 module unexpectedly loaded before shutdown", options.ci);
    SetEnvironmentVariableA("W3D_DX12_GAME_WARP", nullptr);
    return result;
}
