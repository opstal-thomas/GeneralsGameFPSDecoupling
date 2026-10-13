/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2026 TheSuperHackers
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// TheSuperHackers @refactor bobtista 08/10/2026 DX8 definition of the Renderer
// methods. Init and Shutdown own the DX8Wrapper lifecycle. The rendering methods
// are one-line trampolines to the existing DX8Wrapper static API.

#include "WW3D2/Renderer.h"

#include "WW3D2/dx8wrapper.h"
#include "WW3D2/dx8renderer.h"
#include "WW3D2/dx8caps.h"
#include "WW3D2/dx8texman.h"
#include "WW3D2/formconv.h"
#include "WWMath/vector3.h"
#include "WW3D2/lightenvironment.h"
#include "WWDebug/wwdebug.h"
#include "WWLib/thread.h"

static bool Initialized = false;
static bool Lite = false;

bool Renderer::Init(void * window, bool lite)
{
    WWASSERT(!Initialized);
    if (Initialized)
    {
        return true;
    }

    Init_D3D_To_WW3_Conversion();
    WWDEBUG_SAY(("Init DX8Wrapper"));
    if (!DX8Wrapper::Init(window, lite))
    {
        return false;
    }

    Initialized = true;
    Lite = lite;
    return true;
}

void Renderer::Shutdown()
{
    if (Initialized && !Lite)
    {
        DX8Wrapper::Shutdown();
    }
    Initialized = false;
}

void Renderer::Set_Gamma(float gamma, float bright, float contrast, bool calibrate, bool uselimit)
{
    DX8Wrapper::Set_Gamma(gamma, bright, contrast, calibrate, uselimit);
}

void Renderer::Begin_Scene()
{
    DX8Wrapper::Begin_Scene();
}

void Renderer::End_Scene(bool flip_frame)
{
    DX8Wrapper::End_Scene(flip_frame);
}

void Renderer::Flip_To_Primary()
{
    DX8Wrapper::Flip_To_Primary();
}

void Renderer::Clear(bool clear_color, bool clear_z_stencil,
                     const Vector3 & color,
                     float dest_alpha, float z, unsigned int stencil)
{
    DX8Wrapper::Clear(clear_color, clear_z_stencil, color, dest_alpha, z, stencil);
}

void Renderer::Set_Viewport(const RenderViewport & viewport)
{
    D3DVIEWPORT8 vp;
    vp.X      = viewport.x;
    vp.Y      = viewport.y;
    vp.Width  = viewport.width;
    vp.Height = viewport.height;
    vp.MinZ   = viewport.min_z;
    vp.MaxZ   = viewport.max_z;
    DX8Wrapper::Set_Viewport(&vp);
}

void Renderer::Invalidate_Cached_Render_States()
{
    DX8Wrapper::Invalidate_Cached_Render_States();
}

void Renderer::Set_Ambient(const Vector3 & color)
{
    DX8Wrapper::Set_Ambient(color);
}

void Renderer::Set_Light_Environment(LightEnvironmentClass * light_env)
{
    DX8Wrapper::Set_Light_Environment(light_env);
}


// The adapter and swap-chain operations previously forwarded directly from
// WW3D to DX8Wrapper. Keep the public Renderer contract free of D3D8 types.
bool Renderer::Set_Render_Device(const char * name, int width, int height, int bits,
                                 int windowed, bool resize_window)
{
    return DX8Wrapper::Set_Render_Device(name, width, height, bits, windowed, resize_window);
}

bool Renderer::Set_Render_Device(int device, int width, int height, int bits, int windowed,
                                 bool resize_window, bool reset_device, bool restore_assets)
{
    return DX8Wrapper::Set_Render_Device(device, width, height, bits, windowed,
                                         resize_window, reset_device, restore_assets);
}

bool Renderer::Set_Any_Render_Device()
{
    return DX8Wrapper::Set_Any_Render_Device();
}

bool Renderer::Set_Next_Render_Device()
{
    return DX8Wrapper::Set_Next_Render_Device();
}

int Renderer::Get_Render_Device()
{
    return DX8Wrapper::Get_Render_Device();
}

int Renderer::Get_Render_Device_Count()
{
    return DX8Wrapper::Get_Render_Device_Count();
}

const char * Renderer::Get_Render_Device_Name(int device_index)
{
    return DX8Wrapper::Get_Render_Device_Name(device_index);
}

bool Renderer::Is_Windowed()
{
    return DX8Wrapper::Is_Windowed();
}

bool Renderer::Toggle_Windowed()
{
    return DX8Wrapper::Toggle_Windowed();
}

bool Renderer::Set_Device_Resolution(int width, int height, int bits, int windowed, bool resize_window)
{
    return DX8Wrapper::Set_Device_Resolution(width, height, bits, windowed, resize_window);
}

void Renderer::Get_Device_Resolution(int & width, int & height, int & bits, bool & windowed)
{
    DX8Wrapper::Get_Device_Resolution(width, height, bits, windowed);
}

void Renderer::Get_Render_Target_Resolution(int & width, int & height, int & bits, bool & windowed)
{
    DX8Wrapper::Get_Render_Target_Resolution(width, height, bits, windowed);
}

void Renderer::Set_Swap_Interval(long swap)
{
    DX8Wrapper::Set_Swap_Interval(swap);
}

int Renderer::Get_Swap_Interval()
{
    return DX8Wrapper::Get_Swap_Interval();
}

Renderer::DeviceStatus Renderer::Get_Device_Status()
{
    IDirect3DDevice8 * device = DX8Wrapper::_Get_D3D_Device8();
    if (device == nullptr)
        return DEVICE_UNAVAILABLE;

    const HRESULT result = device->TestCooperativeLevel();
    if (result == D3D_OK)
        return DEVICE_READY;
    if (result == D3DERR_DEVICELOST)
        return DEVICE_LOST;
    if (result == D3DERR_DEVICENOTRESET)
        return DEVICE_RESET_REQUIRED;
    return DEVICE_ERROR;
}

bool Renderer::Reset_Device()
{
    return DX8Wrapper::Reset_Device();
}

void Renderer::Set_Polygon_Mode(PolygonMode mode)
{
    switch (mode)
    {
    case POLYGON_POINT:
        DX8Wrapper::Set_DX8_Render_State(D3DRS_FILLMODE, D3DFILL_POINT);
        break;
    case POLYGON_LINE:
        DX8Wrapper::Set_DX8_Render_State(D3DRS_FILLMODE, D3DFILL_WIREFRAME);
        break;
    default:
        DX8Wrapper::Set_DX8_Render_State(D3DRS_FILLMODE, D3DFILL_SOLID);
        break;
    }
}


RenderCapabilities Renderer::Get_Capabilities()
{
    RenderCapabilities out;
    // Capability queries are valid only after the legacy device is created.
    // A default object advertises no optional features in headless mode.
    if (!DX8Wrapper::Is_Initted() || !DX8Wrapper::_Get_D3D_Device8())
        return out;

    const DX8Caps * caps = DX8Wrapper::Get_Current_Caps();
    out.hardwareTransformAndLighting = caps->Support_TnL();
    out.nPatches = caps->Support_NPatches();
    out.textureCompression = caps->Support_DXTC();
    out.anisotropicFiltering = caps->Support_Anisotropic_Filtering();
    out.cubeTextures = caps->Support_Cubemaps();
    out.bumpEnvironmentMapping = caps->Support_Bump_Envmap();
    out.maxSimultaneousTextures = static_cast<unsigned>(caps->Get_Max_Simultaneous_Textures());
    return out;
}

void Renderer::Set_Mesh_Camera(CameraClass * camera)
{
    TheDX8MeshRenderer.Set_Camera(camera);
}

void Renderer::Flush_Mesh_Batches()
{
    TheDX8MeshRenderer.Flush();
}

void Renderer::Clear_Pending_Mesh_Deletes()
{
    TheDX8MeshRenderer.Clear_Pending_Delete_Lists();
}

void Renderer::Invalidate_Mesh_Cache()
{
    TheDX8MeshRenderer.Invalidate();
}

void Renderer::Release_Texture_Resources()
{
    DX8TextureManagerClass::Shutdown();
}


// Transitional DX8 resource implementation. Keep native COM allocation,
// pool selection and usage bits in the backend. The caller still uses the
// native DX8 pointer for legacy drawing until binding and mapping migrate.
static DWORD Get_Legacy_Buffer_Usage(const RenderBufferDescription & description)
{
    DWORD flags = D3DUSAGE_WRITEONLY;
    if (description.usageFlags & RENDER_BUFFER_DYNAMIC)
        flags |= D3DUSAGE_DYNAMIC;
    if (description.usageFlags & RENDER_BUFFER_NPATCHES)
        flags |= D3DUSAGE_NPATCHES;
    if ((description.usageFlags & RENDER_BUFFER_SOFTWARE_VERTEX_PROCESSING) ||
        !Renderer::Get_Capabilities().hardwareTransformAndLighting)
        flags |= D3DUSAGE_SOFTWAREPROCESSING;
    return flags;
}

RenderVertexBufferHandle Renderer::Create_Vertex_Buffer(const RenderBufferDescription & description)
{
    DX8_THREAD_ASSERT();
    if (!description.Is_Valid() || !DX8Wrapper::_Get_D3D_Device8())
        return nullptr;

    IDirect3DVertexBuffer8 * buffer = nullptr;
    const HRESULT result = DX8Wrapper::_Get_D3D_Device8()->CreateVertexBuffer(
        description.byteSize,
        Get_Legacy_Buffer_Usage(description),
        description.legacyVertexFormat,
        description.Is_Dynamic() ? D3DPOOL_DEFAULT : D3DPOOL_MANAGED,
        &buffer);

    if (FAILED(result))
    {
        WWDEBUG_SAY(("Renderer::Create_Vertex_Buffer failed (HRESULT: 0x%08X)", (unsigned)result));
        return nullptr;
    }
    return reinterpret_cast<RenderVertexBufferHandle>(buffer);
}

RenderIndexBufferHandle Renderer::Create_Index_Buffer(const RenderBufferDescription & description)
{
    DX8_THREAD_ASSERT();
    if (!description.Is_Valid() || !DX8Wrapper::_Get_D3D_Device8())
        return nullptr;

    IDirect3DIndexBuffer8 * buffer = nullptr;
    const HRESULT result = DX8Wrapper::_Get_D3D_Device8()->CreateIndexBuffer(
        description.byteSize,
        Get_Legacy_Buffer_Usage(description),
        D3DFMT_INDEX16,
        description.Is_Dynamic() ? D3DPOOL_DEFAULT : D3DPOOL_MANAGED,
        &buffer);

    if (FAILED(result))
    {
        WWDEBUG_SAY(("Renderer::Create_Index_Buffer failed (HRESULT: 0x%08X)", (unsigned)result));
        return nullptr;
    }
    return reinterpret_cast<RenderIndexBufferHandle>(buffer);
}

void Renderer::Release_Vertex_Buffer(RenderVertexBufferHandle buffer)
{
    DX8_THREAD_ASSERT();
    if (buffer)
        reinterpret_cast<IDirect3DVertexBuffer8 *>(buffer)->Release();
}

void Renderer::Release_Index_Buffer(RenderIndexBufferHandle buffer)
{
    DX8_THREAD_ASSERT();
    if (buffer)
        reinterpret_cast<IDirect3DIndexBuffer8 *>(buffer)->Release();
}

void Renderer::Discard_Managed_Resources()
{
    DX8_THREAD_ASSERT();
    if (DX8Wrapper::_Get_D3D_Device8())
        DX8Wrapper::_Get_D3D_Device8()->ResourceManagerDiscardBytes(0);
}


static DWORD Translate_Buffer_Map_Flags(unsigned flags)
{
    DWORD result = 0;
    if (flags & RENDER_MAP_DISCARD)
        result |= D3DLOCK_DISCARD;
    if (flags & RENDER_MAP_NO_OVERWRITE)
        result |= D3DLOCK_NOOVERWRITE;
    if (flags & RENDER_MAP_NO_SYSTEM_LOCK)
        result |= D3DLOCK_NOSYSLOCK;
    if (flags & RENDER_MAP_READ_ONLY)
        result |= D3DLOCK_READONLY;
    return result;
}

void * Renderer::Map_Vertex_Buffer(RenderVertexBufferHandle buffer, unsigned offsetBytes,
                                   unsigned sizeBytes, unsigned mapFlags)
{
    DX8_THREAD_ASSERT();
    if (!buffer)
        return nullptr;
    unsigned char * mapped = nullptr;
    const HRESULT result = reinterpret_cast<IDirect3DVertexBuffer8 *>(buffer)->Lock(
        offsetBytes, sizeBytes, &mapped, Translate_Buffer_Map_Flags(mapFlags));
    DX8_ErrorCode(result);
    return SUCCEEDED(result) ? mapped : nullptr;
}

void * Renderer::Map_Index_Buffer(RenderIndexBufferHandle buffer, unsigned offsetBytes,
                                  unsigned sizeBytes, unsigned mapFlags)
{
    DX8_THREAD_ASSERT();
    if (!buffer)
        return nullptr;
    unsigned char * mapped = nullptr;
    const HRESULT result = reinterpret_cast<IDirect3DIndexBuffer8 *>(buffer)->Lock(
        offsetBytes, sizeBytes, &mapped, Translate_Buffer_Map_Flags(mapFlags));
    DX8_ErrorCode(result);
    return SUCCEEDED(result) ? mapped : nullptr;
}

void Renderer::Unmap_Vertex_Buffer(RenderVertexBufferHandle buffer)
{
    DX8_THREAD_ASSERT();
    if (buffer)
        DX8_ErrorCode(reinterpret_cast<IDirect3DVertexBuffer8 *>(buffer)->Unlock());
}

void Renderer::Unmap_Index_Buffer(RenderIndexBufferHandle buffer)
{
    DX8_THREAD_ASSERT();
    if (buffer)
        DX8_ErrorCode(reinterpret_cast<IDirect3DIndexBuffer8 *>(buffer)->Unlock());
}


void Renderer::Bind_Vertex_Buffer(RenderVertexBufferHandle buffer, unsigned stream, unsigned stride)
{
    DX8_THREAD_ASSERT();
    DX8CALL(SetStreamSource(stream, reinterpret_cast<IDirect3DVertexBuffer8 *>(buffer), stride));
}

void Renderer::Bind_Index_Buffer(RenderIndexBufferHandle buffer, unsigned baseVertexIndex)
{
    DX8_THREAD_ASSERT();
    DX8CALL(SetIndices(reinterpret_cast<IDirect3DIndexBuffer8 *>(buffer), baseVertexIndex));
}
