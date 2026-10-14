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

void Renderer::Set_Shader(const ShaderClass & shader)
{
    DX8Wrapper::Set_Shader(shader);
}

void Renderer::Set_Material(const VertexMaterialClass * material)
{
    DX8Wrapper::Set_Material(material);
}

void Renderer::Set_Texture(unsigned stage, TextureBaseClass * texture)
{
    DX8Wrapper::Set_Texture(stage, texture);
}

void Renderer::Apply_Pending_State_Changes()
{
    DX8Wrapper::Apply_Render_State_Changes();
}

static D3DTRANSFORMSTATETYPE To_DX8_Transform(RenderTransform transform)
{
    switch (transform)
    {
    case RENDER_TRANSFORM_VIEW: return D3DTS_VIEW;
    case RENDER_TRANSFORM_PROJECTION: return D3DTS_PROJECTION;
    case RENDER_TRANSFORM_WORLD:
    default: return D3DTS_WORLD;
    }
}

void Renderer::Set_Transform(RenderTransform transform, const Matrix3D & matrix)
{
    DX8Wrapper::Set_Transform(To_DX8_Transform(transform), matrix);
}

void Renderer::Set_Transform(RenderTransform transform, const Matrix4x4 & matrix)
{
    DX8Wrapper::Set_Transform(To_DX8_Transform(transform), matrix);
}

void Renderer::Get_Transform(RenderTransform transform, Matrix4x4 & matrix)
{
    DX8Wrapper::Get_Transform(To_DX8_Transform(transform), matrix);
}

void Renderer::Set_World_Identity()
{
    DX8Wrapper::Set_World_Identity();
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


static D3DPOOL Get_Legacy_Texture_Memory(RenderTextureMemory memory)
{
    switch (memory)
    {
    case RENDER_TEXTURE_DEFAULT:
        return D3DPOOL_DEFAULT;
    case RENDER_TEXTURE_SYSTEM:
        return D3DPOOL_SYSTEMMEM;
    case RENDER_TEXTURE_MANAGED:
        return D3DPOOL_MANAGED;
    default:
        WWASSERT(0);
        return D3DPOOL_DEFAULT;
    }
}

RenderTexture2DHandle Renderer::Create_Texture_2D(const RenderTexture2DDescription & description)
{
    DX8_THREAD_ASSERT();
    if (!description.Is_Valid())
        return nullptr;

    // Preserve DX8's format negotiation, render-target creation and
    // out-of-video-memory recovery inside the established wrapper.
    IDirect3DTexture8 * texture = DX8Wrapper::_Create_DX8_Texture(
        description.width, description.height,
        static_cast<WW3DFormat>(description.pixelFormat),
        static_cast<MipCountType>(description.mipLevels),
        Get_Legacy_Texture_Memory(description.memory),
        description.renderTarget);
    return reinterpret_cast<RenderTexture2DHandle>(texture);
}

RenderTexture2DHandle Renderer::Create_Depth_Texture_2D(const RenderTexture2DDescription & description)
{
    DX8_THREAD_ASSERT();
    if (!description.Is_Valid())
        return nullptr;

    IDirect3DTexture8 * texture = DX8Wrapper::_Create_DX8_ZTexture(
        description.width, description.height,
        static_cast<WW3DZFormat>(description.pixelFormat),
        static_cast<MipCountType>(description.mipLevels),
        Get_Legacy_Texture_Memory(description.memory));
    return reinterpret_cast<RenderTexture2DHandle>(texture);
}


// Texture binding is the one point where W3D texture stage selection is
// translated into DX8 device state. This forwards through the existing
// DX8Wrapper state cache, preserving AddRef/Release and change suppression.
void Renderer::Bind_Texture(unsigned stage, RenderTextureHandle texture)
{
    DX8Wrapper::Set_DX8_Texture(stage, reinterpret_cast<IDirect3DBaseTexture8 *>(texture));
}

void Renderer::Retain_Texture(RenderTextureHandle texture)
{
    // Texture loading and invalidation can occur outside the render thread.
    // Only reference count manipulation is permitted here.
    if (texture)
        reinterpret_cast<IDirect3DBaseTexture8 *>(texture)->AddRef();
}

void Renderer::Release_Texture(RenderTextureHandle texture)
{
    if (texture)
        reinterpret_cast<IDirect3DBaseTexture8 *>(texture)->Release();
}


RenderTextureCubeHandle Renderer::Create_Texture_Cube(const RenderTextureCubeDescription & description)
{
    DX8_THREAD_ASSERT();
    if (!description.Is_Valid())
        return nullptr;

    IDirect3DCubeTexture8 * texture = DX8Wrapper::_Create_DX8_Cube_Texture(
        description.edgeLength, description.edgeLength,
        static_cast<WW3DFormat>(description.pixelFormat),
        static_cast<MipCountType>(description.mipLevels),
        Get_Legacy_Texture_Memory(description.memory),
        description.renderTarget);
    return reinterpret_cast<RenderTextureCubeHandle>(texture);
}

RenderTextureVolumeHandle Renderer::Create_Texture_Volume(const RenderTextureVolumeDescription & description)
{
    DX8_THREAD_ASSERT();
    if (!description.Is_Valid())
        return nullptr;

    // DX8 volume textures do not support the cube/2D render-target flag.
    IDirect3DVolumeTexture8 * texture = DX8Wrapper::_Create_DX8_Volume_Texture(
        description.width, description.height, description.depth,
        static_cast<WW3DFormat>(description.pixelFormat),
        static_cast<MipCountType>(description.mipLevels),
        Get_Legacy_Texture_Memory(description.memory));
    return reinterpret_cast<RenderTextureVolumeHandle>(texture);
}


RenderSurfaceHandle Renderer::Create_Surface(const RenderSurfaceDescription & description)
{
    DX8_THREAD_ASSERT();
    if (!description.Is_Valid())
        return nullptr;

    return reinterpret_cast<RenderSurfaceHandle>(DX8Wrapper::_Create_DX8_Surface(
        description.width, description.height,
        static_cast<WW3DFormat>(description.pixelFormat)));
}

RenderSurfaceHandle Renderer::Load_Surface(const char * filename)
{
    DX8_THREAD_ASSERT();
    if (!filename)
        return nullptr;

    // Preserve the legacy file-loader path and missing-image fallback.
    return reinterpret_cast<RenderSurfaceHandle>(
        DX8Wrapper::_Create_DX8_Surface(filename));
}

void Renderer::Describe_Surface(RenderSurfaceHandle surface, RenderSurfaceDescription & description)
{
    description = RenderSurfaceDescription();
    if (!surface)
        return;

    D3DSURFACE_DESC nativeDescription;
    ::ZeroMemory(&nativeDescription, sizeof(nativeDescription));
    DX8_ErrorCode(reinterpret_cast<IDirect3DSurface8 *>(surface)->GetDesc(&nativeDescription));
    description.pixelFormat = static_cast<unsigned>(D3DFormat_To_WW3DFormat(nativeDescription.Format));
    description.width = nativeDescription.Width;
    description.height = nativeDescription.Height;
}

void Renderer::Retain_Surface(RenderSurfaceHandle surface)
{
    if (surface)
        reinterpret_cast<IDirect3DSurface8 *>(surface)->AddRef();
}

void Renderer::Release_Surface(RenderSurfaceHandle surface)
{
    if (surface)
        reinterpret_cast<IDirect3DSurface8 *>(surface)->Release();
}


static D3DTEXTUREFILTERTYPE To_DX8_Sampler_Filter(RenderSamplerFilter filter)
{
    switch (filter)
    {
    case RENDER_SAMPLER_NONE:
        return D3DTEXF_NONE;
    case RENDER_SAMPLER_LINEAR:
        return D3DTEXF_LINEAR;
    case RENDER_SAMPLER_ANISOTROPIC:
        return D3DTEXF_ANISOTROPIC;
    case RENDER_SAMPLER_POINT:
    default:
        return D3DTEXF_POINT;
    }
}

void Renderer::Apply_Texture_Sampler(unsigned stage, const RenderSamplerDescription & sampler)
{
    // Preserve DX8's exact per-stage state order and cache invalidation.
    DX8Wrapper::Set_DX8_Texture_Stage_State(
        stage, D3DTSS_MINFILTER, To_DX8_Sampler_Filter(sampler.minFilter));
    DX8Wrapper::Set_DX8_Texture_Stage_State(
        stage, D3DTSS_MAGFILTER, To_DX8_Sampler_Filter(sampler.magFilter));
    DX8Wrapper::Set_DX8_Texture_Stage_State(
        stage, D3DTSS_MIPFILTER, To_DX8_Sampler_Filter(sampler.mipFilter));
    DX8Wrapper::Set_DX8_Texture_Stage_State(
        stage, D3DTSS_ADDRESSU,
        sampler.addressU == RENDER_SAMPLER_REPEAT ? D3DTADDRESS_WRAP : D3DTADDRESS_CLAMP);
    DX8Wrapper::Set_DX8_Texture_Stage_State(
        stage, D3DTSS_ADDRESSV,
        sampler.addressV == RENDER_SAMPLER_REPEAT ? D3DTADDRESS_WRAP : D3DTADDRESS_CLAMP);
}

void Renderer::Set_Texture_Max_Anisotropy(unsigned stage, unsigned level)
{
    DX8Wrapper::Set_DX8_Texture_Stage_State(stage, D3DTSS_MAXANISOTROPY, level);
}


static D3DBLEND To_DX8_Blend(RenderBlendFactor factor)
{
    switch (factor)
    {
    case RENDER_BLEND_ZERO: return D3DBLEND_ZERO;
    case RENDER_BLEND_SRC_COLOR: return D3DBLEND_SRCCOLOR;
    case RENDER_BLEND_INV_SRC_COLOR: return D3DBLEND_INVSRCCOLOR;
    case RENDER_BLEND_SRC_ALPHA: return D3DBLEND_SRCALPHA;
    case RENDER_BLEND_INV_SRC_ALPHA: return D3DBLEND_INVSRCALPHA;
    case RENDER_BLEND_DEST_COLOR: return D3DBLEND_DESTCOLOR;
    case RENDER_BLEND_DEST_ALPHA: return D3DBLEND_DESTALPHA;
    case RENDER_BLEND_INV_DEST_ALPHA: return D3DBLEND_INVDESTALPHA;
    case RENDER_BLEND_ONE:
    default: return D3DBLEND_ONE;
    }
}

static D3DCMPFUNC To_DX8_Compare(RenderCompareFunc compare)
{
    switch (compare)
    {
    case RENDER_COMPARE_NEVER: return D3DCMP_NEVER;
    case RENDER_COMPARE_LESS: return D3DCMP_LESS;
    case RENDER_COMPARE_EQUAL: return D3DCMP_EQUAL;
    case RENDER_COMPARE_GREATER: return D3DCMP_GREATER;
    case RENDER_COMPARE_NOT_EQUAL: return D3DCMP_NOTEQUAL;
    case RENDER_COMPARE_GREATER_EQUAL: return D3DCMP_GREATEREQUAL;
    case RENDER_COMPARE_ALWAYS: return D3DCMP_ALWAYS;
    case RENDER_COMPARE_LESS_EQUAL:
    default: return D3DCMP_LESSEQUAL;
    }
}

void Renderer::Apply_Blend_State(const RenderBlendState & state)
{
    // Original ShaderClass::Apply sets the factors only when blending is
    // active, followed by the enable bit, alpha reference / compare and
    // alpha-test enable. Preserve that exact order for the DX8 state cache.
    if (state.blendEnabled)
    {
        DX8Wrapper::Set_DX8_Render_State(D3DRS_SRCBLEND, To_DX8_Blend(state.source));
        DX8Wrapper::Set_DX8_Render_State(D3DRS_DESTBLEND, To_DX8_Blend(state.destination));
    }
    DX8Wrapper::Set_DX8_Render_State(D3DRS_ALPHABLENDENABLE, state.blendEnabled);

    if (state.alphaTestEnabled)
    {
        DX8Wrapper::Set_DX8_Render_State(D3DRS_ALPHAREF, state.alphaReference);
        DX8Wrapper::Set_DX8_Render_State(D3DRS_ALPHAFUNC, To_DX8_Compare(state.alphaCompare));
    }
    DX8Wrapper::Set_DX8_Render_State(D3DRS_ALPHATESTENABLE, state.alphaTestEnabled);
}

void Renderer::Set_Depth_Compare(RenderCompareFunc compare)
{
    DX8Wrapper::Set_DX8_Render_State(D3DRS_ZFUNC, To_DX8_Compare(compare));
}

void Renderer::Set_Depth_Write(bool enabled)
{
    DX8Wrapper::Set_DX8_Render_State(D3DRS_ZWRITEENABLE, enabled);
}

void Renderer::Set_Cull_Mode(RenderCullMode cullMode)
{
    D3DCULL cull = D3DCULL_NONE;
    if (cullMode == RENDER_CULL_CW)
        cull = D3DCULL_CW;
    else if (cullMode == RENDER_CULL_CCW)
        cull = D3DCULL_CCW;
    DX8Wrapper::Set_DX8_Render_State(D3DRS_CULLMODE, cull);
}

void Renderer::Set_Alpha_Test_Enabled(bool enabled)
{
    DX8Wrapper::Set_DX8_Render_State(D3DRS_ALPHATESTENABLE, enabled);
}


static D3DMATERIALCOLORSOURCE To_DX8_Material_Source(RenderMaterialColorSource source)
{
    switch (source)
    {
    case RENDER_VERTEX_COLOR_1: return D3DMCS_COLOR1;
    case RENDER_VERTEX_COLOR_2: return D3DMCS_COLOR2;
    case RENDER_MATERIAL_COLOR:
    default: return D3DMCS_MATERIAL;
    }
}

void Renderer::Set_Vertex_Lighting(bool enabled)
{
    DX8Wrapper::Set_DX8_Render_State(D3DRS_LIGHTING, enabled);
}

void Renderer::Set_Vertex_Color_Sources(const RenderMaterialColorSources & sources)
{
    // Preserve the original per-field update order in VertexMaterialClass.
    DX8Wrapper::Set_DX8_Render_State(
        D3DRS_AMBIENTMATERIALSOURCE, To_DX8_Material_Source(sources.ambient));
    DX8Wrapper::Set_DX8_Render_State(
        D3DRS_DIFFUSEMATERIALSOURCE, To_DX8_Material_Source(sources.diffuse));
    DX8Wrapper::Set_DX8_Render_State(
        D3DRS_EMISSIVEMATERIALSOURCE, To_DX8_Material_Source(sources.emissive));
}

void Renderer::Set_Texture_UV_Passthrough(unsigned stage, unsigned uvIndex)
{
    DX8Wrapper::Set_DX8_Texture_Stage_State(
        stage, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU | uvIndex);
    DX8Wrapper::Set_DX8_Texture_Stage_State(
        stage, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
}


static D3DCOLORVALUE To_DX8_Material_Color(const RenderMaterialColor & color)
{
    D3DCOLORVALUE nativeColor = {};
    nativeColor.r = color.r;
    nativeColor.g = color.g;
    nativeColor.b = color.b;
    nativeColor.a = color.a;
    return nativeColor;
}

void Renderer::Set_Vertex_Material(const RenderVertexMaterialDescription & material)
{
    // The DX8 wrapper forwards this immediately to IDirect3DDevice8::SetMaterial.
    // The transient stack allocation is therefore safe, and the material
    // value has no native binary-format dependencies in the W3D interface.
    D3DMATERIAL8 nativeMaterial = {};
    nativeMaterial.Diffuse = To_DX8_Material_Color(material.diffuse);
    nativeMaterial.Ambient = To_DX8_Material_Color(material.ambient);
    nativeMaterial.Specular = To_DX8_Material_Color(material.specular);
    nativeMaterial.Emissive = To_DX8_Material_Color(material.emissive);
    nativeMaterial.Power = material.power;
    DX8Wrapper::Set_DX8_Material(&nativeMaterial);
}


static D3DTEXTUREOP To_DX8_Texture_Operation(RenderTextureOperation operation)
{
    switch (operation)
    {
    case RENDER_TEXOP_DISABLE: return D3DTOP_DISABLE;
    case RENDER_TEXOP_SELECT_ARG1: return D3DTOP_SELECTARG1;
    case RENDER_TEXOP_SELECT_ARG2: return D3DTOP_SELECTARG2;
    case RENDER_TEXOP_MODULATE: return D3DTOP_MODULATE;
    case RENDER_TEXOP_MODULATE2X: return D3DTOP_MODULATE2X;
    case RENDER_TEXOP_ADD: return D3DTOP_ADD;
    case RENDER_TEXOP_ADD_SMOOTH: return D3DTOP_ADDSMOOTH;
    case RENDER_TEXOP_SUBTRACT: return D3DTOP_SUBTRACT;
    case RENDER_TEXOP_BLEND_TEXTURE_ALPHA: return D3DTOP_BLENDTEXTUREALPHA;
    case RENDER_TEXOP_BLEND_CURRENT_ALPHA: return D3DTOP_BLENDCURRENTALPHA;
    case RENDER_TEXOP_ADD_SIGNED: return D3DTOP_ADDSIGNED;
    case RENDER_TEXOP_ADD_SIGNED2X: return D3DTOP_ADDSIGNED2X;
    case RENDER_TEXOP_MODULATE_ALPHA_ADD_COLOR: return D3DTOP_MODULATEALPHA_ADDCOLOR;
    case RENDER_TEXOP_BUMP_ENV_MAP: return D3DTOP_BUMPENVMAP;
    case RENDER_TEXOP_BUMP_ENV_MAP_LUMINANCE: return D3DTOP_BUMPENVMAPLUMINANCE;
    default:
        WWASSERT(0);
        return D3DTOP_DISABLE;
    }
}

static DWORD To_DX8_Texture_Argument(RenderTextureArgument argument)
{
    switch (argument)
    {
    case RENDER_TEXARG_CURRENT: return D3DTA_CURRENT;
    case RENDER_TEXARG_TEXTURE: return D3DTA_TEXTURE;
    case RENDER_TEXARG_DIFFUSE: return D3DTA_DIFFUSE;
    case RENDER_TEXARG_TEXTURE_FACTOR: return D3DTA_TFACTOR;
    default:
        WWASSERT(0);
        return D3DTA_CURRENT;
    }
}

static void Apply_DX8_Texture_Channel(
    unsigned stage,
    const RenderTextureStageChannel & channel,
    D3DTEXTURESTAGESTATETYPE opState,
    D3DTEXTURESTAGESTATETYPE arg1State,
    D3DTEXTURESTAGESTATETYPE arg2State,
    bool uncached)
{
    const D3DTEXTUREOP operation = To_DX8_Texture_Operation(channel.operation);
    const DWORD arg1 = To_DX8_Texture_Argument(channel.argument1);
    const DWORD arg2 = To_DX8_Texture_Argument(channel.argument2);

    // The original shader modifies states in operation, arg1, arg2 order.
    // Stage 2 on Voodoo3 intentionally bypasses DX8Wrapper's two-stage
    // cache; preserve the direct calls for that legacy hardware only.
    if (uncached)
    {
        DX8CALL(SetTextureStageState(stage, opState, operation));
        DX8CALL(SetTextureStageState(stage, arg1State, arg1));
        DX8CALL(SetTextureStageState(stage, arg2State, arg2));
    }
    else
    {
        DX8Wrapper::Set_DX8_Texture_Stage_State(stage, opState, operation);
        DX8Wrapper::Set_DX8_Texture_Stage_State(stage, arg1State, arg1);
        DX8Wrapper::Set_DX8_Texture_Stage_State(stage, arg2State, arg2);
    }
}

void Renderer::Apply_Texture_Stage_Color(
    unsigned stage, const RenderTextureStageChannel & channel, bool uncached)
{
    Apply_DX8_Texture_Channel(stage, channel, D3DTSS_COLOROP,
                             D3DTSS_COLORARG1, D3DTSS_COLORARG2, uncached);
}

void Renderer::Apply_Texture_Stage_Alpha(
    unsigned stage, const RenderTextureStageChannel & channel, bool uncached)
{
    Apply_DX8_Texture_Channel(stage, channel, D3DTSS_ALPHAOP,
                             D3DTSS_ALPHAARG1, D3DTSS_ALPHAARG2, uncached);
}

// Single-field texture and blend bindings preserve the existing DX8 state
// cache and ordering. No native D3D values escape the Renderer contract.
void Renderer::Set_Texture_Filter(unsigned stage, RenderSamplerFilterField field,
                                  RenderSamplerFilter filter)
{
    D3DTEXTURESTAGESTATETYPE nativeField = D3DTSS_MINFILTER;
    switch (field)
    {
    case RENDER_SAMPLER_MAG_FILTER: nativeField = D3DTSS_MAGFILTER; break;
    case RENDER_SAMPLER_MIP_FILTER: nativeField = D3DTSS_MIPFILTER; break;
    case RENDER_SAMPLER_MIN_FILTER:
    default: break;
    }
    DX8Wrapper::Set_DX8_Texture_Stage_State(stage, nativeField,
                                             To_DX8_Sampler_Filter(filter));
}

void Renderer::Set_Texture_Address(unsigned stage, RenderSamplerAxis axis,
                                   RenderSamplerAddress address)
{
    D3DTEXTURESTAGESTATETYPE nativeField = D3DTSS_ADDRESSU;
    switch (axis)
    {
    case RENDER_SAMPLER_AXIS_V: nativeField = D3DTSS_ADDRESSV; break;
    case RENDER_SAMPLER_AXIS_W: nativeField = D3DTSS_ADDRESSW; break;
    case RENDER_SAMPLER_AXIS_U:
    default: break;
    }
    DX8Wrapper::Set_DX8_Texture_Stage_State(stage, nativeField,
        address == RENDER_SAMPLER_REPEAT ? D3DTADDRESS_WRAP : D3DTADDRESS_CLAMP);
}

void Renderer::Set_Source_Blend(RenderBlendFactor factor)
{
    DX8Wrapper::Set_DX8_Render_State(D3DRS_SRCBLEND, To_DX8_Blend(factor));
}

void Renderer::Set_Destination_Blend(RenderBlendFactor factor)
{
    DX8Wrapper::Set_DX8_Render_State(D3DRS_DESTBLEND, To_DX8_Blend(factor));
}

void Renderer::Set_Blending_Enabled(bool enabled)
{
    DX8Wrapper::Set_DX8_Render_State(D3DRS_ALPHABLENDENABLE, enabled);
}

void Renderer::Set_Texture_Factor(unsigned packedARGB)
{
    DX8Wrapper::Set_DX8_Render_State(D3DRS_TEXTUREFACTOR, packedARGB);
}
