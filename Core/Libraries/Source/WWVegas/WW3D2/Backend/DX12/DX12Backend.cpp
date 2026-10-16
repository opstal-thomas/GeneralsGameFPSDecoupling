/*
** Command & Conquer Generals Zero Hour(tm)
** Copyright 2026 TheSuperHackers
** Distributed under the GNU General Public License, v3 or later.
*/

// DX12-B04: native primary-window frame/state integration. The selected
// renderer never calls DX8Wrapper or creates a D3D8 device. The remaining
// WW3D resource and material methods are B07+ work, so the W3D executable
// must stay fail-closed unless the native-frame diagnostic is explicitly
// enabled. This is not a DX12-only playable-game launch (B10).
#include "WW3D2/Renderer.h"
#include "WW3D2/Backend/DX12/DX12PrimaryWindow.h"
#include "WW3D2/Backend/DX12/DX12W3DGeometryBuffers.h"
#include "WW3D2/Backend/DX12/DX12W3DNativeDraw.h"
#include "WW3D2/Backend/DX12/DX12W3DSortingBridge.h"
#include "WW3D2/Backend/DX12/DX12W3DTextures.h"
#include "WW3D2/Backend/DX12/DX12W3DTextureStages.h"
#include "WW3D2/Backend/DX12/DX12W3DSurfaces.h"
#include "WW3D2/Backend/DX12/DX12W3DRenderTargets.h"
#include "WWMath/vector3.h"
#include "WWMath/matrix4.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <cmath>

#if !defined(W3D_RENDER_BACKEND_DX12) || defined(W3D_RENDER_BACKEND_DX8)
#error "DX12Backend.cpp must be compiled exclusively for W3D_RENDER_BACKEND=DX12"
#endif

namespace
{
    DX12PrimaryWindow primaryWindow;
    bool ready = false;
    unsigned renderThread = 0;
    unsigned syncInterval = 0;
    DX12W3DTextureStages textureStages;
    DX12W3DRenderTargets renderTargets;
    DX12W3DNativeDraw nativeDraw;
    Matrix4x4 transforms[3] = {Matrix4x4(true), Matrix4x4(true), Matrix4x4(true)};
    Vector3 globalAmbient(0.f, 0.f, 0.f);

    bool ValidTransform(RenderTransform transform)
    {
        return transform >= RENDER_TRANSFORM_WORLD &&
               transform <= RENDER_TRANSFORM_PROJECTION;
    }

    void ReportPending(const char * operation)
    {
        char message[256] = {};
        std::snprintf(message, sizeof(message),
            "[DX12 backend] %s is unsupported until the native W3D resource/draw migration.\\n",
            operation);
        OutputDebugStringA(message);
    }

    void ReportFailure(const char * operation)
    {
        OutputDebugStringA("[DX12 backend] ");
        OutputDebugStringA(operation);
        OutputDebugStringA(": ");
        OutputDebugStringA(primaryWindow.LastError());
        OutputDebugStringA("\\n");
    }

    bool DiagnosticOptIn()
    {
#if defined(W3D_DX12_DEVELOPER_CHECKPOINT)
        // Only the separately built B10 developer executable defines this.
        // Normal Generals / Zero Hour DX12 builds remain fail-closed until
        // the rest of the WW3D resource and draw paths are native.
        return true;
#else
        char option[4] = {};
        return GetEnvironmentVariableA(
            "W3D_DX12_B04_DIAGNOSTIC", option, sizeof(option)) == 1 &&
            option[0] == '1';
#endif
    }

    unsigned PackChannel(float component)
    {
        if (!(component >= 0.f)) return 0;
        if (component >= 1.f) return 255;
        return static_cast<unsigned>(component * 255.f + 0.5f);
    }

    unsigned PackRGBA(float red, float green, float blue, float alpha)
    {
        return (PackChannel(alpha) << 24) | (PackChannel(red) << 16) |
               (PackChannel(green) << 8) | PackChannel(blue);
    }

    bool SetNativeResolution(int width, int height, int bits, int windowed,
                             bool resizeWindow)
    {
        if (!ready || primaryWindow.IsSceneOpen() ||
            renderThread != GetCurrentThreadId() ||
            (bits != -1 && bits != 32) ||
            (windowed != -1 && windowed != 0 && windowed != 1))
            return false;

        // DX12-B06 owns exclusive-fullscreen policy. B04 permits either a
        // decorated window or an already-borderless HWND, not style mutation.
        if (windowed != -1 &&
            (windowed == 0) != primaryWindow.IsBorderless())
            return false;

        auto & device = primaryWindow.Presentation();
        if (width == -1) width = static_cast<int>(device.Width());
        if (height == -1) height = static_cast<int>(device.Height());
        if (width <= 0 || height <= 0)
            return false;

        HWND hwnd = static_cast<HWND>(primaryWindow.Window());
        if (!hwnd || !IsWindow(hwnd))
            return false;
        if (resizeWindow)
        {
            const DWORD style = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_STYLE));
            const DWORD exStyle = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_EXSTYLE));
            RECT adjusted = {0, 0, width, height};
            if (!AdjustWindowRectEx(&adjusted, style, FALSE, exStyle) ||
                !SetWindowPos(hwnd, nullptr, 0, 0,
                              adjusted.right - adjusted.left,
                              adjusted.bottom - adjusted.top,
                              SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED))
                return false;
        }
        else
        {
            RECT client = {};
            if (!GetClientRect(hwnd, &client) ||
                client.right - client.left != width ||
                client.bottom - client.top != height)
                return false; // refuse a mismatched logical vs physical size
        }
        return primaryWindow.Synchronize() == DX12PrimaryWindow::Status::Ready;
    }
}

// The shared renderer stages are owned by the exclusive DX12 backend.
// R05's descriptor binding is intentionally separate from the old preview
// pass, and M01/B07 can call BindForDraw at native draw submission time.
DX12W3DTextureStages & DX12W3DRendererTextureStages()
{
    return textureStages;
}

bool Renderer::Init(void * window, bool lite)
{
    Shutdown();
    if (lite)
    {
        ReportPending("Renderer::Init: DX12 lite/headless support");
        return false;
    }
    char option[4] = {};
    const bool warp = GetEnvironmentVariableA(
        "W3D_DX12_GAME_WARP", option, sizeof(option)) == 1 && option[0] == '1';
    if (!primaryWindow.Attach(window, warp))
    {
        ReportFailure("Renderer::Init");
        primaryWindow.Shutdown();
        return false;
    }
    if (!DiagnosticOptIn())
    {
        // B04 is a native frame/state checkpoint. B10 will establish the
        // game-launch checkpoint after the unported W3D callers are gated.
        ReportPending("Renderer::Init: set W3D_DX12_B04_DIAGNOSTIC=1 for frame diagnostic");
        primaryWindow.Shutdown();
        return false;
    }

    nativeDraw.Reset();
    ready = true;
    renderThread = GetCurrentThreadId();
    syncInterval = 0;
    for (auto & matrix : transforms) matrix.Make_Identity();
    return true;
}

void Renderer::Shutdown()
{
    // Stage references may outlive a native frame; keep texture identity
    // independent of graphics device reset, then drop stage caches.
    textureStages.Clear();
    renderTargets.Reset();
    nativeDraw.Reset();
    ready = false;
    renderThread = 0;
    syncInterval = 0;
    primaryWindow.Shutdown();
    for (auto & matrix : transforms) matrix.Make_Identity();
}

bool Renderer::Is_Initialized()
{
    return ready;
}

unsigned Renderer::Get_Render_Thread_ID()
{
    return renderThread;
}

bool Renderer::Has_Active_Device()
{
    return ready && primaryWindow.Presentation().IsDeviceHealthy();
}

bool Renderer::Is_Device_Unavailable_For_Rendering()
{
    return Get_Device_Status() != DEVICE_READY;
}

bool Renderer::Is_Debug_Console_Disabled()
{
    ReportPending("Renderer::Is_Debug_Console_Disabled");
    return {};
}

void Renderer::Set_Gamma(float gamma, float bright, float contrast, bool calibrate, bool uselimit)
{
    ReportPending("Renderer::Set_Gamma");
}

void Renderer::Set_Hardware_Cursor_Visible(bool visible)
{
    ReportPending("Renderer::Set_Hardware_Cursor_Visible");
}

bool Renderer::Set_Hardware_Cursor_Image(unsigned hotspotX, unsigned hotspotY, RenderSurfaceHandle surface)
{
    ReportPending("Renderer::Set_Hardware_Cursor_Image");
    return {};
}

void Renderer::Set_Hardware_Cursor_Position(int x, int y)
{
    ReportPending("Renderer::Set_Hardware_Cursor_Position");
}

void Renderer::Begin_Scene()
{
    if (!ready || renderThread != GetCurrentThreadId() ||
        primaryWindow.IsSceneOpen())
        return;
    if (!primaryWindow.BeginScene())
    {
        if (primaryWindow.CurrentStatus() == DX12PrimaryWindow::Status::Failed)
        {
            ReportFailure("Renderer::Begin_Scene");
            ready = false;
        }
        return;
    }
    nativeDraw.BeginFrame();
    if (!renderTargets.BeginFrame(primaryWindow.Presentation()))
    {
        OutputDebugStringA(renderTargets.LastError());
        return;
    }
    // The native stage cache pins any modified TGA/DDS mip generations
    // once per command list. Actual shader-visible root-table binding is
    // performed by the native draw pass after its root signature is set.
    if (!textureStages.PrepareBound(primaryWindow.Presentation()))
        OutputDebugStringA(textureStages.LastError());
}

void Renderer::End_Scene(bool flip_frame)
{
    if (!primaryWindow.IsSceneOpen())
        return; // minimized window: BeginScene intentionally skipped recording
    if (!renderTargets.EndFrame(primaryWindow.Presentation()))
        OutputDebugStringA(renderTargets.LastError());
    if (!primaryWindow.EndScene(flip_frame, syncInterval))
    {
        ReportFailure("Renderer::End_Scene");
        ready = false;
    }
}

void Renderer::Flip_To_Primary()
{
    // DXGI windowed / borderless flip-discard has no fullscreen buffer
    // rotation equivalent. End_Scene(true) is the sole Present owner.
}

void Renderer::Clear(bool clear_color, bool clear_z_stencil,
                     const Vector3 & color, float dest_alpha,
                     float z, unsigned int stencil)
{
    const float rgba[4] = {color.X, color.Y, color.Z, dest_alpha};
    if (!ready) return;
    if (primaryWindow.IsSceneOpen())
    {
        // Once R07 has opened the command list, all output clears use its
        // currently bound RTV/DSV views, even for color-only primary targets.
        // The primary-window path below remains the pre-BeginScene clear
        // queue; it must not accidentally clear an unbound primary DSV.
        if (!renderTargets.Clear(primaryWindow.Presentation(), clear_color,
                                 clear_z_stencil, rgba, z, stencil))
            OutputDebugStringA(renderTargets.LastError());
        return;
    }
    if (!primaryWindow.Clear(clear_color, clear_z_stencil, rgba, z))
    {
        ReportFailure("Renderer::Clear");
        if (primaryWindow.CurrentStatus() == DX12PrimaryWindow::Status::Failed)
            ready = false;
    }
}

void Renderer::Set_Viewport(const RenderViewport & viewport)
{
    if (ready && !primaryWindow.SetViewport(viewport.x, viewport.y,
        viewport.width, viewport.height, viewport.min_z, viewport.max_z))
        ReportFailure("Renderer::Set_Viewport");
}

void Renderer::Invalidate_Cached_Render_States()
{
    // The B04 viewport is reapplied by BeginScene. PSO and resource caches
    // are added by B07, so there is nothing else to invalidate yet.
}

unsigned Renderer::Pack_Color_ARGB(const Vector3 & color, float alpha)
{
    return PackRGBA(color.X, color.Y, color.Z, alpha);
}

unsigned Renderer::Pack_Color_ARGB(const Vector4 & color)
{
    return PackRGBA(color.X, color.Y, color.Z, color.W);
}

unsigned Renderer::Pack_Color_ARGB_Clamp(const Vector4 & color)
{
    return PackRGBA(color.X, color.Y, color.Z, color.W);
}

bool Renderer::Is_DX12_Gameplay_Preview_Recording()
{
    ReportPending("Renderer::Is_DX12_Gameplay_Preview_Recording");
    return {};
}

bool Renderer::Snapshot_DX12_Preview_Texture_RGBA8( RenderTextureHandle texture, unsigned char * rgba8, unsigned capacity, unsigned & width, unsigned & height)
{
    (void)texture; (void)rgba8; (void)capacity;
    width = height = 0; return false;
}

void Renderer::Preview_DX12_Gameplay_Mesh( const RenderGameplayPreviewVertex * vertices, unsigned vertexCount, const unsigned short * indices, unsigned indexCount, const Matrix3D & modelTransform, RenderTextureHandle texture, bool alphaTest, bool alphaBlend, RenderGameplayPreviewKind kind, unsigned firstIndex, unsigned drawIndexCount)
{
    ReportPending("Renderer::Preview_DX12_Gameplay_Mesh");
}

namespace
{
    bool SubmitGameTriangles(bool strip, unsigned startIndex, unsigned primitiveCount,
                             unsigned minVertex, unsigned vertexCount)
    {
        if (!ready || renderThread != GetCurrentThreadId() || !primaryWindow.IsSceneOpen())
            return false;
        // Row-major W3D matrix convention matches the native SM5.1 shader's
        // mul(vertex, World*View*Projection) convention.
        const Matrix4x4 & w = transforms[RENDER_TRANSFORM_WORLD];
        const Matrix4x4 & v = transforms[RENDER_TRANSFORM_VIEW];
        const Matrix4x4 & p = transforms[RENDER_TRANSFORM_PROJECTION];
        float worldView[4][4] = {};
        float wvp[16] = {};
        for (unsigned row = 0; row < 4; ++row)
            for (unsigned column = 0; column < 4; ++column)
                for (unsigned inner = 0; inner < 4; ++inner)
                    worldView[row][column] += w[row][inner] * v[inner][column];
        for (unsigned row = 0; row < 4; ++row)
            for (unsigned column = 0; column < 4; ++column)
                for (unsigned inner = 0; inner < 4; ++inner)
                    wvp[row * 4 + column] += worldView[row][inner] * p[inner][column];

        float normalWorld[16] = {};
        normalWorld[15] = 1.f;
        // adjugate-transpose / det of world.upper3x3, for n(row) * N
        const float a = w[0][0], b = w[0][1], c = w[0][2];
        const float d = w[1][0], e = w[1][1], f = w[1][2];
        const float g = w[2][0], h = w[2][1], i = w[2][2];
        const float cof[9] = {
            e*i-f*h, f*g-d*i, d*h-e*g,
            c*h-b*i, a*i-c*g, b*g-a*h,
            b*f-c*e, c*d-a*f, a*e-b*d
        };
        const float determinant = a*cof[0] + b*cof[1] + c*cof[2];
        if (std::isfinite(determinant) && std::fabs(determinant) > 1.e-8f)
        {
            for (unsigned row = 0; row < 3; ++row)
                for (unsigned column = 0; column < 3; ++column)
                    normalWorld[row * 4 + column] = cof[row * 3 + column] / determinant;
        }
        else
            normalWorld[0] = normalWorld[5] = normalWorld[10] = 1.f;

        if (!nativeDraw.Draw(primaryWindow.Presentation(), renderTargets, textureStages,
                             wvp, normalWorld, strip, startIndex, primitiveCount,
                             minVertex, vertexCount))
        {
            OutputDebugStringA("[DX12 M01] Native gameplay indexed draw rejected: ");
            OutputDebugStringA(nativeDraw.LastError());
            OutputDebugStringA("\n");
            return false;
        }
        return true;
    }
}

void Renderer::Draw_Triangles(unsigned short startIndex, unsigned short triangleCount,
                              unsigned short minVertexIndex, unsigned short vertexCount)
{
    SubmitGameTriangles(false, startIndex, triangleCount, minVertexIndex, vertexCount);
}

void Renderer::Draw_Triangle_Strip(unsigned short startIndex, unsigned short primitiveCount,
                                   unsigned short minVertexIndex, unsigned short vertexCount)
{
    SubmitGameTriangles(true, startIndex, primitiveCount, minVertexIndex, vertexCount);
}

unsigned DX12W3DSortingCurrentBaseVertexIndex()
{
    return ready && primaryWindow.IsSceneOpen() &&
        renderThread == GetCurrentThreadId() ?
        primaryWindow.Presentation().BoundBaseVertexIndex() : 0;
}

RenderSortingMatrix Renderer::Multiply_Sorting_World_View( const RenderSortingMatrix & world, const RenderSortingMatrix & view)
{
    RenderSortingMatrix result = {};
    // These snapshots use the original D3DX row-vector convention:
    // the translation is in m[3][0..2], and World * View is the order.
    for (unsigned row = 0; row < 4; ++row)
        for (unsigned column = 0; column < 4; ++column)
            for (unsigned inner = 0; inner < 4; ++inner)
                result.m[row][column] += world.m[row][inner] * view.m[inner][column];
    return result;
}

Vector3 Renderer::Transform_Sorting_Center( const RenderSortingMatrix & world, const RenderSortingMatrix & view, const Vector3 & center)
{
    const RenderSortingMatrix m = Multiply_Sorting_World_View(world, view);
    // D3DXVec3Transform returns the first three *homogeneous* components.
    // It deliberately does not perform a perspective divide.
    return Vector3(
        center.X * m.m[0][0] + center.Y * m.m[1][0] +
            center.Z * m.m[2][0] + m.m[3][0],
        center.X * m.m[0][1] + center.Y * m.m[1][1] +
            center.Z * m.m[2][1] + m.m[3][1],
        center.X * m.m[0][2] + center.Y * m.m[1][2] +
            center.Z * m.m[2][2] + m.m[3][2]);
}

bool Renderer::Is_Triangle_Draw_Enabled()
{
    return nativeDraw.IsDrawEnabled();
}

void Renderer::Set_Triangle_Draw_Enabled(bool enabled)
{
    nativeDraw.SetDrawEnabled(enabled);
}

void Renderer::Unbind_Sorting_Geometry()
{
    Bind_Index_Buffer(nullptr, 0);
    Bind_Vertex_Buffer(nullptr, 0, 0);
}

void Renderer::Draw_Transformed_Quad(const void * vertices, unsigned vertexStride, unsigned uvSets)
{
    ReportPending("Renderer::Draw_Transformed_Quad");
}

void Renderer::Bind_Immediate_Texture(unsigned stage, RenderTextureHandle texture)
{
    // Immediate/sorted W3D geometry previously bypassed the DX8Wrapper
    // texture cache. DX12 uses the same strongly retained native stage
    // descriptors; its actual immediate draw setup remains M01/B07.
    Bind_Texture(stage, texture);
}

void Renderer::Set_Immediate_Stage_Alpha_Current(unsigned stage)
{
    ReportPending("Renderer::Set_Immediate_Stage_Alpha_Current");
}

void Renderer::Set_Immediate_Alpha_Blending(bool enabled)
{
    ReportPending("Renderer::Set_Immediate_Alpha_Blending");
}

bool Renderer::Create_Legacy_Shader(bool vertexShader, const void * declaration, const void * bytecode, unsigned usage, unsigned & handle)
{
    (void)vertexShader; (void)declaration; (void)bytecode; (void)usage;
    handle = 0; ReportPending("Renderer::Create_Legacy_Shader"); return false;
}

bool Renderer::Select_Legacy_Pixel_Shader(unsigned handle)
{
    ReportPending("Renderer::Select_Legacy_Pixel_Shader");
    return {};
}

void Renderer::Delete_Legacy_Pixel_Shader(unsigned handle)
{
    ReportPending("Renderer::Delete_Legacy_Pixel_Shader");
}

void Renderer::Set_Legacy_Pixel_Shader_Vector(unsigned registerIndex, float x, float y, float z, float w)
{
    ReportPending("Renderer::Set_Legacy_Pixel_Shader_Vector");
}

void Renderer::Apply_Pending_State_Changes()
{
    // Native PSO and texture descriptors are resolved at each draw submission;
    // there is no DX8 deferred state cache to flush here.
}

void Renderer::Set_Transform(RenderTransform transform, const Matrix3D & matrix)
{
    if (ValidTransform(transform)) transforms[transform] = Matrix4x4(matrix);
}

void Renderer::Set_Transform(RenderTransform transform, const Matrix4x4 & matrix)
{
    if (ValidTransform(transform)) transforms[transform] = matrix;
}

void Renderer::Get_Transform(RenderTransform transform, Matrix4x4 & matrix)
{
    matrix = ValidTransform(transform) ? transforms[transform] : Matrix4x4(true);
}

void Renderer::Set_Texture_Transform(unsigned stage, const float matrix[16])
{
    ReportPending("Renderer::Set_Texture_Transform");
}

void Renderer::Get_Transform_Elements(RenderTransform transform, float matrix[16])
{
    const Matrix4x4 & value = ValidTransform(transform) ? transforms[transform] : transforms[0];
    for (unsigned row = 0; row < 4; ++row)
        for (unsigned column = 0; column < 4; ++column)
            matrix[row * 4 + column] = value[row][column];
}

void Renderer::Set_World_Identity()
{
    transforms[RENDER_TRANSFORM_WORLD].Make_Identity();
}

void Renderer::Set_Ambient(const Vector3 & color)
{
    globalAmbient = color;
    nativeDraw.SetAmbient(color.X, color.Y, color.Z);
}

// The standalone WARP backend never depends on the WW3D light classes.
// The complete typed adapter in dx8renderer.cpp translates game objects.
void DX12W3DApplyLightingEnvironment(const RenderLightingEnvironmentDescription & lighting)
{
    nativeDraw.SetLightingEnvironment(lighting);
}

RenderLightingEnvironmentDescription DX12W3DCurrentLightingEnvironment()
{
    return nativeDraw.GetLightingEnvironment();
}

RenderLightingEnvironmentDescription DX12W3DDefaultLightingEnvironment()
{
    RenderLightingEnvironmentDescription lighting;
    lighting.ambient[0] = globalAmbient.X;
    lighting.ambient[1] = globalAmbient.Y;
    lighting.ambient[2] = globalAmbient.Z;
    return lighting;
}

bool Renderer::Set_Render_Device(const char * name, int width, int height, int bits, int windowed, bool resize_window)
{
    if (name && std::strcmp(name, "DX12") != 0 &&
        std::strcmp(name, "Direct3D 12") != 0)
        return false;
    return Set_Render_Device(0, width, height, bits, windowed, resize_window, false, false);
}

bool Renderer::Set_Render_Device(int device, int width, int height, int bits, int windowed, bool resize_window, bool reset_device, bool restore_assets)
{
    (void)reset_device;
    (void)restore_assets;
    return device == 0 && SetNativeResolution(width, height, bits, windowed, resize_window);
}

bool Renderer::Set_Any_Render_Device()
{
    return Has_Active_Device();
}

bool Renderer::Set_Next_Render_Device()
{
    return false; // exactly one selected native adapter
}

int Renderer::Get_Render_Device()
{
    return ready ? 0 : -1;
}

int Renderer::Get_Render_Device_Count()
{
    return ready ? 1 : 0;
}

const char * Renderer::Get_Render_Device_Name(int device_index)
{
    return device_index == 0 && ready ? "Direct3D 12" : "";
}

bool Renderer::Is_Windowed()
{
    return primaryWindow.Window() && !primaryWindow.IsBorderless();
}

bool Renderer::Toggle_Windowed()
{
    ReportPending("Renderer::Toggle_Windowed");
    return {};
}

bool Renderer::Set_Device_Resolution(int width, int height, int bits, int windowed, bool resize_window)
{
    return SetNativeResolution(width, height, bits, windowed, resize_window);
}

void Renderer::Get_Device_Resolution(int & width, int & height, int & bits, bool & windowed)
{
    width = static_cast<int>(primaryWindow.Presentation().Width());
    height = static_cast<int>(primaryWindow.Presentation().Height());
    bits = 32;
    windowed = !primaryWindow.IsBorderless();
}

void Renderer::Get_Render_Target_Resolution(int & width, int & height, int & bits, bool & windowed)
{
    Get_Device_Resolution(width, height, bits, windowed);
}

void Renderer::Set_Swap_Interval(long swap)
{
    syncInterval = static_cast<unsigned>((std::max)(0L, (std::min)(swap, 4L)));
}

int Renderer::Get_Swap_Interval()
{
    return static_cast<int>(syncInterval);
}

Renderer::DeviceStatus Renderer::Get_Device_Status()
{
    if (!ready)
        return DEVICE_UNAVAILABLE;
    if (!primaryWindow.IsSceneOpen())
        primaryWindow.Synchronize(); // permit recovery after minimize/restore
    if (primaryWindow.CurrentStatus() == DX12PrimaryWindow::Status::Suspended)
        return DEVICE_LOST;
    if (primaryWindow.CurrentStatus() == DX12PrimaryWindow::Status::Failed)
        return DEVICE_ERROR;
    return Has_Active_Device() ? DEVICE_READY : DEVICE_ERROR;
}

bool Renderer::Reset_Device()
{
    return ready && !primaryWindow.IsSceneOpen() &&
        primaryWindow.Synchronize() == DX12PrimaryWindow::Status::Ready;
}

void Renderer::Set_Polygon_Mode(PolygonMode mode)
{
    ReportPending("Renderer::Set_Polygon_Mode");
}

bool Renderer::Get_Adapter_Identification(RenderAdapterIdentification & identification)
{
    identification = RenderAdapterIdentification();
    return ready && primaryWindow.Presentation().GetAdapterIdentity(
        identification.vendorId, identification.deviceId, identification.driverVersion);
}

RenderCapabilities Renderer::Get_Capabilities()
{
    RenderCapabilities caps;
    if (Has_Active_Device())
    {
        // W3D's existing texture loader uses these values to round/clamp
        // input dimensions. Zeros would collapse valid maps to 0x0.
        caps.maxTextureWidth = D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION;
        caps.maxTextureHeight = D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION;
        caps.maxVolumeExtent = D3D12_REQ_TEXTURE3D_U_V_OR_W_DIMENSION;
        caps.maxTextureAspectRatio = D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION;
        caps.cubeTextures = true;
        caps.textureCompression = true;
        caps.maxTexturesPerPass = 2; // W3D legacy two-stage material combiner.
        caps.maxSimultaneousTextures = 2;
        // Do not advertise unported legacy fixed-function stage/sampler
        // capabilities merely because D3D12 hardware can support them.
    }
    return caps;
}

void Renderer::Release_Texture_Resources()
{
    textureStages.Clear();
    // The fence retains old immutable descriptor pages and resource
    // generations while any previously recorded GPU draw is in flight.
}

RenderVertexBufferHandle Renderer::Create_Vertex_Buffer(const RenderBufferDescription & description)
{
    if (!Has_Active_Device() || renderThread != GetCurrentThreadId())
        return nullptr;
    return DX12W3DGeometryBuffers::CreateVertex(
        primaryWindow.Presentation().NativeDevice(), description);
}

RenderVertexBufferHandle Renderer::Create_Point_Sprite_Vertex_Buffer(unsigned byteSize)
{
    RenderBufferDescription description;
    description.byteSize = byteSize;
    description.legacyVertexFormat = 0x002; // historical W3D XYZ-only FVF identifier
    description.usageFlags = RENDER_BUFFER_DYNAMIC;
    return Create_Vertex_Buffer(description);
}

RenderIndexBufferHandle Renderer::Create_Index_Buffer(const RenderBufferDescription & description)
{
    if (!Has_Active_Device() || renderThread != GetCurrentThreadId())
        return nullptr;
    return DX12W3DGeometryBuffers::CreateIndex(
        primaryWindow.Presentation().NativeDevice(), description);
}

void Renderer::Release_Vertex_Buffer(RenderVertexBufferHandle buffer)
{
    DX12W3DGeometryBuffers::ReleaseVertex(buffer);
}

void Renderer::Release_Index_Buffer(RenderIndexBufferHandle buffer)
{
    DX12W3DGeometryBuffers::ReleaseIndex(buffer);
}

void Renderer::Discard_Managed_Resources()
{
    // No D3D8 managed pool. Retired DX12 generations remain alive through
    // native submission fences, not an implicit legacy resource manager.
}

void * Renderer::Map_Vertex_Buffer(RenderVertexBufferHandle buffer,
                                   unsigned offsetBytes, unsigned sizeBytes, unsigned mapFlags)
{
    if (!ready || renderThread != GetCurrentThreadId())
        return nullptr;
    return DX12W3DGeometryBuffers::MapVertex(buffer, offsetBytes, sizeBytes, mapFlags);
}

void * Renderer::Map_Index_Buffer(RenderIndexBufferHandle buffer,
                                  unsigned offsetBytes, unsigned sizeBytes, unsigned mapFlags)
{
    if (!ready || renderThread != GetCurrentThreadId())
        return nullptr;
    return DX12W3DGeometryBuffers::MapIndex(buffer, offsetBytes, sizeBytes, mapFlags);
}

void Renderer::Unmap_Vertex_Buffer(RenderVertexBufferHandle buffer)
{
    if (buffer && !DX12W3DGeometryBuffers::UnmapVertex(buffer))
        OutputDebugStringA(DX12W3DGeometryBuffers::LastError());
}

void Renderer::Unmap_Index_Buffer(RenderIndexBufferHandle buffer)
{
    if (buffer && !DX12W3DGeometryBuffers::UnmapIndex(buffer))
        OutputDebugStringA(DX12W3DGeometryBuffers::LastError());
}

void Renderer::Bind_Vertex_Buffer(RenderVertexBufferHandle buffer, unsigned stream, unsigned stride)
{
    if (!ready || !primaryWindow.IsSceneOpen())
        return;
    if (!DX12W3DGeometryBuffers::BindVertex(primaryWindow.Presentation(), buffer, stream, stride))
    {
        if (stream == 0) nativeDraw.BindVertex(nullptr, 0);
        OutputDebugStringA(DX12W3DGeometryBuffers::LastError());
        return;
    }
    if (stream == 0)
        nativeDraw.BindVertex(buffer ? DX12W3DGeometryBuffers::NativeVertexLayout(buffer) : nullptr,
                              buffer && stride ? DX12W3DGeometryBuffers::VertexByteSize(buffer) / stride : 0);
}

void Renderer::Bind_Point_Sprite_Vertex_Buffer(RenderVertexBufferHandle buffer, unsigned stride)
{
    Bind_Vertex_Buffer(buffer, 0, stride);
}

void Renderer::Draw_Point_List(unsigned firstVertex, unsigned pointCount)
{
    ReportPending("Renderer::Draw_Point_List");
}

void Renderer::Set_Point_Sprite_Rendering(bool enabled, float size, float minSize, float maxSize)
{
    ReportPending("Renderer::Set_Point_Sprite_Rendering");
}

void Renderer::Bind_Index_Buffer(RenderIndexBufferHandle buffer, unsigned baseVertexIndex)
{
    if (!ready || !primaryWindow.IsSceneOpen())
        return;
    if (!DX12W3DGeometryBuffers::BindIndex(primaryWindow.Presentation(),
                                            buffer, baseVertexIndex))
    {
        nativeDraw.BindIndex(0);
        OutputDebugStringA(DX12W3DGeometryBuffers::LastError());
        return;
    }
    nativeDraw.BindIndex(buffer ?
        DX12W3DGeometryBuffers::IndexByteSize(buffer) /
        DX12W3DGeometryBuffers::NativeIndexElementBytes(buffer) : 0);
}

void Renderer::Bind_Dynamic_Vertex_Buffer(const RenderDynamicVertexBinding & binding)
{
    if (!ready || !primaryWindow.IsSceneOpen())
        return;
    if (!binding.resource)
    {
        const unsigned long long bytes =
            static_cast<unsigned long long>(binding.stride) * binding.vertexCount;
        DX12W3DVertexLayout decoded;
        if (!binding.cpuData || !binding.vertexCount || !binding.stride ||
            bytes > UINT_MAX ||
            !DX12W3DVertexLayouts::DecodeLegacyFVF(binding.legacyVertexFormat, decoded) ||
            !DX12W3DGeometryBuffers::BindTransientVertex(primaryWindow.Presentation(),
                binding.cpuData, static_cast<unsigned>(bytes), binding.stride,
                binding.legacyVertexFormat))
        {
            nativeDraw.BindVertex(nullptr, 0);
            OutputDebugStringA(DX12W3DGeometryBuffers::LastError());
        }
        else nativeDraw.BindVertex(&decoded, binding.vertexCount);
        return;
    }
    if (!DX12W3DGeometryBuffers::BindVertex(primaryWindow.Presentation(),
        binding.resource, 0, binding.stride, binding.firstVertex, binding.vertexCount))
    {
        nativeDraw.BindVertex(nullptr, 0);
        OutputDebugStringA(DX12W3DGeometryBuffers::LastError());
    }
    else nativeDraw.BindVertex(DX12W3DGeometryBuffers::NativeVertexLayout(binding.resource),
                               binding.vertexCount);
}

void Renderer::Bind_Dynamic_Index_Buffer(const RenderDynamicIndexBinding & binding, unsigned short baseVertexIndex)
{
    if (!ready || !primaryWindow.IsSceneOpen())
        return;
    if (!binding.resource)
    {
        const unsigned bytes = static_cast<unsigned>(binding.indexCount) * 2u;
        if (!binding.cpuData || !binding.indexCount ||
            !DX12W3DGeometryBuffers::BindTransientIndex(primaryWindow.Presentation(),
                binding.cpuData, bytes, baseVertexIndex))
        {
            nativeDraw.BindIndex(0);
            OutputDebugStringA(DX12W3DGeometryBuffers::LastError());
        }
        else nativeDraw.BindIndex(binding.indexCount);
        return;
    }
    if (!DX12W3DGeometryBuffers::BindIndex(primaryWindow.Presentation(),
        binding.resource, baseVertexIndex, binding.firstIndex, binding.indexCount))
    {
        nativeDraw.BindIndex(0);
        OutputDebugStringA(DX12W3DGeometryBuffers::LastError());
    }
    else nativeDraw.BindIndex(binding.indexCount);
}

void Renderer::Set_Index_Buffer_Index_Offset(unsigned offset)
{
    // Legacy name means base VERTEX index, not index-buffer byte offset.
    // D3D12 applies this to DrawIndexedInstanced instead of IASetIndexBuffer.
    if (ready && primaryWindow.IsSceneOpen() && renderThread == GetCurrentThreadId())
        primaryWindow.Presentation().SetBoundBaseVertexIndex(offset);
}

RenderTexture2DHandle Renderer::Create_Texture_2D(const RenderTexture2DDescription & description)
{
    if (!Has_Active_Device() || renderThread != GetCurrentThreadId())
        return nullptr;
    return DX12W3DTextures::Create2D(primaryWindow.Presentation().NativeDevice(), description);
}

RenderTexture2DHandle Renderer::Create_Texture_From_Surface( RenderSurfaceHandle surface, unsigned mipLevels)
{
    if (!Has_Active_Device() || renderThread != GetCurrentThreadId())
        return nullptr;
    if (renderTargets.Owns(surface))
    {
        ReportPending("Renderer::Create_Texture_From_Surface: GPU copy is R08");
        return nullptr;
    }
    return DX12W3DSurfaces::ToTexture(
        primaryWindow.Presentation().NativeDevice(), surface, mipLevels);
}

unsigned Renderer::Get_Texture_Mip_Count(RenderTextureHandle texture)
{
    return DX12W3DTextures::MipCount(texture);
}

void Renderer::Describe_Texture_Level_2D(RenderTexture2DHandle texture, unsigned level,
                                         RenderTextureLevelDescription & description)
{
    DX12W3DTextures::Describe(reinterpret_cast<RenderTextureHandle>(texture),
                              level, description);
}

void Renderer::Describe_Depth_Texture_Level_2D(RenderTexture2DHandle texture, unsigned level,
                                               RenderTextureLevelDescription & description)
{
    DX12W3DTextures::Describe(reinterpret_cast<RenderTextureHandle>(texture),
                              level, description);
}

void Renderer::Describe_Texture_Level_Cube(RenderTextureCubeHandle texture, unsigned level,
                                           RenderTextureLevelDescription & description)
{
    DX12W3DTextures::Describe(reinterpret_cast<RenderTextureHandle>(texture),
                              level, description);
}

void Renderer::Describe_Texture_Level_Volume(RenderTextureVolumeHandle texture, unsigned level,
                                             RenderTextureLevelDescription & description)
{
    DX12W3DTextures::Describe(reinterpret_cast<RenderTextureHandle>(texture),
                              level, description);
}

unsigned Renderer::Get_Texture_2D_Memory_Usage(RenderTexture2DHandle texture)
{
    return DX12W3DTextures::MemoryUsage(reinterpret_cast<RenderTextureHandle>(texture));
}

RenderTexture2DHandle Renderer::Create_Offscreen_Texture(const RenderSurfaceDescription & description)
{
    if (!Has_Active_Device() || renderThread != GetCurrentThreadId())
        return nullptr;
    return renderTargets.CreateOffscreenTexture(primaryWindow.Presentation(), description);
}

RenderSurfaceHandle Renderer::Acquire_Current_Color_Surface()
{
    return primaryWindow.IsSceneOpen()
        ? renderTargets.AcquireCurrentColor(primaryWindow.Presentation()) : nullptr;
}

RenderSurfaceHandle Renderer::Acquire_Back_Buffer_Surface()
{
    return primaryWindow.IsSceneOpen()
        ? renderTargets.AcquirePrimaryColor(primaryWindow.Presentation()) : nullptr;
}

unsigned Renderer::Read_Current_Color_Region(unsigned char * destination, unsigned capacity, int x, int y, unsigned width, unsigned height)
{
    // R08: a synchronous transfer/readback must use copyable footprints and
    // GPU fences. Do not reinterpret an offscreen RTV as a CPU image surface.
    ReportPending("Renderer::Read_Current_Color_Region (R08 GPU readback)");
    return 0;
}

RenderSurfaceHandle Renderer::Acquire_Current_Depth_Surface()
{
    return primaryWindow.IsSceneOpen()
        ? renderTargets.AcquireCurrentDepth(primaryWindow.Presentation()) : nullptr;
}

bool Renderer::Set_Render_Target_Surfaces(RenderSurfaceHandle color, RenderSurfaceHandle depth)
{
    if (!ready || renderThread != GetCurrentThreadId() || !primaryWindow.IsSceneOpen())
        return false;
    // It is illegal to sample a resource through the R05 shader stages
    // while the same resource is bound as an RTV/DSV output.
    const RenderTextureHandle colorTexture = renderTargets.TextureForSurface(color);
    const RenderTextureHandle depthTexture = renderTargets.TextureForSurface(depth);
    for (unsigned stage = 0; stage < DX12W3DTextureStages::StageCount; ++stage)
    {
        const auto bound = textureStages.Bound(stage);
        if (bound && (bound == colorTexture || bound == depthTexture))
            return false;
    }
    if (!renderTargets.Bind(primaryWindow.Presentation(), color, depth))
        return false;
    return !renderTargets.IsPrimary() || primaryWindow.RestorePrimaryViewport();
}

RenderSurfaceHandle Renderer::Acquire_Texture_Surface_Level( RenderTexture2DHandle texture, unsigned mipLevel)
{
    if (!Has_Active_Device() || renderThread != GetCurrentThreadId())
        return nullptr;
    return renderTargets.AcquireTextureLevel(primaryWindow.Presentation(), texture, mipLevel);
}

void Renderer::Update_Texture(RenderTextureHandle source, RenderTextureHandle destination)
{
    if (!DX12W3DTextures::Update(source, destination))
        OutputDebugStringA(DX12W3DTextures::LastError());
}

void Renderer::Map_Texture_2D(RenderTexture2DHandle texture, unsigned level,
                              RenderSurfaceMap & mapped)
{
    if (!DX12W3DTextures::Map2D(texture, level, mapped))
        OutputDebugStringA(DX12W3DTextures::LastError());
}

void Renderer::Unmap_Texture_2D(RenderTexture2DHandle texture, unsigned level)
{
    if (!DX12W3DTextures::Unmap2D(texture, level))
        OutputDebugStringA(DX12W3DTextures::LastError());
}

void Renderer::Map_Texture_Cube(RenderTextureCubeHandle texture, unsigned face,
                                unsigned level, RenderSurfaceMap & mapped)
{
    if (!DX12W3DTextures::MapCube(texture, face, level, mapped))
        OutputDebugStringA(DX12W3DTextures::LastError());
}

void Renderer::Unmap_Texture_Cube(RenderTextureCubeHandle texture, unsigned face,
                                  unsigned level)
{
    if (!DX12W3DTextures::UnmapCube(texture, face, level))
        OutputDebugStringA(DX12W3DTextures::LastError());
}

void Renderer::Map_Texture_Volume(RenderTextureVolumeHandle texture, unsigned level,
                                  RenderVolumeMap & mapped)
{
    if (!DX12W3DTextures::MapVolume(texture, level, mapped))
        OutputDebugStringA(DX12W3DTextures::LastError());
}

void Renderer::Unmap_Texture_Volume(RenderTextureVolumeHandle texture, unsigned level)
{
    if (!DX12W3DTextures::UnmapVolume(texture, level))
        OutputDebugStringA(DX12W3DTextures::LastError());
}

RenderTexture2DHandle Renderer::Create_Depth_Texture_2D(const RenderTexture2DDescription & description)
{
    if (!Has_Active_Device() || renderThread != GetCurrentThreadId())
        return nullptr;
    return renderTargets.CreateDepthTexture(primaryWindow.Presentation(), description);
}

void Renderer::Bind_Texture(unsigned stage, RenderTextureHandle texture)
{
    if (texture && primaryWindow.IsSceneOpen() &&
        (texture == renderTargets.BoundColorTexture() ||
         texture == renderTargets.BoundDepthTexture()))
    {
        // Depth-only and color+depth passes both forbid simultaneous
        // shader reads of the resource selected as their writable output.
        OutputDebugStringA("[DX12-R07] refusing SRV feedback from bound RTV/DSV\n");
        return;
    }
    if (!textureStages.Bind(stage, texture))
    {
        OutputDebugStringA(textureStages.LastError());
        return;
    }
    // Stage identity is strongly referenced and null explicitly unbinds.
    // A changed CPU mip generation is uploaded before the next native draw;
    // BindForDraw snapshots immutable t0/t1 SRVs after root-signature setup.
    if (primaryWindow.IsSceneOpen() &&
        !textureStages.PrepareBound(primaryWindow.Presentation()))
        OutputDebugStringA(textureStages.LastError());
}

void Renderer::Retain_Texture(RenderTextureHandle texture)
{
    DX12W3DTextures::Retain(texture);
}

void Renderer::Release_Texture(RenderTextureHandle texture)
{
    DX12W3DTextures::Release(texture);
}

unsigned Renderer::Get_Texture_Priority(RenderTextureHandle texture)
{
    return DX12W3DTextures::GetPriority(texture);
}

unsigned Renderer::Set_Texture_Priority(RenderTextureHandle texture, unsigned priority)
{
    return DX12W3DTextures::SetPriority(texture, priority);
}

RenderTextureCubeHandle Renderer::Create_Texture_Cube(const RenderTextureCubeDescription & description)
{
    if (!Has_Active_Device() || renderThread != GetCurrentThreadId())
        return nullptr;
    return DX12W3DTextures::CreateCube(primaryWindow.Presentation().NativeDevice(), description);
}

RenderTextureVolumeHandle Renderer::Create_Texture_Volume(const RenderTextureVolumeDescription & description)
{
    if (!Has_Active_Device() || renderThread != GetCurrentThreadId())
        return nullptr;
    return DX12W3DTextures::CreateVolume(primaryWindow.Presentation().NativeDevice(), description);
}

RenderSurfaceHandle Renderer::Create_Surface(const RenderSurfaceDescription & description)
{
    return DX12W3DSurfaces::Create(description);
}

RenderSurfaceHandle Renderer::Load_Surface(const char * filename)
{
    // Native filesystem TGA loader. TextureLoader handles archived
    // DDS/TGA assets through W3D's existing FileFactory CPU decode path.
    return DX12W3DSurfaces::LoadTGAFile(filename);
}

void Renderer::Describe_Surface(RenderSurfaceHandle surface, RenderSurfaceDescription & description)
{
    if (renderTargets.Owns(surface))
    {
        renderTargets.Describe(surface, description);
        return;
    }
    if (!DX12W3DSurfaces::Describe(surface, description))
        OutputDebugStringA(DX12W3DSurfaces::LastError());
}

void Renderer::Retain_Surface(RenderSurfaceHandle surface)
{
    if (renderTargets.Owns(surface)) renderTargets.Retain(surface);
    else DX12W3DSurfaces::Retain(surface);
}

void Renderer::Release_Surface(RenderSurfaceHandle surface)
{
    if (renderTargets.Owns(surface)) renderTargets.Release(surface);
    else DX12W3DSurfaces::Release(surface);
}

void Renderer::Lock_Surface(RenderSurfaceHandle surface, RenderSurfaceMap & map, const RenderSurfaceRect * region, bool readOnly)
{
    if (renderTargets.Owns(surface))
    {
        map = RenderSurfaceMap();
        ReportPending("Renderer::Lock_Surface: R08 GPU readback");
        return;
    }
    if (!DX12W3DSurfaces::Lock(surface, map, region, readOnly))
        OutputDebugStringA(DX12W3DSurfaces::LastError());
}

void Renderer::Unlock_Surface(RenderSurfaceHandle surface)
{
    if (renderTargets.Owns(surface))
    {
        ReportPending("Renderer::Unlock_Surface: R08 GPU readback");
        return;
    }
    if (!DX12W3DSurfaces::Unlock(surface))
        OutputDebugStringA(DX12W3DSurfaces::LastError());
}

void Renderer::Copy_Surface(RenderSurfaceHandle source, const RenderSurfaceRect * sourceRect, RenderSurfaceHandle destination, const RenderSurfacePoint * destinationPoint)
{
    if (renderTargets.Owns(source) || renderTargets.Owns(destination))
    {
        ReportPending("Renderer::Copy_Surface: R08 GPU copy");
        return;
    }
    if (!DX12W3DSurfaces::Copy(source, sourceRect, destination, destinationPoint))
        OutputDebugStringA(DX12W3DSurfaces::LastError());
}

void Renderer::Blit_Surface(RenderSurfaceHandle source, const RenderSurfaceRect & sourceRect, RenderSurfaceHandle destination, const RenderSurfaceRect & destinationRect, RenderSurfaceBlitFilter filter)
{
    if (renderTargets.Owns(source) || renderTargets.Owns(destination))
    {
        ReportPending("Renderer::Blit_Surface: R08 GPU blit");
        return;
    }
    if (!DX12W3DSurfaces::Blit(source, sourceRect, destination, destinationRect, filter))
        OutputDebugStringA(DX12W3DSurfaces::LastError());
}

void Renderer::Apply_Texture_Sampler(unsigned stage, const RenderSamplerDescription & sampler)
{
    ReportPending("Renderer::Apply_Texture_Sampler");
}

void Renderer::Set_Texture_Max_Anisotropy(unsigned stage, unsigned level)
{
    ReportPending("Renderer::Set_Texture_Max_Anisotropy");
}

void Renderer::Apply_Blend_State(const RenderBlendState & state)
{
    nativeDraw.SetBlend(state);
}

void Renderer::Set_Depth_Compare(RenderCompareFunc compare)
{
    nativeDraw.SetDepthCompare(compare);
}

void Renderer::Set_Legacy_Depth_Bias(unsigned bias)
{
    ReportPending("Renderer::Set_Legacy_Depth_Bias");
}

void Renderer::Set_Depth_Write(bool enabled)
{
    nativeDraw.SetDepthWrite(enabled);
}

void Renderer::Set_Cull_Mode(RenderCullMode cullMode)
{
    nativeDraw.SetCull(cullMode);
}

void Renderer::Set_Alpha_Test_Enabled(bool enabled)
{
    nativeDraw.SetAlphaTest(enabled);
}

void Renderer::Set_Vertex_Lighting(bool enabled)
{
    nativeDraw.SetLightingEnabled(enabled);
}

void Renderer::Set_Vertex_Color_Sources(const RenderMaterialColorSources & sources)
{
    nativeDraw.SetMaterialColorSources(sources);
}

void Renderer::Set_Texture_UV_Passthrough(unsigned stage, unsigned uvIndex)
{
    ReportPending("Renderer::Set_Texture_UV_Passthrough");
}

void Renderer::Set_Vertex_Material(const RenderVertexMaterialDescription & material)
{
    // Unlit diffuse/opacity bridge for M01; lighting and material mapper
    // permutations remain M03 rather than silently claiming full parity.
    nativeDraw.SetMaterial(material);
}

void Renderer::Set_Fixed_Stage_Operation(unsigned stage, bool alpha, RenderFixedStageOperation operation)
{
    ReportPending("Renderer::Set_Fixed_Stage_Operation");
}

void Renderer::Set_Fixed_Stage_Argument(unsigned stage, bool alpha, unsigned slot, RenderTextureArgument source, bool complement, bool replicateAlpha)
{
    ReportPending("Renderer::Set_Fixed_Stage_Argument");
}

void Renderer::Set_Texture_Coordinate_Source(unsigned stage, unsigned uvIndex, bool cameraSpacePosition)
{
    ReportPending("Renderer::Set_Texture_Coordinate_Source");
}

void Renderer::Set_Texture_Transform_2D(unsigned stage, bool enabled)
{
    ReportPending("Renderer::Set_Texture_Transform_2D");
}

void Renderer::Apply_Texture_Stage_Color( unsigned stage, const RenderTextureStageChannel & channel, bool uncached)
{
    ReportPending("Renderer::Apply_Texture_Stage_Color");
}

void Renderer::Apply_Texture_Stage_Alpha( unsigned stage, const RenderTextureStageChannel & channel, bool uncached)
{
    ReportPending("Renderer::Apply_Texture_Stage_Alpha");
}

void Renderer::Set_Texture_Filter(unsigned stage, RenderSamplerFilterField field, RenderSamplerFilter filter)
{
    ReportPending("Renderer::Set_Texture_Filter");
}

void Renderer::Set_Texture_Address(unsigned stage, RenderSamplerAxis axis, RenderSamplerAddress address)
{
    ReportPending("Renderer::Set_Texture_Address");
}

void Renderer::Set_Texture_Border_Color(unsigned stage, unsigned packedARGB)
{
    ReportPending("Renderer::Set_Texture_Border_Color");
}

void Renderer::Set_Source_Blend(RenderBlendFactor factor)
{
    nativeDraw.SetSourceBlend(factor);
}

void Renderer::Set_Destination_Blend(RenderBlendFactor factor)
{
    nativeDraw.SetDestinationBlend(factor);
}

void Renderer::Set_Blend_Operation(RenderBlendOperation operation)
{
    nativeDraw.SetBlendOperation(operation);
}

void Renderer::Set_Blending_Enabled(bool enabled)
{
    nativeDraw.SetBlendingEnabled(enabled);
}

void Renderer::Set_Texture_Factor(unsigned packedARGB)
{
    ReportPending("Renderer::Set_Texture_Factor");
}

void Renderer::Set_Color_Write_Mask(bool rgbEnabled, bool alphaEnabled)
{
    nativeDraw.SetColorWriteMask(rgbEnabled, alphaEnabled);
}

bool Renderer::Supports_Texture_Format(unsigned w3dPixelFormat)
{
    return ready && DX12W3DTextures::SupportsFormat(w3dPixelFormat);
}

unsigned Renderer::Get_Back_Buffer_Format()
{
    ReportPending("Renderer::Get_Back_Buffer_Format");
    return {};
}

