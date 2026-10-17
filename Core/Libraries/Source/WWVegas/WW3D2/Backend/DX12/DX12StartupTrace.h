/*
 * DX12-T01 game startup diagnostics.
 *
 * Developer-only native startup instrumentation. The T01 opt-in gameplay path
 * can fail during game subsystem initialization before a W3D scene is drawn.
 * Persist the first failing startup stage without relying on an attached
 * debugger. This file is not compiled by normal DX8 configurations.
 */
#pragma once

#if defined(W3D_RENDER_BACKEND_DX12) && defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cstdio>
#include <cstring>

namespace DX12StartupTrace
{
    // All trace text is an internal fixed stage label; never log command-line
    // arguments, user file paths, credentials or game profile contents.
    inline void Write(const char * stage)
    {
        if (!stage)
            return;
        char line[512] = {};
        const int size = std::snprintf(line, sizeof(line),
            "[DX12-T01 startup] pid=%lu tick=%lu %s\r\n",
            static_cast<unsigned long>(GetCurrentProcessId()),
            static_cast<unsigned long>(GetTickCount()), stage);
        if (size <= 0)
            return;
        OutputDebugStringA(line);

        char temp[MAX_PATH] = {};
        const DWORD length = GetTempPathA(MAX_PATH, temp);
        if (length == 0 || length >= MAX_PATH)
            return;
        char file[MAX_PATH + 48] = {};
        const int pathSize = std::snprintf(file, sizeof(file),
            "%sGeneralsDX12Startup.log", temp);
        if (pathSize <= 0 || static_cast<size_t>(pathSize) >= sizeof(file))
            return;

        HANDLE handle = CreateFileA(file, FILE_APPEND_DATA,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE)
            return;
        DWORD written = 0;
        const DWORD bytes = static_cast<DWORD>(std::strlen(line));
        WriteFile(handle, line, bytes, &written, nullptr);
        CloseHandle(handle);
    }

    inline void WriteWin32Failure(const char * stage)
    {
        const DWORD error = GetLastError();
        char message[288] = {};
        std::snprintf(message, sizeof(message),
            "%s (GetLastError=%lu)", stage ? stage : "Win32 startup failure",
            static_cast<unsigned long>(error));
        Write(message);
    }
}
#endif
