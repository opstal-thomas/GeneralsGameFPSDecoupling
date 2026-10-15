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
#include "WW3D2/vertmaterial.h"
#include <cstring>
#include "WW3D2/dx8renderer.h"
#include "WW3D2/dx8caps.h"
#include "WW3D2/dx8texman.h"
#include "WW3D2/formconv.h"
#include "WWMath/vector3.h"
#include "WW3D2/lightenvironment.h"
#include "WWDebug/wwdebug.h"
#include "WWLib/thread.h"
#include <d3dx8.h>

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

bool Renderer::Is_Initialized()
{
    return DX8Wrapper::Is_Initted();
}

unsigned Renderer::Get_Render_Thread_ID()
{
    return DX8Wrapper::_Get_Main_Thread_ID();
}

bool Renderer::Has_Active_Device()
{
    return DX8Wrapper::_Get_D3D_Device8() != nullptr;
}

void Renderer::Set_Gamma(float gamma, float bright, float contrast, bool calibrate, bool uselimit)
{
    DX8Wrapper::Set_Gamma(gamma, bright, contrast, calibrate, uselimit);
}

void Renderer::Set_Hardware_Cursor_Visible(bool visible)
{
    IDirect3DDevice8 * device = DX8Wrapper::_Get_D3D_Device8();
    if (device)
        device->ShowCursor(visible ? TRUE : FALSE);
}

bool Renderer::Set_Hardware_Cursor_Image(unsigned hotspotX, unsigned hotspotY,
                                         RenderSurfaceHandle surface)
{
    IDirect3DDevice8 * device = DX8Wrapper::_Get_D3D_Device8();
    return device && device->SetCursorProperties(hotspotX, hotspotY,
        reinterpret_cast<IDirect3DSurface8 *>(surface)) == S_OK;
}

void Renderer::Set_Hardware_Cursor_Position(int x, int y)
{
    IDirect3DDevice8 * device = DX8Wrapper::_Get_D3D_Device8();
    if (device)
        device->SetCursorPosition(x, y, D3DCURSOR_IMMEDIATE_UPDATE);
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

unsigned Renderer::Pack_Color_ARGB(const Vector3 & color, float alpha)
{
    return DX8Wrapper::Convert_Color(color, alpha);
}

unsigned Renderer::Pack_Color_ARGB(const Vector4 & color)
{
    return DX8Wrapper::Convert_Color(color);
}

unsigned Renderer::Pack_Color_ARGB_Clamp(const Vector4 & color)
{
    return DX8Wrapper::Convert_Color_Clamp(color);
}

void Renderer::Draw_Triangles(unsigned short startIndex, unsigned short triangleCount,
                              unsigned short minVertexIndex, unsigned short vertexCount)
{
    DX8Wrapper::Draw_Triangles(startIndex, triangleCount, minVertexIndex, vertexCount);
}

void Renderer::Capture_Sorting_State(RenderStateStruct & state)
{
    DX8Wrapper::Get_Render_State(state);
}

void Renderer::Restore_Sorting_State(const RenderStateStruct & state)
{
    DX8Wrapper::Set_Render_State(state);
}

void Renderer::Release_Sorting_State()
{
    DX8Wrapper::Release_Render_State();
}

static void Apply_DX8_Sorting_Light(unsigned index, const RenderSortingLight * light)
{
    if (!light)
    {
        DX8Wrapper::Set_DX8_Light(index, nullptr);
        return;
    }
    D3DLIGHT8 nativeLight = DX8_Light_From_Render_Sorting(*light);
    DX8Wrapper::Set_DX8_Light(index, &nativeLight);
}

void Renderer::Apply_Sorting_Transforms_And_Lights(RenderStateStruct & render_state)
{
    DX8Wrapper::_Set_DX8_Transform(D3DTS_WORLD, DX8_Matrix_From_Render_Sorting(render_state.world));
    DX8Wrapper::_Set_DX8_Transform(D3DTS_VIEW, DX8_Matrix_From_Render_Sorting(render_state.view));


    if (!render_state.material->Get_Lighting())
    	return;	//no point changing lights if they are ignored.
  //prevLight = render_state.lightsHash;

    if (render_state.LightEnable[0]) {
    	Apply_DX8_Sorting_Light(0,&render_state.Lights[0]);
    	if (render_state.LightEnable[1]) {
    		Apply_DX8_Sorting_Light(1,&render_state.Lights[1]);
    		if (render_state.LightEnable[2]) {
    			Apply_DX8_Sorting_Light(2,&render_state.Lights[2]);
    			if (render_state.LightEnable[3]) {
    				Apply_DX8_Sorting_Light(3,&render_state.Lights[3]);
    			}
    			else {
    				Apply_DX8_Sorting_Light(3,nullptr);
    			}
    		}
    		else {
    			Apply_DX8_Sorting_Light(2,nullptr);
    		}
    	}
    	else {
    		Apply_DX8_Sorting_Light(1,nullptr);
    	}
    }
    else {
    	Apply_DX8_Sorting_Light(0,nullptr);
    }
}

bool Renderer::Is_Triangle_Draw_Enabled()
{
    return DX8Wrapper::_Is_Triangle_Draw_Enabled();
}

void Renderer::Set_Triangle_Draw_Enabled(bool enabled)
{
    DX8Wrapper::_Enable_Triangle_Draw(enabled);
}

void Renderer::Unbind_Sorting_Geometry()
{
    DX8Wrapper::Set_Index_Buffer(nullptr, 0);
    DX8Wrapper::Set_Vertex_Buffer(nullptr);
}

void Renderer::Draw_Transformed_Quad(const void * vertices, unsigned vertexStride,
                                     unsigned uvSets)
{
    const unsigned requiredStride = 20u + uvSets * 8u;
    WWASSERT(vertices && (uvSets == 1 || uvSets == 2) &&
             vertexStride >= requiredStride);
    if (!vertices || (uvSets != 1 && uvSets != 2) ||
        vertexStride < requiredStride)
        return;

    IDirect3DDevice8 * device = DX8Wrapper::_Get_D3D_Device8();
    if (!device)
        return;
    // The legacy screen filters used SetVertexShader(FVF) followed by
    // DrawPrimitiveUP. Binding the FVF here preserves their exact layout,
    // even after a shader pass modifies the device's vertex program.
    const DWORD fvf = D3DFVF_XYZRHW | D3DFVF_DIFFUSE |
        (uvSets == 2 ? D3DFVF_TEX2 : D3DFVF_TEX1);
    DX8_ErrorCode(device->SetVertexShader(fvf));
    DX8_ErrorCode(device->DrawPrimitiveUP(
        D3DPT_TRIANGLESTRIP, 2, vertices, vertexStride));
}

void Renderer::Bind_Immediate_Texture(unsigned stage, RenderTextureHandle texture)
{
    IDirect3DDevice8 * device = DX8Wrapper::_Get_D3D_Device8();
    if (device)
        device->SetTexture(stage, reinterpret_cast<IDirect3DBaseTexture8 *>(texture));
}

void Renderer::Set_Immediate_Stage_Alpha_Current(unsigned stage)
{
    IDirect3DDevice8 * device = DX8Wrapper::_Get_D3D_Device8();
    if (!device)
        return;
    device->SetTextureStageState(stage, D3DTSS_ALPHAARG1, D3DTA_CURRENT);
    device->SetTextureStageState(stage, D3DTSS_ALPHAARG2, D3DTA_TEXTURE);
    device->SetTextureStageState(stage, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
}

void Renderer::Set_Immediate_Alpha_Blending(bool enabled)
{
    IDirect3DDevice8 * device = DX8Wrapper::_Get_D3D_Device8();
    if (device)
        device->SetRenderState(D3DRS_ALPHABLENDENABLE, enabled ? TRUE : FALSE);
}

bool Renderer::Create_Legacy_Shader(bool vertexShader, const void * declaration,
                                    const void * bytecode, unsigned usage, unsigned & handle)
{
    IDirect3DDevice8 * device = DX8Wrapper::_Get_D3D_Device8();
    handle = 0;
    if (!device || !bytecode || (vertexShader && !declaration))
        return false;

    DWORD nativeHandle = 0;
    const HRESULT hr = vertexShader
        ? device->CreateVertexShader(static_cast<const DWORD *>(declaration),
                                     static_cast<const DWORD *>(bytecode), &nativeHandle, usage)
        : device->CreatePixelShader(static_cast<const DWORD *>(bytecode), &nativeHandle);
    if (hr != S_OK)
        return false;
    handle = static_cast<unsigned>(nativeHandle);
    return true;
}

bool Renderer::Select_Legacy_Pixel_Shader(unsigned handle)
{
    IDirect3DDevice8 * device = DX8Wrapper::_Get_D3D_Device8();
    return device && device->SetPixelShader(static_cast<DWORD>(handle)) == S_OK;
}

void Renderer::Delete_Legacy_Pixel_Shader(unsigned handle)
{
    IDirect3DDevice8 * device = DX8Wrapper::_Get_D3D_Device8();
    if (device)
        device->DeletePixelShader(static_cast<DWORD>(handle));
}

void Renderer::Set_Legacy_Pixel_Shader_Vector(unsigned registerIndex,
                                              float x, float y, float z, float w)
{
    IDirect3DDevice8 * device = DX8Wrapper::_Get_D3D_Device8();
    if (!device)
        return;
    const float value[4] = {x, y, z, w};
    device->SetPixelShaderConstant(registerIndex, value, 1);
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

void Renderer::Set_Texture_Transform(unsigned stage, const float matrix[16])
{
    WWASSERT(stage < 8 && matrix);
    if (stage >= 8 || !matrix)
        return;
    D3DMATRIX nativeMatrix = {};
    static_assert(sizeof(nativeMatrix) == sizeof(float) * 16, "Invalid DX8 matrix layout");
    std::memcpy(&nativeMatrix, matrix, sizeof(nativeMatrix));
    DX8Wrapper::_Set_DX8_Transform(
        static_cast<D3DTRANSFORMSTATETYPE>(D3DTS_TEXTURE0 + stage), nativeMatrix);
}

void Renderer::Get_Transform_Elements(RenderTransform transform, float matrix[16])
{
    WWASSERT(matrix);
    if (!matrix)
        return;
    D3DMATRIX nativeMatrix = {};
    DX8Wrapper::_Get_DX8_Transform(To_DX8_Transform(transform), nativeMatrix);
    std::memcpy(matrix, &nativeMatrix, sizeof(nativeMatrix));
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


bool Renderer::Get_Adapter_Identification(RenderAdapterIdentification & identification)
{
    identification = RenderAdapterIdentification();
    IDirect3D8 * interface8 = DX8Wrapper::_Get_D3D8();
    if (!interface8 || !DX8Wrapper::_Get_D3D_Device8())
        return false;

    // Preserve legacy adapter-zero vendor classification and driver version.
    D3DADAPTER_IDENTIFIER8 native = {};
    interface8->GetAdapterIdentifier(0, D3DENUM_NO_WHQL_LEVEL, &native);
    identification.vendorId = native.VendorId;
    identification.deviceId = native.DeviceId;
    identification.driverVersion = static_cast<unsigned long long>(native.DriverVersion.QuadPart);
    return true;
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
    out.pointSprites = caps->Support_PointSprites();
    out.dot3TextureOperation = caps->Support_Dot3();
    out.pixelShaderMajorVersion = static_cast<unsigned>(caps->Get_Pixel_Shader_Major_Version());
    out.pixelShaderMinorVersion = static_cast<unsigned>(caps->Get_Pixel_Shader_Minor_Version());
    out.maxSimultaneousTextures = static_cast<unsigned>(caps->Get_Max_Simultaneous_Textures());
    out.maxTexturesPerPass = static_cast<unsigned>(caps->Get_Max_Textures_Per_Pass());
    const D3DCAPS8 & native = caps->Get_DX8_Caps();
    out.maxTextureWidth = native.MaxTextureWidth;
    out.maxTextureHeight = native.MaxTextureHeight;
    out.maxVolumeExtent = native.MaxVolumeExtent;
    out.maxTextureAspectRatio = native.MaxTextureAspectRatio;
    out.linearMinFilter = (native.TextureFilterCaps & D3DPTFILTERCAPS_MINFLINEAR) != 0;
    out.linearMagFilter = (native.TextureFilterCaps & D3DPTFILTERCAPS_MAGFLINEAR) != 0;
    out.linearMipFilter = (native.TextureFilterCaps & D3DPTFILTERCAPS_MIPFLINEAR) != 0;
    out.anisotropicMinFilter = (native.TextureFilterCaps & D3DPTFILTERCAPS_MINFANISOTROPIC) != 0;
    out.anisotropicMagFilter = (native.TextureFilterCaps & D3DPTFILTERCAPS_MAGFANISOTROPIC) != 0;
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

RenderVertexBufferHandle Renderer::Create_Point_Sprite_Vertex_Buffer(unsigned byteSize)
{
    DX8_THREAD_ASSERT();
    IDirect3DDevice8 * device = DX8Wrapper::_Get_D3D_Device8();
    if (!device || !byteSize)
        return nullptr;

    IDirect3DVertexBuffer8 * buffer = nullptr;
    // Preserve snow's original WRITEONLY|DYNAMIC|POINTS, XYZ-only FVF and
    // DEFAULT-pool allocation. Do not add other buffer usage flags.
    const HRESULT result = device->CreateVertexBuffer(
        byteSize, D3DUSAGE_WRITEONLY | D3DUSAGE_DYNAMIC | D3DUSAGE_POINTS,
        D3DFVF_XYZ, D3DPOOL_DEFAULT, &buffer);
    DX8_ErrorCode(result);
    return SUCCEEDED(result) ? reinterpret_cast<RenderVertexBufferHandle>(buffer) : nullptr;
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

void Renderer::Bind_Point_Sprite_Vertex_Buffer(RenderVertexBufferHandle buffer, unsigned stride)
{
    IDirect3DDevice8 * device = DX8Wrapper::_Get_D3D_Device8();
    if (!device)
        return;
    device->SetStreamSource(0, reinterpret_cast<IDirect3DVertexBuffer8 *>(buffer), stride);
    device->SetVertexShader(D3DFVF_XYZ);
}

void Renderer::Draw_Point_List(unsigned firstVertex, unsigned pointCount)
{
    IDirect3DDevice8 * device = DX8Wrapper::_Get_D3D_Device8();
    if (device)
        device->DrawPrimitive(D3DPT_POINTLIST, firstVertex, pointCount);
}

// DX8 floating-point point-sprite states are bit-cast into DWORD slots.
// Keep the original value bits without introducing a platform type upstream.
static DWORD Point_Sprite_Float_Bits(float value)
{
    DWORD bits = 0;
    static_assert(sizeof(bits) == sizeof(value), "Expected 32-bit float render state");
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

void Renderer::Set_Point_Sprite_Rendering(bool enabled, float size, float minSize, float maxSize)
{
    DX8Wrapper::Set_DX8_Render_State(D3DRS_POINTSPRITEENABLE, enabled ? TRUE : FALSE);
    DX8Wrapper::Set_DX8_Render_State(D3DRS_POINTSCALEENABLE, enabled ? TRUE : FALSE);
    if (!enabled)
        return;

    DX8Wrapper::Set_DX8_Render_State(D3DRS_POINTSIZE, Point_Sprite_Float_Bits(size));
    DX8Wrapper::Set_DX8_Render_State(D3DRS_POINTSIZE_MIN, Point_Sprite_Float_Bits(minSize));
    DX8Wrapper::Set_DX8_Render_State(D3DRS_POINTSIZE_MAX, Point_Sprite_Float_Bits(maxSize));
    DX8Wrapper::Set_DX8_Render_State(D3DRS_POINTSCALE_A, Point_Sprite_Float_Bits(0.00f));
    DX8Wrapper::Set_DX8_Render_State(D3DRS_POINTSCALE_B, Point_Sprite_Float_Bits(0.00f));
    DX8Wrapper::Set_DX8_Render_State(D3DRS_POINTSCALE_C, Point_Sprite_Float_Bits(1.00f));
}

void Renderer::Bind_Index_Buffer(RenderIndexBufferHandle buffer, unsigned baseVertexIndex)
{
    DX8_THREAD_ASSERT();
    DX8CALL(SetIndices(reinterpret_cast<IDirect3DIndexBuffer8 *>(buffer), baseVertexIndex));
}

void Renderer::Bind_Dynamic_Vertex_Buffer(const DynamicVBAccessClass & access)
{
    DX8Wrapper::Set_Vertex_Buffer(access);
}

void Renderer::Bind_Dynamic_Index_Buffer(const DynamicIBAccessClass & access,
                                         unsigned short baseVertexIndex)
{
    DX8Wrapper::Set_Index_Buffer(access, baseVertexIndex);
}

void Renderer::Bind_Legacy_Index_Buffer(const IndexBufferClass * buffer,
                                        unsigned short baseVertexIndex)
{
    DX8Wrapper::Set_Index_Buffer(buffer, baseVertexIndex);
}

void Renderer::Bind_Legacy_Vertex_Buffer(const VertexBufferClass * buffer)
{
    DX8Wrapper::Set_Vertex_Buffer(buffer);
}

void Renderer::Set_Index_Buffer_Index_Offset(unsigned offset)
{
    DX8Wrapper::Set_Index_Buffer_Index_Offset(offset);
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

RenderTexture2DHandle Renderer::Create_Texture_From_Surface(
    RenderSurfaceHandle surface, unsigned mipLevels)
{
    DX8_THREAD_ASSERT();
    if (!surface)
        return nullptr;

    IDirect3DTexture8 * texture = DX8Wrapper::_Create_DX8_Texture(
        reinterpret_cast<IDirect3DSurface8 *>(surface),
        static_cast<MipCountType>(mipLevels));
    return reinterpret_cast<RenderTexture2DHandle>(texture);
}

unsigned Renderer::Get_Texture_Mip_Count(RenderTextureHandle texture)
{
    if (!texture)
        return 0;
    return reinterpret_cast<IDirect3DBaseTexture8 *>(texture)->GetLevelCount();
}

// Only the backend interprets D3D8 resource descriptions.
static void Describe_DX8_Texture_Level_2D(RenderTexture2DHandle texture, unsigned level,
                                          bool depthFormat, RenderTextureLevelDescription & description)
{
    description = RenderTextureLevelDescription();
    if (!texture)
        return;
    D3DSURFACE_DESC native = {};
    const HRESULT result = reinterpret_cast<IDirect3DTexture8 *>(texture)->GetLevelDesc(level, &native);
    DX8_ErrorCode(result);
    if (FAILED(result))
        return;
    description.width = native.Width;
    description.height = native.Height;
    description.depth = 1;
    description.pixelFormat = depthFormat
        ? static_cast<unsigned>(D3DFormat_To_WW3DZFormat(native.Format))
        : static_cast<unsigned>(D3DFormat_To_WW3DFormat(native.Format));
}

void Renderer::Describe_Texture_Level_2D(RenderTexture2DHandle texture, unsigned level,
                                         RenderTextureLevelDescription & description)
{
    Describe_DX8_Texture_Level_2D(texture, level, false, description);
}

void Renderer::Describe_Depth_Texture_Level_2D(RenderTexture2DHandle texture, unsigned level,
                                               RenderTextureLevelDescription & description)
{
    Describe_DX8_Texture_Level_2D(texture, level, true, description);
}

void Renderer::Describe_Texture_Level_Cube(RenderTextureCubeHandle texture, unsigned level,
                                           RenderTextureLevelDescription & description)
{
    description = RenderTextureLevelDescription();
    if (!texture)
        return;
    D3DSURFACE_DESC native = {};
    const HRESULT result = reinterpret_cast<IDirect3DCubeTexture8 *>(texture)->GetLevelDesc(level, &native);
    DX8_ErrorCode(result);
    if (FAILED(result))
        return;
    description.width = native.Width;
    description.height = native.Height;
    description.depth = 1;
    description.pixelFormat = static_cast<unsigned>(D3DFormat_To_WW3DFormat(native.Format));
}

void Renderer::Describe_Texture_Level_Volume(RenderTextureVolumeHandle texture, unsigned level,
                                             RenderTextureLevelDescription & description)
{
    description = RenderTextureLevelDescription();
    if (!texture)
        return;
    D3DVOLUME_DESC native = {};
    const HRESULT result = reinterpret_cast<IDirect3DVolumeTexture8 *>(texture)->GetLevelDesc(level, &native);
    DX8_ErrorCode(result);
    if (FAILED(result))
        return;
    description.width = native.Width;
    description.height = native.Height;
    description.depth = native.Depth;
    description.pixelFormat = static_cast<unsigned>(D3DFormat_To_WW3DFormat(native.Format));
}

unsigned Renderer::Get_Texture_2D_Memory_Usage(RenderTexture2DHandle texture)
{
    if (!texture)
        return 0;

    IDirect3DTexture8 * nativeTexture = reinterpret_cast<IDirect3DTexture8 *>(texture);
    unsigned size = 0;
    for (unsigned level = 0; level < nativeTexture->GetLevelCount(); ++level)
    {
        D3DSURFACE_DESC desc = {};
        DX8_ErrorCode(nativeTexture->GetLevelDesc(level, &desc));
        size += desc.Size;
    }
    return size;
}

RenderTexture2DHandle Renderer::Create_Offscreen_Texture(const RenderSurfaceDescription & description)
{
    // Preserve the screen-filter's original exact CreateTexture parameters.
    // D3DXCreateTexture can negotiate formats, so do not use it for this RTT.
    if (!description.Is_Valid() || description.multisampled)
        return nullptr;
    IDirect3DDevice8 * device = DX8Wrapper::_Get_D3D_Device8();
    if (!device)
        return nullptr;
    IDirect3DTexture8 * texture = nullptr;
    const HRESULT result = device->CreateTexture(description.width, description.height,
        1, D3DUSAGE_RENDERTARGET,
        WW3DFormat_To_D3DFormat(static_cast<WW3DFormat>(description.pixelFormat)),
        D3DPOOL_DEFAULT, &texture);
    return result == S_OK ? reinterpret_cast<RenderTexture2DHandle>(texture) : nullptr;
}

RenderSurfaceHandle Renderer::Acquire_Current_Color_Surface()
{
    IDirect3DDevice8 * device = DX8Wrapper::_Get_D3D_Device8();
    IDirect3DSurface8 * surface = nullptr;
    if (!device || device->GetRenderTarget(&surface) != S_OK)
        return nullptr;
    return reinterpret_cast<RenderSurfaceHandle>(surface);
}

RenderSurfaceHandle Renderer::Acquire_Back_Buffer_Surface()
{
    IDirect3DDevice8 * device = DX8Wrapper::_Get_D3D_Device8();
    IDirect3DSurface8 * surface = nullptr;
    if (!device || device->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &surface) != S_OK)
        return nullptr;
    return reinterpret_cast<RenderSurfaceHandle>(surface);
}

// Preserve the smudge probe's exact native readback sequence and Size-based
// byte copy. The D3D8 objects are acquired and released entirely here.
unsigned Renderer::Read_Current_Color_Region(unsigned char * destination, unsigned capacity,
                                             int x, int y, unsigned width, unsigned height)
{
    if (!destination || !capacity || !width || !height)
        return 0;
    IDirect3DDevice8 * device = DX8Wrapper::_Get_D3D_Device8();
    if (!device)
        return 0;

    IDirect3DSurface8 * source = nullptr;
    if (device->GetRenderTarget(&source) != S_OK || !source)
        return 0;

    unsigned copied = 0;
    D3DSURFACE_DESC desc = {};
    if (source->GetDesc(&desc) == S_OK)
    {
        IDirect3DSurface8 * image = nullptr;
        if (device->CreateImageSurface(width, height, desc.Format, &image) == S_OK && image)
        {
            RECT region = {x, y, x + static_cast<int>(width), y + static_cast<int>(height)};
            POINT origin = {0, 0};
            if (device->CopyRects(source, &region, 1, image, &origin) == S_OK)
            {
                D3DLOCKED_RECT locked = {};
                if (image->LockRect(&locked, nullptr, D3DLOCK_READONLY) == S_OK)
                {
                    D3DSURFACE_DESC imageDesc = {};
                    if (image->GetDesc(&imageDesc) == S_OK && locked.pBits)
                    {
                        copied = imageDesc.Size < capacity ? imageDesc.Size : capacity;
                        std::memcpy(destination, locked.pBits, copied);
                    }
                    image->UnlockRect();
                }
            }
            image->Release();
        }
    }
    source->Release();
    return copied;
}

RenderSurfaceHandle Renderer::Acquire_Current_Depth_Surface()
{
    IDirect3DDevice8 * device = DX8Wrapper::_Get_D3D_Device8();
    IDirect3DSurface8 * surface = nullptr;
    if (!device || device->GetDepthStencilSurface(&surface) != S_OK)
        return nullptr;
    return reinterpret_cast<RenderSurfaceHandle>(surface);
}

bool Renderer::Set_Render_Target_Surfaces(RenderSurfaceHandle color, RenderSurfaceHandle depth)
{
    IDirect3DDevice8 * device = DX8Wrapper::_Get_D3D_Device8();
    return device && device->SetRenderTarget(
        reinterpret_cast<IDirect3DSurface8 *>(color),
        reinterpret_cast<IDirect3DSurface8 *>(depth)) == S_OK;
}

RenderSurfaceHandle Renderer::Acquire_Texture_Surface_Level(
    RenderTexture2DHandle texture, unsigned mipLevel)
{
    if (!texture)
        return nullptr;
    IDirect3DSurface8 * surface = nullptr;
    DX8_ErrorCode(reinterpret_cast<IDirect3DTexture8 *>(texture)->GetSurfaceLevel(
        mipLevel, &surface));
    return reinterpret_cast<RenderSurfaceHandle>(surface);
}

void Renderer::Update_Texture(RenderTextureHandle source, RenderTextureHandle destination)
{
    DX8CALL(UpdateTexture(
        reinterpret_cast<IDirect3DBaseTexture8 *>(source),
        reinterpret_cast<IDirect3DBaseTexture8 *>(destination)));
}

void Renderer::Map_Texture_2D(RenderTexture2DHandle texture, unsigned level,
                              RenderSurfaceMap & mapped)
{
    mapped = RenderSurfaceMap();
    D3DLOCKED_RECT lock = {};
    DX8_ErrorCode(reinterpret_cast<IDirect3DTexture8 *>(texture)->LockRect(
        level, &lock, nullptr, 0));
    mapped.data = lock.pBits;
    mapped.rowPitch = lock.Pitch;
}

void Renderer::Unmap_Texture_2D(RenderTexture2DHandle texture, unsigned level)
{
    DX8_ErrorCode(reinterpret_cast<IDirect3DTexture8 *>(texture)->UnlockRect(level));
}

void Renderer::Map_Texture_Cube(RenderTextureCubeHandle texture, unsigned face,
                                unsigned level, RenderSurfaceMap & mapped)
{
    mapped = RenderSurfaceMap();
    D3DLOCKED_RECT lock = {};
    DX8_ErrorCode(reinterpret_cast<IDirect3DCubeTexture8 *>(texture)->LockRect(
        static_cast<D3DCUBEMAP_FACES>(face), level, &lock, nullptr, 0));
    mapped.data = lock.pBits;
    mapped.rowPitch = lock.Pitch;
}

void Renderer::Unmap_Texture_Cube(RenderTextureCubeHandle texture, unsigned face,
                                  unsigned level)
{
    DX8_ErrorCode(reinterpret_cast<IDirect3DCubeTexture8 *>(texture)->UnlockRect(
        static_cast<D3DCUBEMAP_FACES>(face), level));
}

void Renderer::Map_Texture_Volume(RenderTextureVolumeHandle texture, unsigned level,
                                  RenderVolumeMap & mapped)
{
    mapped = RenderVolumeMap();
    D3DLOCKED_BOX lock = {};
    DX8_ErrorCode(reinterpret_cast<IDirect3DVolumeTexture8 *>(texture)->LockBox(
        level, &lock, nullptr, 0));
    mapped.data = lock.pBits;
    mapped.rowPitch = lock.RowPitch;
    mapped.slicePitch = lock.SlicePitch;
}

void Renderer::Unmap_Texture_Volume(RenderTextureVolumeHandle texture, unsigned level)
{
    DX8_ErrorCode(reinterpret_cast<IDirect3DVolumeTexture8 *>(texture)->UnlockBox(level));
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


unsigned Renderer::Get_Texture_Priority(RenderTextureHandle texture)
{
    return texture ? reinterpret_cast<IDirect3DBaseTexture8 *>(texture)->GetPriority() : 0;
}

unsigned Renderer::Set_Texture_Priority(RenderTextureHandle texture, unsigned priority)
{
    return texture ? reinterpret_cast<IDirect3DBaseTexture8 *>(texture)->SetPriority(priority) : 0;
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
    description.multisampled = nativeDescription.MultiSampleType != D3DMULTISAMPLE_NONE;
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

void Renderer::Lock_Surface(RenderSurfaceHandle surface, RenderSurfaceMap & map,
                            const RenderSurfaceRect * region, bool readOnly)
{
    map.data = nullptr;
    map.rowPitch = 0;
    RECT nativeRegion = {};
    RECT * nativeRect = nullptr;
    if (region)
    {
        nativeRegion.left = region->left;
        nativeRegion.top = region->top;
        nativeRegion.right = region->right;
        nativeRegion.bottom = region->bottom;
        nativeRect = &nativeRegion;
    }
    D3DLOCKED_RECT locked = {};
    DX8_ErrorCode(reinterpret_cast<IDirect3DSurface8 *>(surface)->LockRect(
        &locked, nativeRect, readOnly ? D3DLOCK_READONLY : 0));
    map.data = locked.pBits;
    map.rowPitch = locked.Pitch;
}

void Renderer::Unlock_Surface(RenderSurfaceHandle surface)
{
    DX8_ErrorCode(reinterpret_cast<IDirect3DSurface8 *>(surface)->UnlockRect());
}

// Preserve DX8 CopyRects semantics, including the full-surface/null-region
// path used by font atlas upload.
void Renderer::Copy_Surface(RenderSurfaceHandle source, const RenderSurfaceRect * sourceRect,
                            RenderSurfaceHandle destination, const RenderSurfacePoint * destinationPoint)
{
    RECT nativeSource = {};
    POINT nativeDestination = {};
    const RECT * src = nullptr;
    const POINT * dst = nullptr;
    if (sourceRect)
    {
        nativeSource.left = sourceRect->left;
        nativeSource.top = sourceRect->top;
        nativeSource.right = sourceRect->right;
        nativeSource.bottom = sourceRect->bottom;
        src = &nativeSource;
    }
    if (destinationPoint)
    {
        nativeDestination.x = destinationPoint->x;
        nativeDestination.y = destinationPoint->y;
        dst = &nativeDestination;
    }
    DX8Wrapper::_Copy_DX8_Rects(
        reinterpret_cast<IDirect3DSurface8 *>(source), src, src ? 1 : 0,
        reinterpret_cast<IDirect3DSurface8 *>(destination), dst);
}

void Renderer::Blit_Surface(RenderSurfaceHandle source, const RenderSurfaceRect & sourceRect,
                            RenderSurfaceHandle destination, const RenderSurfaceRect & destinationRect,
                            RenderSurfaceBlitFilter filter)
{
    RECT src = {sourceRect.left, sourceRect.top, sourceRect.right, sourceRect.bottom};
    RECT dst = {destinationRect.left, destinationRect.top,
                destinationRect.right, destinationRect.bottom};
    const DWORD nativeFilter = filter == RENDER_SURFACE_BLIT_TRIANGLE
        ? D3DX_FILTER_TRIANGLE : D3DX_FILTER_NONE;
    DX8_ErrorCode(D3DXLoadSurfaceFromSurface(
        reinterpret_cast<IDirect3DSurface8 *>(destination), nullptr, &dst,
        reinterpret_cast<IDirect3DSurface8 *>(source), nullptr, &src, nativeFilter, 0));
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


void Renderer::Set_Fixed_Stage_Operation(unsigned stage, bool alpha,
                                         RenderFixedStageOperation operation)
{
    D3DTEXTUREOP nativeOperation = D3DTOP_DISABLE;
    switch (operation)
    {
    case RENDER_FIXED_STAGE_SELECT_ARG1: nativeOperation = D3DTOP_SELECTARG1; break;
    case RENDER_FIXED_STAGE_SELECT_ARG2: nativeOperation = D3DTOP_SELECTARG2; break;
    case RENDER_FIXED_STAGE_MODULATE: nativeOperation = D3DTOP_MODULATE; break;
    case RENDER_FIXED_STAGE_ADD: nativeOperation = D3DTOP_ADD; break;
    case RENDER_FIXED_STAGE_BLEND_CURRENT_ALPHA: nativeOperation = D3DTOP_BLENDCURRENTALPHA; break;
    case RENDER_FIXED_STAGE_DOT_PRODUCT3: nativeOperation = D3DTOP_DOTPRODUCT3; break;
    case RENDER_FIXED_STAGE_MULTIPLY_ADD: nativeOperation = D3DTOP_MULTIPLYADD; break;
    case RENDER_FIXED_STAGE_DISABLE: break;
    default: WWASSERT(0); return;
    }
    DX8Wrapper::Set_DX8_Texture_Stage_State(
        stage, alpha ? D3DTSS_ALPHAOP : D3DTSS_COLOROP, nativeOperation);
}

void Renderer::Set_Fixed_Stage_Argument(unsigned stage, bool alpha, unsigned slot,
                                        RenderTextureArgument source, bool complement,
                                        bool replicateAlpha)
{
    // D3D8 offers COLORARG0 for three-input operations but no ALPHAARG0.
    if (slot > 2 || (alpha && slot == 0))
    {
        WWASSERT(0);
        return;
    }
    const D3DTEXTURESTAGESTATETYPE nativeField = alpha
        ? (slot == 1 ? D3DTSS_ALPHAARG1 : D3DTSS_ALPHAARG2)
        : (slot == 0 ? D3DTSS_COLORARG0
           : slot == 1 ? D3DTSS_COLORARG1 : D3DTSS_COLORARG2);
    unsigned nativeArgument = To_DX8_Texture_Argument(source);
    if (complement)
        nativeArgument |= D3DTA_COMPLEMENT;
    if (replicateAlpha)
        nativeArgument |= D3DTA_ALPHAREPLICATE;
    DX8Wrapper::Set_DX8_Texture_Stage_State(stage, nativeField, nativeArgument);
}

void Renderer::Set_Texture_Coordinate_Source(unsigned stage, unsigned uvIndex,
                                             bool cameraSpacePosition)
{
    const unsigned nativeIndex = uvIndex |
        (cameraSpacePosition ? D3DTSS_TCI_CAMERASPACEPOSITION : D3DTSS_TCI_PASSTHRU);
    DX8Wrapper::Set_DX8_Texture_Stage_State(
        stage, D3DTSS_TEXCOORDINDEX, nativeIndex);
}

void Renderer::Set_Texture_Transform_2D(unsigned stage, bool enabled)
{
    DX8Wrapper::Set_DX8_Texture_Stage_State(
        stage, D3DTSS_TEXTURETRANSFORMFLAGS, enabled ? D3DTTFF_COUNT2 : D3DTTFF_DISABLE);
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
    D3DTEXTUREADDRESS nativeAddress = D3DTADDRESS_CLAMP;
    switch (address)
    {
    case RENDER_SAMPLER_REPEAT: nativeAddress = D3DTADDRESS_WRAP; break;
    case RENDER_SAMPLER_BORDER: nativeAddress = D3DTADDRESS_BORDER; break;
    case RENDER_SAMPLER_CLAMP:
    default: break;
    }
    DX8Wrapper::Set_DX8_Texture_Stage_State(stage, nativeField, nativeAddress);
}

void Renderer::Set_Texture_Border_Color(unsigned stage, unsigned packedARGB)
{
    DX8Wrapper::Set_DX8_Texture_Stage_State(stage, D3DTSS_BORDERCOLOR, packedARGB);
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

void Renderer::Set_Color_Write_Mask(bool rgbEnabled, bool alphaEnabled)
{
    unsigned mask = 0;
    if (rgbEnabled)
        mask |= D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE;
    if (alphaEnabled)
        mask |= D3DCOLORWRITEENABLE_ALPHA;
    DX8Wrapper::Set_DX8_Render_State(D3DRS_COLORWRITEENABLE, mask);
}

bool Renderer::Supports_Texture_Format(unsigned w3dPixelFormat)
{
    const DX8Caps * caps = DX8Wrapper::Is_Initted() ? DX8Wrapper::Get_Current_Caps() : nullptr;
    return caps && caps->Support_Texture_Format(static_cast<WW3DFormat>(w3dPixelFormat));
}

unsigned Renderer::Get_Back_Buffer_Format()
{
    return static_cast<unsigned>(DX8Wrapper::getBackBufferFormat());
}
