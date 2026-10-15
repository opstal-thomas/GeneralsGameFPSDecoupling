/*
**  Command & Conquer Generals Zero Hour(tm)
**  Copyright 2026 TheSuperHackers
**
**  This program is free software under the GNU General Public License,
**  version 3 or (at your option) any later version.
*/

// DX12-B03: the exclusive backend now binds DX12 presentation to the game's
// actual application HWND and presents a single native diagnostic frame.
// Normal game rendering is deliberately blocked until B04 implements frame,
// render-state and resource submission. No D3D8 fallback is permitted.
#include "WW3D2/Renderer.h"
#include "WW3D2/Backend/DX12/DX12PrimaryWindow.h"
#include "WWMath/vector3.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cstdio>
#include <algorithm>
#include <d3d12.h>

#if !defined(W3D_RENDER_BACKEND_DX12) || defined(W3D_RENDER_BACKEND_DX8)
#error "DX12Backend.cpp must be compiled exclusively for W3D_RENDER_BACKEND=DX12"
#endif

namespace
{
    DX12PrimaryWindow primaryWindow;
    bool ready = false;
    bool recording = false;
    unsigned renderThread = 0;
    unsigned syncInterval = 0;
    RenderViewport viewportState = {0, 0, 0, 0, 0.f, 1.f};

    void ReportPending(const char * operation)
    {
        char message[256] = {};
        std::snprintf(message, sizeof(message),
            "[DX12 backend bootstrap] %s is not implemented yet (DX12-B03/B04 onward).\n",
            operation);
        OutputDebugStringA(message);
    }
}

bool Renderer::Init(void * window, bool lite)
{
    // WW3D::Init passes the application's actual HWND. Do not make a second
    // preview window, alter its window procedure or construct a DX8 device.
    // Keep the unsupported game-rendering path closed until DX12-B04.
    if (lite)
    {
        ReportPending("Renderer::Init: DX12 lite/headless support");
        return false;
    }

    char warpValue[4] = {};
    const bool useWarp = GetEnvironmentVariableA(
        "W3D_DX12_GAME_WARP", warpValue, sizeof(warpValue)) == 1 &&
        warpValue[0] == '1';

    if (!primaryWindow.Attach(window, useWarp))
    {
        OutputDebugStringA(primaryWindow.LastError());
        OutputDebugStringA("\n");
        primaryWindow.Shutdown();
        return false;
    }

    constexpr float diagnosticColor[4] = {0.04f, 0.16f, 0.28f, 1.0f};
    if (primaryWindow.CurrentStatus() == DX12PrimaryWindow::Status::Ready &&
        !primaryWindow.PresentDiagnostic(diagnosticColor))
    {
        OutputDebugStringA(primaryWindow.LastError());
        OutputDebugStringA("\n");
    }

    // B03 verifies primary-HWND device/swapchain ownership and diagnostic
    // presentation. Returning false is intentional: the game's higher-level
    // render paths still call unported DX8 facilities, and proceeding would
    // appear to run DX12 while silently dropping essential drawing.
    ReportPending("Renderer::Init: DX12 main-HWND presentation ready; "
                  "normal frame and resource rendering requires B04");
    // B04 supports only an opt-in native diagnostic until B07/B10 make the
    // W3D resource and draw API authoritative; do not advertise game parity.
    char enabled[4] = {};
    if (GetEnvironmentVariableA("W3D_DX12_B04_DIAGNOSTIC", enabled, sizeof(enabled)) == 1 &&
        enabled[0] == '1')
    {
        ready = true;
        renderThread = GetCurrentThreadId();
        return true;
    }
    primaryWindow.Shutdown();
    return false;
}

void Renderer::Shutdown()
{
    ready = recording = false;
    renderThread = 0;
    primaryWindow.Shutdown();
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
    return ready && primaryWindow.Presentation().IsReady();
}

bool Renderer::Is_Device_Unavailable_For_Rendering()
{
    return !ready || primaryWindow.CurrentStatus() != DX12PrimaryWindow::Status::Ready;
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
    if (!ready || recording || renderThread != GetCurrentThreadId()) return;
    auto status = primaryWindow.Synchronize();
    if (status == DX12PrimaryWindow::Status::Suspended) return;
    if (status != DX12PrimaryWindow::Status::Ready) { ready = false; return; }
    const float black[] = {0.f, 0.f, 0.f, 1.f};
    if (!primaryWindow.Presentation().BeginFrame(black)) { ready = false; return; }
    recording = true;
    Set_Viewport(viewportState);
}

void Renderer::End_Scene(bool flip_frame)
{
    (void)flip_frame;
    if (!recording) return;
    if (!primaryWindow.Presentation().EndFrame(syncInterval))
    {
        OutputDebugStringA(primaryWindow.Presentation().LastError());
        ready = false;
    }
    recording = false;
}

void Renderer::Flip_To_Primary()
{
    // Presentation is already performed by End_Scene.
}

void Renderer::Clear(bool clear_color, bool clear_z_stencil, const Vector3 & color, float dest_alpha, float z, unsigned int stencil)
{
    (void)stencil;
    if (!recording) return;
    const float rgba[] = {color.X, color.Y, color.Z, dest_alpha};
    if (!primaryWindow.Presentation().ClearActiveFrame(clear_color, clear_z_stencil, rgba, z))
        ready = false;
}

void Renderer::Set_Viewport(const RenderViewport & viewport)
{
    viewportState = viewport;
    if (!recording) return;
    const unsigned width = primaryWindow.Presentation().Width();
    const unsigned height = primaryWindow.Presentation().Height();
    const unsigned x = (std::min)(viewport.x, width);
    const unsigned y = (std::min)(viewport.y, height);
    const unsigned w = viewport.width ? (std::min)(viewport.width, width - x) : width - x;
    const unsigned h = viewport.height ? (std::min)(viewport.height, height - y) : height - y;
    D3D12_VIEWPORT vp = {static_cast<float>(x), static_cast<float>(y),
        static_cast<float>(w), static_cast<float>(h), viewport.min_z, viewport.max_z};
    D3D12_RECT scissor = {static_cast<LONG>(x), static_cast<LONG>(y),
        static_cast<LONG>(x + w), static_cast<LONG>(y + h)};
    auto * list = primaryWindow.Presentation().ActiveCommandList();
    if (list) { list->RSSetViewports(1, &vp); list->RSSetScissorRects(1, &scissor); }
}

void Renderer::Invalidate_Cached_Render_States()
{
    if (recording) Set_Viewport(viewportState);
}

unsigned Renderer::Pack_Color_ARGB(const Vector3 & color, float alpha)
{
    ReportPending("Renderer::Pack_Color_ARGB");
    return {};
}

unsigned Renderer::Pack_Color_ARGB(const Vector4 & color)
{
    ReportPending("Renderer::Pack_Color_ARGB");
    return {};
}

unsigned Renderer::Pack_Color_ARGB_Clamp(const Vector4 & color)
{
    ReportPending("Renderer::Pack_Color_ARGB_Clamp");
    return {};
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

void Renderer::Draw_Triangles(unsigned short startIndex, unsigned short triangleCount, unsigned short minVertexIndex, unsigned short vertexCount)
{
    ReportPending("Renderer::Draw_Triangles");
}

void Renderer::Draw_Triangle_Strip(unsigned short startIndex, unsigned short primitiveCount, unsigned short minVertexIndex, unsigned short vertexCount)
{
    ReportPending("Renderer::Draw_Triangle_Strip");
}

void Renderer::Capture_Sorting_State(RenderStateStruct & state)
{
    ReportPending("Renderer::Capture_Sorting_State");
}

void Renderer::Restore_Sorting_State(const RenderStateStruct & state)
{
    ReportPending("Renderer::Restore_Sorting_State");
}

void Renderer::Release_Sorting_State()
{
    ReportPending("Renderer::Release_Sorting_State");
}

void Renderer::Apply_Sorting_Transforms_And_Lights(RenderStateStruct & render_state)
{
    ReportPending("Renderer::Apply_Sorting_Transforms_And_Lights");
}

RenderSortingMatrix Renderer::Multiply_Sorting_World_View( const RenderSortingMatrix & world, const RenderSortingMatrix & view)
{
    ReportPending("Renderer::Multiply_Sorting_World_View");
    return {};
}

Vector3 Renderer::Transform_Sorting_Center( const RenderSortingMatrix & world, const RenderSortingMatrix & view, const Vector3 & center)
{
    ReportPending("Renderer::Transform_Sorting_Center");
    return {};
}

bool Renderer::Is_Triangle_Draw_Enabled()
{
    ReportPending("Renderer::Is_Triangle_Draw_Enabled");
    return {};
}

void Renderer::Set_Triangle_Draw_Enabled(bool enabled)
{
    ReportPending("Renderer::Set_Triangle_Draw_Enabled");
}

void Renderer::Unbind_Sorting_Geometry()
{
    ReportPending("Renderer::Unbind_Sorting_Geometry");
}

void Renderer::Draw_Transformed_Quad(const void * vertices, unsigned vertexStride, unsigned uvSets)
{
    ReportPending("Renderer::Draw_Transformed_Quad");
}

void Renderer::Bind_Immediate_Texture(unsigned stage, RenderTextureHandle texture)
{
    ReportPending("Renderer::Bind_Immediate_Texture");
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

void Renderer::Set_Shader(const ShaderClass & shader)
{
    ReportPending("Renderer::Set_Shader");
}

void Renderer::Set_Material(const VertexMaterialClass * material)
{
    ReportPending("Renderer::Set_Material");
}

void Renderer::Set_Texture(unsigned stage, TextureBaseClass * texture)
{
    ReportPending("Renderer::Set_Texture");
}

void Renderer::Apply_Pending_State_Changes()
{
    ReportPending("Renderer::Apply_Pending_State_Changes");
}

void Renderer::Set_Transform(RenderTransform transform, const Matrix3D & matrix)
{
    ReportPending("Renderer::Set_Transform");
}

void Renderer::Set_Transform(RenderTransform transform, const Matrix4x4 & matrix)
{
    ReportPending("Renderer::Set_Transform");
}

void Renderer::Get_Transform(RenderTransform transform, Matrix4x4 & matrix)
{
    ReportPending("Renderer::Get_Transform");
}

void Renderer::Set_Texture_Transform(unsigned stage, const float matrix[16])
{
    ReportPending("Renderer::Set_Texture_Transform");
}

void Renderer::Get_Transform_Elements(RenderTransform transform, float matrix[16])
{
    (void)transform; for (unsigned i = 0; i < 16; ++i) matrix[i] = 0.f;
    ReportPending("Renderer::Get_Transform_Elements");
}

void Renderer::Set_World_Identity()
{
    ReportPending("Renderer::Set_World_Identity");
}

void Renderer::Set_Ambient(const Vector3 & color)
{
    ReportPending("Renderer::Set_Ambient");
}

void Renderer::Set_Light_Environment(LightEnvironmentClass * light_env)
{
    ReportPending("Renderer::Set_Light_Environment");
}

bool Renderer::Set_Render_Device(const char * name, int width, int height, int bits, int windowed, bool resize_window)
{
    ReportPending("Renderer::Set_Render_Device");
    return {};
}

bool Renderer::Set_Render_Device(int device, int width, int height, int bits, int windowed, bool resize_window, bool reset_device, bool restore_assets)
{
    ReportPending("Renderer::Set_Render_Device");
    return {};
}

bool Renderer::Set_Any_Render_Device()
{
    ReportPending("Renderer::Set_Any_Render_Device");
    return {};
}

bool Renderer::Set_Next_Render_Device()
{
    ReportPending("Renderer::Set_Next_Render_Device");
    return {};
}

int Renderer::Get_Render_Device()
{
    ReportPending("Renderer::Get_Render_Device");
    return {};
}

int Renderer::Get_Render_Device_Count()
{
    ReportPending("Renderer::Get_Render_Device_Count");
    return {};
}

const char * Renderer::Get_Render_Device_Name(int device_index)
{
    (void)device_index;
    return "DX12 bootstrap (B03/B04 pending)";
}

bool Renderer::Is_Windowed()
{
    ReportPending("Renderer::Is_Windowed");
    return {};
}

bool Renderer::Toggle_Windowed()
{
    ReportPending("Renderer::Toggle_Windowed");
    return {};
}

bool Renderer::Set_Device_Resolution(int width, int height, int bits, int windowed, bool resize_window)
{
    ReportPending("Renderer::Set_Device_Resolution");
    return {};
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
    return DEVICE_UNAVAILABLE;
}

bool Renderer::Reset_Device()
{
    ReportPending("Renderer::Reset_Device");
    return {};
}

void Renderer::Set_Polygon_Mode(PolygonMode mode)
{
    ReportPending("Renderer::Set_Polygon_Mode");
}

bool Renderer::Get_Adapter_Identification(RenderAdapterIdentification & identification)
{
    identification = RenderAdapterIdentification();
    return false;
}

RenderCapabilities Renderer::Get_Capabilities()
{
    ReportPending("Renderer::Get_Capabilities");
    return {};
}

void Renderer::Set_Mesh_Camera(CameraClass * camera)
{
    ReportPending("Renderer::Set_Mesh_Camera");
}

void Renderer::Flush_Mesh_Batches()
{
    ReportPending("Renderer::Flush_Mesh_Batches");
}

void Renderer::Clear_Pending_Mesh_Deletes()
{
    ReportPending("Renderer::Clear_Pending_Mesh_Deletes");
}

void Renderer::Invalidate_Mesh_Cache()
{
    ReportPending("Renderer::Invalidate_Mesh_Cache");
}

void Renderer::Release_Texture_Resources()
{
    ReportPending("Renderer::Release_Texture_Resources");
}

RenderVertexBufferHandle Renderer::Create_Vertex_Buffer(const RenderBufferDescription & description)
{
    ReportPending("Renderer::Create_Vertex_Buffer");
    return {};
}

RenderVertexBufferHandle Renderer::Create_Point_Sprite_Vertex_Buffer(unsigned byteSize)
{
    ReportPending("Renderer::Create_Point_Sprite_Vertex_Buffer");
    return {};
}

RenderIndexBufferHandle Renderer::Create_Index_Buffer(const RenderBufferDescription & description)
{
    ReportPending("Renderer::Create_Index_Buffer");
    return {};
}

void Renderer::Release_Vertex_Buffer(RenderVertexBufferHandle buffer)
{
    ReportPending("Renderer::Release_Vertex_Buffer");
}

void Renderer::Release_Index_Buffer(RenderIndexBufferHandle buffer)
{
    ReportPending("Renderer::Release_Index_Buffer");
}

void Renderer::Discard_Managed_Resources()
{
    ReportPending("Renderer::Discard_Managed_Resources");
}

void * Renderer::Map_Vertex_Buffer(RenderVertexBufferHandle buffer, unsigned offsetBytes, unsigned sizeBytes, unsigned mapFlags)
{
    ReportPending("Renderer::Map_Vertex_Buffer");
    return {};
}

void * Renderer::Map_Index_Buffer(RenderIndexBufferHandle buffer, unsigned offsetBytes, unsigned sizeBytes, unsigned mapFlags)
{
    ReportPending("Renderer::Map_Index_Buffer");
    return {};
}

void Renderer::Unmap_Vertex_Buffer(RenderVertexBufferHandle buffer)
{
    ReportPending("Renderer::Unmap_Vertex_Buffer");
}

void Renderer::Unmap_Index_Buffer(RenderIndexBufferHandle buffer)
{
    ReportPending("Renderer::Unmap_Index_Buffer");
}

void Renderer::Bind_Vertex_Buffer(RenderVertexBufferHandle buffer, unsigned stream, unsigned stride)
{
    ReportPending("Renderer::Bind_Vertex_Buffer");
}

void Renderer::Bind_Point_Sprite_Vertex_Buffer(RenderVertexBufferHandle buffer, unsigned stride)
{
    ReportPending("Renderer::Bind_Point_Sprite_Vertex_Buffer");
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
    ReportPending("Renderer::Bind_Index_Buffer");
}

void Renderer::Bind_Dynamic_Vertex_Buffer(const RenderDynamicVertexBinding & binding)
{
    ReportPending("Renderer::Bind_Dynamic_Vertex_Buffer");
}

void Renderer::Bind_Dynamic_Vertex_Buffer(const DynamicVBAccessClass & access)
{
    ReportPending("Renderer::Bind_Dynamic_Vertex_Buffer");
}

void Renderer::Bind_Dynamic_Index_Buffer(const RenderDynamicIndexBinding & binding, unsigned short baseVertexIndex)
{
    ReportPending("Renderer::Bind_Dynamic_Index_Buffer");
}

void Renderer::Bind_Dynamic_Index_Buffer(const DynamicIBAccessClass & access, unsigned short baseVertexIndex)
{
    ReportPending("Renderer::Bind_Dynamic_Index_Buffer");
}

void Renderer::Bind_Legacy_Index_Buffer(const IndexBufferClass * buffer, unsigned short baseVertexIndex)
{
    ReportPending("Renderer::Bind_Legacy_Index_Buffer");
}

void Renderer::Bind_Legacy_Vertex_Buffer(const VertexBufferClass * buffer)
{
    ReportPending("Renderer::Bind_Legacy_Vertex_Buffer");
}

void Renderer::Set_Index_Buffer_Index_Offset(unsigned offset)
{
    ReportPending("Renderer::Set_Index_Buffer_Index_Offset");
}

RenderTexture2DHandle Renderer::Create_Texture_2D(const RenderTexture2DDescription & description)
{
    ReportPending("Renderer::Create_Texture_2D");
    return {};
}

RenderTexture2DHandle Renderer::Create_Texture_From_Surface( RenderSurfaceHandle surface, unsigned mipLevels)
{
    ReportPending("Renderer::Create_Texture_From_Surface");
    return {};
}

unsigned Renderer::Get_Texture_Mip_Count(RenderTextureHandle texture)
{
    ReportPending("Renderer::Get_Texture_Mip_Count");
    return {};
}

void Renderer::Describe_Texture_Level_2D(RenderTexture2DHandle texture, unsigned level, RenderTextureLevelDescription & description)
{
    description = RenderTextureLevelDescription();
    ReportPending("Renderer::Describe_Texture_Level_2D");
}

void Renderer::Describe_Depth_Texture_Level_2D(RenderTexture2DHandle texture, unsigned level, RenderTextureLevelDescription & description)
{
    description = RenderTextureLevelDescription();
    ReportPending("Renderer::Describe_Depth_Texture_Level_2D");
}

void Renderer::Describe_Texture_Level_Cube(RenderTextureCubeHandle texture, unsigned level, RenderTextureLevelDescription & description)
{
    description = RenderTextureLevelDescription();
    ReportPending("Renderer::Describe_Texture_Level_Cube");
}

void Renderer::Describe_Texture_Level_Volume(RenderTextureVolumeHandle texture, unsigned level, RenderTextureLevelDescription & description)
{
    description = RenderTextureLevelDescription();
    ReportPending("Renderer::Describe_Texture_Level_Volume");
}

unsigned Renderer::Get_Texture_2D_Memory_Usage(RenderTexture2DHandle texture)
{
    ReportPending("Renderer::Get_Texture_2D_Memory_Usage");
    return {};
}

RenderTexture2DHandle Renderer::Create_Offscreen_Texture(const RenderSurfaceDescription & description)
{
    ReportPending("Renderer::Create_Offscreen_Texture");
    return {};
}

RenderSurfaceHandle Renderer::Acquire_Current_Color_Surface()
{
    ReportPending("Renderer::Acquire_Current_Color_Surface");
    return {};
}

RenderSurfaceHandle Renderer::Acquire_Back_Buffer_Surface()
{
    ReportPending("Renderer::Acquire_Back_Buffer_Surface");
    return {};
}

unsigned Renderer::Read_Current_Color_Region(unsigned char * destination, unsigned capacity, int x, int y, unsigned width, unsigned height)
{
    ReportPending("Renderer::Read_Current_Color_Region");
    return {};
}

RenderSurfaceHandle Renderer::Acquire_Current_Depth_Surface()
{
    ReportPending("Renderer::Acquire_Current_Depth_Surface");
    return {};
}

bool Renderer::Set_Render_Target_Surfaces(RenderSurfaceHandle color, RenderSurfaceHandle depth)
{
    ReportPending("Renderer::Set_Render_Target_Surfaces");
    return {};
}

RenderSurfaceHandle Renderer::Acquire_Texture_Surface_Level( RenderTexture2DHandle texture, unsigned mipLevel)
{
    ReportPending("Renderer::Acquire_Texture_Surface_Level");
    return {};
}

void Renderer::Update_Texture(RenderTextureHandle source, RenderTextureHandle destination)
{
    ReportPending("Renderer::Update_Texture");
}

void Renderer::Map_Texture_2D(RenderTexture2DHandle texture, unsigned level, RenderSurfaceMap & mapped)
{
    mapped = RenderSurfaceMap();
    ReportPending("Renderer::Map_Texture_2D");
}

void Renderer::Unmap_Texture_2D(RenderTexture2DHandle texture, unsigned level)
{
    ReportPending("Renderer::Unmap_Texture_2D");
}

void Renderer::Map_Texture_Cube(RenderTextureCubeHandle texture, unsigned face, unsigned level, RenderSurfaceMap & mapped)
{
    mapped = RenderSurfaceMap();
    ReportPending("Renderer::Map_Texture_Cube");
}

void Renderer::Unmap_Texture_Cube(RenderTextureCubeHandle texture, unsigned face, unsigned level)
{
    ReportPending("Renderer::Unmap_Texture_Cube");
}

void Renderer::Map_Texture_Volume(RenderTextureVolumeHandle texture, unsigned level, RenderVolumeMap & mapped)
{
    mapped = RenderVolumeMap();
    ReportPending("Renderer::Map_Texture_Volume");
}

void Renderer::Unmap_Texture_Volume(RenderTextureVolumeHandle texture, unsigned level)
{
    ReportPending("Renderer::Unmap_Texture_Volume");
}

RenderTexture2DHandle Renderer::Create_Depth_Texture_2D(const RenderTexture2DDescription & description)
{
    ReportPending("Renderer::Create_Depth_Texture_2D");
    return {};
}

void Renderer::Bind_Texture(unsigned stage, RenderTextureHandle texture)
{
    ReportPending("Renderer::Bind_Texture");
}

void Renderer::Retain_Texture(RenderTextureHandle texture)
{
    ReportPending("Renderer::Retain_Texture");
}

void Renderer::Release_Texture(RenderTextureHandle texture)
{
    ReportPending("Renderer::Release_Texture");
}

unsigned Renderer::Get_Texture_Priority(RenderTextureHandle texture)
{
    ReportPending("Renderer::Get_Texture_Priority");
    return {};
}

unsigned Renderer::Set_Texture_Priority(RenderTextureHandle texture, unsigned priority)
{
    ReportPending("Renderer::Set_Texture_Priority");
    return {};
}

RenderTextureCubeHandle Renderer::Create_Texture_Cube(const RenderTextureCubeDescription & description)
{
    ReportPending("Renderer::Create_Texture_Cube");
    return {};
}

RenderTextureVolumeHandle Renderer::Create_Texture_Volume(const RenderTextureVolumeDescription & description)
{
    ReportPending("Renderer::Create_Texture_Volume");
    return {};
}

RenderSurfaceHandle Renderer::Create_Surface(const RenderSurfaceDescription & description)
{
    ReportPending("Renderer::Create_Surface");
    return {};
}

RenderSurfaceHandle Renderer::Load_Surface(const char * filename)
{
    ReportPending("Renderer::Load_Surface");
    return {};
}

void Renderer::Describe_Surface(RenderSurfaceHandle surface, RenderSurfaceDescription & description)
{
    (void)surface; description = RenderSurfaceDescription();
    ReportPending("Renderer::Describe_Surface");
}

void Renderer::Retain_Surface(RenderSurfaceHandle surface)
{
    ReportPending("Renderer::Retain_Surface");
}

void Renderer::Release_Surface(RenderSurfaceHandle surface)
{
    ReportPending("Renderer::Release_Surface");
}

void Renderer::Lock_Surface(RenderSurfaceHandle surface, RenderSurfaceMap & map, const RenderSurfaceRect * region, bool readOnly)
{
    map = RenderSurfaceMap();
    ReportPending("Renderer::Lock_Surface");
}

void Renderer::Unlock_Surface(RenderSurfaceHandle surface)
{
    ReportPending("Renderer::Unlock_Surface");
}

void Renderer::Copy_Surface(RenderSurfaceHandle source, const RenderSurfaceRect * sourceRect, RenderSurfaceHandle destination, const RenderSurfacePoint * destinationPoint)
{
    ReportPending("Renderer::Copy_Surface");
}

void Renderer::Blit_Surface(RenderSurfaceHandle source, const RenderSurfaceRect & sourceRect, RenderSurfaceHandle destination, const RenderSurfaceRect & destinationRect, RenderSurfaceBlitFilter filter)
{
    ReportPending("Renderer::Blit_Surface");
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
    ReportPending("Renderer::Apply_Blend_State");
}

void Renderer::Set_Depth_Compare(RenderCompareFunc compare)
{
    ReportPending("Renderer::Set_Depth_Compare");
}

void Renderer::Set_Legacy_Depth_Bias(unsigned bias)
{
    ReportPending("Renderer::Set_Legacy_Depth_Bias");
}

void Renderer::Set_Depth_Write(bool enabled)
{
    ReportPending("Renderer::Set_Depth_Write");
}

void Renderer::Set_Cull_Mode(RenderCullMode cullMode)
{
    ReportPending("Renderer::Set_Cull_Mode");
}

void Renderer::Set_Alpha_Test_Enabled(bool enabled)
{
    ReportPending("Renderer::Set_Alpha_Test_Enabled");
}

void Renderer::Set_Vertex_Lighting(bool enabled)
{
    ReportPending("Renderer::Set_Vertex_Lighting");
}

void Renderer::Set_Vertex_Color_Sources(const RenderMaterialColorSources & sources)
{
    ReportPending("Renderer::Set_Vertex_Color_Sources");
}

void Renderer::Set_Texture_UV_Passthrough(unsigned stage, unsigned uvIndex)
{
    ReportPending("Renderer::Set_Texture_UV_Passthrough");
}

void Renderer::Set_Vertex_Material(const RenderVertexMaterialDescription & material)
{
    ReportPending("Renderer::Set_Vertex_Material");
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
    ReportPending("Renderer::Set_Source_Blend");
}

void Renderer::Set_Destination_Blend(RenderBlendFactor factor)
{
    ReportPending("Renderer::Set_Destination_Blend");
}

void Renderer::Set_Blend_Operation(RenderBlendOperation operation)
{
    ReportPending("Renderer::Set_Blend_Operation");
}

void Renderer::Set_Blending_Enabled(bool enabled)
{
    ReportPending("Renderer::Set_Blending_Enabled");
}

void Renderer::Set_Texture_Factor(unsigned packedARGB)
{
    ReportPending("Renderer::Set_Texture_Factor");
}

void Renderer::Set_Color_Write_Mask(bool rgbEnabled, bool alphaEnabled)
{
    ReportPending("Renderer::Set_Color_Write_Mask");
}

bool Renderer::Supports_Texture_Format(unsigned w3dPixelFormat)
{
    ReportPending("Renderer::Supports_Texture_Format");
    return {};
}

unsigned Renderer::Get_Back_Buffer_Format()
{
    ReportPending("Renderer::Get_Back_Buffer_Format");
    return {};
}

