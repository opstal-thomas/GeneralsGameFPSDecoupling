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

// TheSuperHackers @refactor bobtista 08/10/2026 Static rendering interface so WW3D2
// rendering can be re-targeted to other backends. The build links exactly one
// backend, and that backend defines the methods below. The DX8 backend is the
// reference implementation.

#pragma once

// Forward declarations keep this header includable without pulling in the full
// WW3D2 header graph. All W3D types below are passed by pointer or reference.

class LightEnvironmentClass;
class CameraClass;
class DynamicVBAccessClass;
class DynamicIBAccessClass;
struct RenderStateStruct;
class IndexBufferClass;
class Vector3;
class Vector4;
class Matrix3D;
class Matrix4x4;
class ShaderClass;
class VertexMaterialClass;
class TextureBaseClass;

// Strongly typed, incomplete GPU resource handles. The backend owns the
// actual object and its lifetime. A vertex handle cannot be passed to an
// index-buffer operation by mistake. The DX8 adapter uses COM resources,
// while a future renderer may use a completely different representation.
struct RendererVertexBufferResource;
struct RendererIndexBufferResource;
using RenderVertexBufferHandle = RendererVertexBufferResource *;
using RenderIndexBufferHandle = RendererIndexBufferResource *;

// A distinct opaque identity for a backend-owned two-dimensional texture.
struct RendererTexture2DResource;
using RenderTexture2DHandle = RendererTexture2DResource *;

// All existing W3D texture kinds (2D, cube, volume and depth) may be bound
// through the same stage. Distinct from the 2D creation handle so that
// callers cannot accidentally use an arbitrary texture as a 2D render target.
struct RendererTextureResource;
using RenderTextureHandle = RendererTextureResource *;

// Distinct creation handles for specialized textures. All texture types
// still bind to a stage through the generic RenderTextureHandle.
struct RendererTextureCubeResource;
struct RendererTextureVolumeResource;
using RenderTextureCubeHandle = RendererTextureCubeResource *;
using RenderTextureVolumeHandle = RendererTextureVolumeResource *;

struct RendererSurfaceResource;
using RenderSurfaceHandle = RendererSurfaceResource *;

// Pixel-space surface copy regions, independent of Windows RECT/POINT.
// Right and bottom are exclusive, matching the existing W3D surface API.
struct RenderSurfaceRect
{
    int left, top, right, bottom;
};

struct RenderSurfacePoint
{
    int x, y;
};

// Mapped CPU-visible image data, including its potentially padded row pitch.
struct RenderSurfaceMap
{
    RenderSurfaceMap() : data(nullptr), rowPitch(0) {}
    void * data;
    int rowPitch;
};

// Three-dimensional texture mapping includes the pitch between depth slices.
struct RenderVolumeMap
{
    RenderVolumeMap() : data(nullptr), rowPitch(0), slicePitch(0) {}
    void * data;
    int rowPitch;
    int slicePitch;
};

enum RenderSurfaceBlitFilter
{
    RENDER_SURFACE_BLIT_NONE,
    RENDER_SURFACE_BLIT_TRIANGLE
};

// Semantic W3D transforms; no Direct3D transform-state ordinals are exposed.
enum RenderTransform
{
    RENDER_TRANSFORM_WORLD,
    RENDER_TRANSFORM_VIEW,
    RENDER_TRANSFORM_PROJECTION
};

struct RenderViewport
{
    unsigned int x;
    unsigned int y;
    unsigned int width;
    unsigned int height;
    float min_z;
    float max_z;
};

// Backend capabilities must not expose D3D8 capability structures to callers.
// Keeping these values in WW3D terms allows other graphics APIs to report
// equivalent capabilities without depending on DX8Caps or D3DCAPS8.
struct RenderCapabilities
{
    RenderCapabilities()
        : hardwareTransformAndLighting(false), nPatches(false),
          textureCompression(false), anisotropicFiltering(false),
          cubeTextures(false), bumpEnvironmentMapping(false),
          pointSprites(false), dot3TextureOperation(false),
          pixelShaderMajorVersion(0), pixelShaderMinorVersion(0), maxSimultaneousTextures(0),
          maxTexturesPerPass(0), maxTextureWidth(0), maxTextureHeight(0),
          maxVolumeExtent(0), maxTextureAspectRatio(0),
          linearMinFilter(false), linearMagFilter(false), linearMipFilter(false),
          anisotropicMinFilter(false), anisotropicMagFilter(false)
    {
    }

    bool hardwareTransformAndLighting;
    bool nPatches;
    bool textureCompression;
    bool anisotropicFiltering;
    bool cubeTextures;
    bool bumpEnvironmentMapping;
    bool pointSprites;
    bool dot3TextureOperation;
    unsigned pixelShaderMajorVersion;
    unsigned pixelShaderMinorVersion;
    unsigned maxSimultaneousTextures;
    unsigned maxTexturesPerPass;
    unsigned maxTextureWidth;
    unsigned maxTextureHeight;
    unsigned maxVolumeExtent;
    unsigned maxTextureAspectRatio;
    bool linearMinFilter;
    bool linearMagFilter;
    bool linearMipFilter;
    bool anisotropicMinFilter;
    bool anisotropicMagFilter;
};

// Backend-neutral hardware identity for vendor shader selection.
struct RenderAdapterIdentification
{
    RenderAdapterIdentification() : vendorId(0), deviceId(0), driverVersion(0) {}
    unsigned vendorId;
    unsigned deviceId;
    unsigned long long driverVersion;
};

// Transitional descriptions for legacy W3D geometry allocations. They
// contain neither Direct3D interfaces nor native D3D usage/pool enums.
// A modern backend will translate the original W3D vertex layout to a
// programmable input layout. The DX8 adapter interprets legacyVertexFormat
// as the original FVF bitmask until buffer binding is migrated.
enum RenderBufferUsageFlags
{
    RENDER_BUFFER_STATIC = 0,
    RENDER_BUFFER_DYNAMIC = 1 << 0,
    RENDER_BUFFER_SOFTWARE_VERTEX_PROCESSING = 1 << 1,
    RENDER_BUFFER_NPATCHES = 1 << 2
};

enum RenderBufferMapFlags
{
    RENDER_MAP_NONE = 0,
    RENDER_MAP_DISCARD = 1 << 0,
    RENDER_MAP_NO_OVERWRITE = 1 << 1,
    RENDER_MAP_NO_SYSTEM_LOCK = 1 << 2,
    RENDER_MAP_READ_ONLY = 1 << 3
};

struct RenderBufferDescription
{
    RenderBufferDescription()
        : byteSize(0), legacyVertexFormat(0), usageFlags(RENDER_BUFFER_STATIC)
    {
    }

    unsigned byteSize;
    unsigned legacyVertexFormat; // 0 for 16-bit index buffers.
    unsigned usageFlags;

    bool Is_Valid() const { return byteSize > 0; }
    bool Is_Dynamic() const { return (usageFlags & RENDER_BUFFER_DYNAMIC) != 0; }
};

// The transitional texture format and mip fields carry WW3D's own numeric
// values, never native D3D8 or DXGI identifiers. Render targets are still
// ordinary 2D textures with the renderTarget flag set.
enum RenderTextureMemory
{
    RENDER_TEXTURE_DEFAULT,
    RENDER_TEXTURE_MANAGED,
    RENDER_TEXTURE_SYSTEM
};

struct RenderTexture2DDescription
{
    RenderTexture2DDescription()
        : width(0), height(0), pixelFormat(0), mipLevels(0),
          memory(RENDER_TEXTURE_MANAGED), renderTarget(false)
    {
    }

    unsigned width;
    unsigned height;
    unsigned pixelFormat;
    unsigned mipLevels; // Zero means all mipmap levels in W3D.
    RenderTextureMemory memory;
    bool renderTarget;

    bool Is_Valid() const { return width > 0 && height > 0; }
};

struct RenderTextureCubeDescription
{
    RenderTextureCubeDescription()
        : edgeLength(0), pixelFormat(0), mipLevels(0),
          memory(RENDER_TEXTURE_MANAGED), renderTarget(false)
    {
    }

    unsigned edgeLength;
    unsigned pixelFormat;
    unsigned mipLevels;
    RenderTextureMemory memory;
    bool renderTarget;

    bool Is_Valid() const { return edgeLength > 0; }
};

struct RenderTextureVolumeDescription
{
    RenderTextureVolumeDescription()
        : width(0), height(0), depth(0), pixelFormat(0), mipLevels(0),
          memory(RENDER_TEXTURE_MANAGED)
    {
    }

    unsigned width;
    unsigned height;
    unsigned depth;
    unsigned pixelFormat;
    unsigned mipLevels;
    RenderTextureMemory memory;

    bool Is_Valid() const { return width > 0 && height > 0 && depth > 0; }
};

// Uncompressed WW3D image surfaces (including file-backed images) remain
// distinct from GPU textures and render targets. This description has no
// platform-native graphics types and can also report an existing surface.
struct RenderSurfaceDescription
{
    RenderSurfaceDescription() : width(0), height(0), pixelFormat(0) {}

    unsigned width;
    unsigned height;
    unsigned pixelFormat;

    bool Is_Valid() const { return width > 0 && height > 0; }
};

// Sampler state is a renderer concept, not a Direct3D texture-stage enum.
// Filter mode initialization remains in W3D for now; only translated,
// resolved sampler settings cross the backend boundary.
enum RenderSamplerFilter
{
    RENDER_SAMPLER_NONE,
    RENDER_SAMPLER_POINT,
    RENDER_SAMPLER_LINEAR,
    RENDER_SAMPLER_ANISOTROPIC
};

enum RenderSamplerAddress
{
    RENDER_SAMPLER_REPEAT,
    RENDER_SAMPLER_CLAMP
};

// Per-field sampler updates avoid changing unrelated cached texture states.
enum RenderSamplerFilterField
{
    RENDER_SAMPLER_MIN_FILTER,
    RENDER_SAMPLER_MAG_FILTER,
    RENDER_SAMPLER_MIP_FILTER
};

enum RenderSamplerAxis
{
    RENDER_SAMPLER_AXIS_U,
    RENDER_SAMPLER_AXIS_V,
    RENDER_SAMPLER_AXIS_W
};

struct RenderSamplerDescription
{
    RenderSamplerDescription()
        : minFilter(RENDER_SAMPLER_POINT),
          magFilter(RENDER_SAMPLER_POINT),
          mipFilter(RENDER_SAMPLER_NONE),
          addressU(RENDER_SAMPLER_REPEAT),
          addressV(RENDER_SAMPLER_REPEAT)
    {
    }

    RenderSamplerFilter minFilter;
    RenderSamplerFilter magFilter;
    RenderSamplerFilter mipFilter;
    RenderSamplerAddress addressU;
    RenderSamplerAddress addressV;
};

// The original W3D shaders encode depth, cull and alpha blending into
// bitfields. These states describe their graphics meaning, not the legacy
// DX8 values. A modern backend can translate them into immutable PSO keys.
enum RenderBlendFactor
{
    RENDER_BLEND_ZERO,
    RENDER_BLEND_ONE,
    RENDER_BLEND_SRC_COLOR,
    RENDER_BLEND_INV_SRC_COLOR,
    RENDER_BLEND_SRC_ALPHA,
    RENDER_BLEND_INV_SRC_ALPHA,
    RENDER_BLEND_DEST_COLOR,
    RENDER_BLEND_DEST_ALPHA,
    RENDER_BLEND_INV_DEST_ALPHA
};

enum RenderCompareFunc
{
    RENDER_COMPARE_NEVER,
    RENDER_COMPARE_LESS,
    RENDER_COMPARE_EQUAL,
    RENDER_COMPARE_LESS_EQUAL,
    RENDER_COMPARE_GREATER,
    RENDER_COMPARE_NOT_EQUAL,
    RENDER_COMPARE_GREATER_EQUAL,
    RENDER_COMPARE_ALWAYS
};

enum RenderCullMode
{
    RENDER_CULL_NONE,
    RENDER_CULL_CW,
    RENDER_CULL_CCW
};

struct RenderBlendState
{
    RenderBlendState()
        : source(RENDER_BLEND_ONE), destination(RENDER_BLEND_ZERO),
          blendEnabled(false), alphaTestEnabled(false),
          alphaReference(0x60), alphaCompare(RENDER_COMPARE_GREATER_EQUAL),
          colorWriteEnabled(true)
    {
    }

    RenderBlendFactor source;
    RenderBlendFactor destination;
    bool blendEnabled;
    bool alphaTestEnabled;
    unsigned alphaReference;
    RenderCompareFunc alphaCompare;
    // The DX8 compatibility path emulates disabled color writes using
    // ZERO/ONE blending. DX12 can use a native render-target write mask.
    bool colorWriteEnabled;

    bool Is_Opaque() const { return !blendEnabled && !alphaTestEnabled; }
};

// Vertex materials are still loaded from W3D's original format. Color
// sources describe lighting input selection, not native Direct3D state.
enum RenderMaterialColorSource
{
    RENDER_MATERIAL_COLOR,
    RENDER_VERTEX_COLOR_1,
    RENDER_VERTEX_COLOR_2
};

struct RenderMaterialColorSources
{
    RenderMaterialColorSources()
        : ambient(RENDER_MATERIAL_COLOR),
          diffuse(RENDER_MATERIAL_COLOR),
          emissive(RENDER_MATERIAL_COLOR)
    {
    }

    RenderMaterialColorSource ambient;
    RenderMaterialColorSource diffuse;
    RenderMaterialColorSource emissive;
};

// Vertex material colors are a W3D concept, independent of the native
// fixed-function material memory layout.
struct RenderMaterialColor
{
    RenderMaterialColor(float red = 0.0f, float green = 0.0f, float blue = 0.0f,
                        float alpha = 0.0f)
        : r(red), g(green), b(blue), a(alpha) {}
    float r, g, b, a;
};

struct RenderVertexMaterialDescription
{
    RenderVertexMaterialDescription()
        : diffuse(1.0f, 1.0f, 1.0f, 1.0f),
          ambient(1.0f, 1.0f, 1.0f, 1.0f),
          power(1.0f) {}

    RenderMaterialColor diffuse;
    RenderMaterialColor ambient;
    RenderMaterialColor specular;
    RenderMaterialColor emissive;
    float power;
};

// The W3D fixed-function pixel pipeline consists of separate color and
// alpha combiners per texture stage. Operations and inputs are semantic,
// rather than the numeric values used by Direct3D 8 texture-stage states.
// A DX12 backend can implement these with generated pixel shaders.
enum RenderTextureOperation
{
    RENDER_TEXOP_DISABLE,
    RENDER_TEXOP_SELECT_ARG1,
    RENDER_TEXOP_SELECT_ARG2,
    RENDER_TEXOP_MODULATE,
    RENDER_TEXOP_MODULATE2X,
    RENDER_TEXOP_ADD,
    RENDER_TEXOP_ADD_SMOOTH,
    RENDER_TEXOP_SUBTRACT,
    RENDER_TEXOP_BLEND_TEXTURE_ALPHA,
    RENDER_TEXOP_BLEND_CURRENT_ALPHA,
    RENDER_TEXOP_ADD_SIGNED,
    RENDER_TEXOP_ADD_SIGNED2X,
    RENDER_TEXOP_MODULATE_ALPHA_ADD_COLOR,
    RENDER_TEXOP_BUMP_ENV_MAP,
    RENDER_TEXOP_BUMP_ENV_MAP_LUMINANCE
};

// Legacy shader-manager stage operations. This is separate from the 4-bit
// W3D material combiner key, which cannot encode all legacy screen effects.
enum RenderFixedStageOperation
{
    RENDER_FIXED_STAGE_DISABLE,
    RENDER_FIXED_STAGE_SELECT_ARG1,
    RENDER_FIXED_STAGE_SELECT_ARG2,
    RENDER_FIXED_STAGE_MODULATE,
    RENDER_FIXED_STAGE_ADD,
    RENDER_FIXED_STAGE_BLEND_CURRENT_ALPHA,
    RENDER_FIXED_STAGE_DOT_PRODUCT3,
    RENDER_FIXED_STAGE_MULTIPLY_ADD
};

enum RenderTextureArgument
{
    RENDER_TEXARG_CURRENT,
    RENDER_TEXARG_TEXTURE,
    RENDER_TEXARG_DIFFUSE,
    RENDER_TEXARG_TEXTURE_FACTOR
};

struct RenderTextureStageChannel
{
    RenderTextureStageChannel(
        RenderTextureOperation op = RENDER_TEXOP_DISABLE,
        RenderTextureArgument first = RENDER_TEXARG_TEXTURE,
        RenderTextureArgument second = RENDER_TEXARG_CURRENT)
        : operation(op), argument1(first), argument2(second) {}

    RenderTextureOperation operation;
    RenderTextureArgument argument1;
    RenderTextureArgument argument2;
};

// Each channel uses four bits for the operation and two bits for each
// input source. Two fixed-function W3D stages therefore fit into one
// stable 32-bit shader permutation key without hashing struct padding.
// A future DX12 pipeline cache can compile/select a shader for this key.
struct RenderTextureCombinerKey
{
    RenderTextureStageChannel color[2];
    RenderTextureStageChannel alpha[2];

    static unsigned Encode_Channel(const RenderTextureStageChannel & c)
    {
        return (static_cast<unsigned>(c.operation) & 0x0fu)
            | ((static_cast<unsigned>(c.argument1) & 0x03u) << 4)
            | ((static_cast<unsigned>(c.argument2) & 0x03u) << 6);
    }

    unsigned Encode() const
    {
        return Encode_Channel(color[0])
            | (Encode_Channel(alpha[0]) << 8)
            | (Encode_Channel(color[1]) << 16)
            | (Encode_Channel(alpha[1]) << 24);
    }

    static bool Is_Bump_Operation(RenderTextureOperation operation)
    {
        return operation == RENDER_TEXOP_BUMP_ENV_MAP
            || operation == RENDER_TEXOP_BUMP_ENV_MAP_LUMINANCE;
    }

    bool Requires_Bump_Shader() const
    {
        return Is_Bump_Operation(color[0].operation)
            || Is_Bump_Operation(alpha[0].operation)
            || Is_Bump_Operation(color[1].operation)
            || Is_Bump_Operation(alpha[1].operation);
    }
};

// A method appears here once a caller routes through it, not in anticipation of
// one. The set below is what current callers route through; the rest of the
// DX8Wrapper API stays reachable through DX8Wrapper's static methods until a
// caller migrates, at which point the method it needs moves here.
//
// Method names intentionally match the existing DX8Wrapper names so migrating a
// caller is a mechanical DX8Wrapper::X(...) -> Renderer::X(...) rewrite.

class Renderer
{
public:
    // Neutral device health, with no Direct3D HRESULTs exposed to callers.
    enum DeviceStatus
    {
        DEVICE_UNAVAILABLE,      // No device is initialized (e.g. lite/headless mode).
        DEVICE_READY,
        DEVICE_LOST,             // The window/device is temporarily unavailable.
        DEVICE_RESET_REQUIRED,
        DEVICE_ERROR
    };

    enum PolygonMode
    {
        POLYGON_POINT,
        POLYGON_LINE,
        POLYGON_FILL
    };

    static bool Can_Render(DeviceStatus status) { return status == DEVICE_READY; }
    static bool Requires_Reset(DeviceStatus status) { return status == DEVICE_RESET_REQUIRED; }

    // Initialized in WW3D::Init and shut down in WW3D::Shutdown.
    static bool Init(void * window, bool lite);
    static void Shutdown();
    // Exact legacy DX8 initialization/thread identity checks, without exposing
    // DX8Wrapper to resource and asynchronous-loading callers.
    static bool Is_Initialized();
    static unsigned Get_Render_Thread_ID();

    // Device selection, resolution and present policy stay behind the
    // backend boundary. The DX8 implementation preserves legacy semantics.
    static bool Set_Render_Device(const char * name, int width, int height, int bits,
                                  int windowed, bool resize_window);
    static bool Set_Render_Device(int device, int width, int height, int bits, int windowed,
                                  bool resize_window, bool reset_device, bool restore_assets);
    static bool Set_Any_Render_Device();
    static bool Set_Next_Render_Device();
    static int Get_Render_Device();
    static int Get_Render_Device_Count();
    static const char * Get_Render_Device_Name(int device_index);
    static bool Is_Windowed();
    static bool Toggle_Windowed();
    static bool Set_Device_Resolution(int width, int height, int bits, int windowed, bool resize_window);
    static void Get_Device_Resolution(int & width, int & height, int & bits, bool & windowed);
    static void Get_Render_Target_Resolution(int & width, int & height, int & bits, bool & windowed);
    static void Set_Swap_Interval(long swap);
    static int Get_Swap_Interval();
    static DeviceStatus Get_Device_Status();
    static bool Reset_Device();
    static RenderCapabilities Get_Capabilities();
    static bool Get_Adapter_Identification(RenderAdapterIdentification & identification);

    // The W3D image format identifiers are asset-format IDs, not Direct3D
    // constants. Query the device/backend without exposing DX8Caps.
    static bool Supports_Texture_Format(unsigned w3dPixelFormat);
    static unsigned Get_Back_Buffer_Format();

    static void Set_Gamma(float gamma, float bright, float contrast, bool calibrate = true, bool uselimit = true);

    static void Begin_Scene();
    static void End_Scene(bool flip_frame = true);
    static void Flip_To_Primary();
    static void Clear(bool clear_color, bool clear_z_stencil,
                      const Vector3 & color,
                      float dest_alpha = 0.0f, float z = 1.0f, unsigned int stencil = 0);
    static void Set_Viewport(const RenderViewport & viewport);
    static void Set_Polygon_Mode(PolygonMode mode);
    static void Invalidate_Cached_Render_States();

    // Packed W3D ARGB diffuse colors, with legacy DX8 rounding preserved
    // by the active backend. These operations are CPU-side only.
    static unsigned Pack_Color_ARGB(const Vector3 & color, float alpha);
    static unsigned Pack_Color_ARGB(const Vector4 & color);
    static unsigned Pack_Color_ARGB_Clamp(const Vector4 & color);

    // Submit already-bound indexed triangle geometry. Buffer ownership and
    // dynamic sorting remain separate from this draw command for now.
    static void Draw_Triangles(unsigned short startIndex, unsigned short triangleCount,
                               unsigned short minVertexIndex, unsigned short vertexCount);

    // The W3D sorting pool still stores legacy render-state snapshots.
    // Keep capture, temporary restoration, and engine-owned buffer refs
    // backend-private until the state snapshot becomes fully semantic.
    static void Capture_Sorting_State(RenderStateStruct & state);
    static void Restore_Sorting_State(const RenderStateStruct & state);
    static void Release_Sorting_State();
    static void Apply_Sorting_Transforms_And_Lights(RenderStateStruct & state);
    static bool Is_Triangle_Draw_Enabled();
    static void Set_Triangle_Draw_Enabled(bool enabled);
    static void Unbind_Sorting_Geometry();

    // Pretransformed W3D postprocess quads. The vertex memory contains four
    // tightly-packed [x,y,z,rhw,ARGB,u0,v0,(u1,v1)] vertices. This is a
    // transitional CPU vertex path, not a DX8 FVF or D3D primitive ordinal.
    static void Draw_Transformed_Quad(const void * vertices, unsigned vertexStride,
                                      unsigned uvSets);
    // Screen filters deliberately bypass the WW3D deferred texture cache.
    // Keep this immediate binding distinct from Bind_Texture's cached path.
    static void Bind_Immediate_Texture(unsigned stage, RenderTextureHandle texture);
    // Immediate fixed-function alpha selection for motion-blur passes.
    // This bypasses the deferred DX8 state cache like the original calls.
    static void Set_Immediate_Stage_Alpha_Current(unsigned stage);

    // W3D-owned material, shader and texture selections. The original DX8
    // backend preserves its deferred state cache and reference ownership.
    // New backends interpret these as rendering intent, not DX8 commands.
    static void Set_Shader(const ShaderClass & shader);
    static void Set_Material(const VertexMaterialClass * material);
    static void Set_Texture(unsigned stage, TextureBaseClass * texture);
    static void Apply_Pending_State_Changes();

    // A neutral transform boundary. Texture projection and special DX8
    // transform IDs remain in the legacy code until translated separately.
    static void Set_Transform(RenderTransform transform, const Matrix3D & matrix);
    static void Set_Transform(RenderTransform transform, const Matrix4x4 & matrix);
    static void Get_Transform(RenderTransform transform, Matrix4x4 & matrix);
    // Projected material UV transforms are indexed texture transforms,
    // not DX8 transform-state ordinals.
    static void Set_Texture_Transform(unsigned stage, const Matrix4x4 & matrix);
    static void Set_World_Identity();

    // The high-level W3D scene submits geometry using these methods, rather
    // than reaching into the DX8 mesh renderer. Preserve flush ordering:
    // mesh batches, legacy shaders, static sorted lists, transparent sorting,
    // then pending mesh deletion. This is a draw submission boundary.
    static void Set_Mesh_Camera(CameraClass * camera);
    static void Flush_Mesh_Batches();
    static void Clear_Pending_Mesh_Deletes();
    static void Invalidate_Mesh_Cache();

    // Kept separate from Shutdown because WW3D historically releases the
    // texture manager before shutting down the graphics device.
    static void Release_Texture_Resources();

    // Resource creation/ownership are backend-private. DX8 stores the COM
    // address in the opaque handle during the transition; higher layers do
    // not depend on COM identity or the backend's allocation strategy.
    static RenderVertexBufferHandle Create_Vertex_Buffer(const RenderBufferDescription & description);
    static RenderIndexBufferHandle Create_Index_Buffer(const RenderBufferDescription & description);
    static void Release_Vertex_Buffer(RenderVertexBufferHandle buffer);
    static void Release_Index_Buffer(RenderIndexBufferHandle buffer);
    static void Discard_Managed_Resources();

    // Mapping uses byte offsets and sizes. Zero size means the remaining
    // buffer, matching legacy DX8 full-buffer lock behavior.
    static void * Map_Vertex_Buffer(RenderVertexBufferHandle buffer, unsigned offsetBytes,
                                    unsigned sizeBytes, unsigned mapFlags = RENDER_MAP_NONE);
    static void * Map_Index_Buffer(RenderIndexBufferHandle buffer, unsigned offsetBytes,
                                   unsigned sizeBytes, unsigned mapFlags = RENDER_MAP_NONE);
    static void Unmap_Vertex_Buffer(RenderVertexBufferHandle buffer);
    static void Unmap_Index_Buffer(RenderIndexBufferHandle buffer);

    // Actual graphics device binding is distinct from WW3D's delayed
    // state-cache selection. These calls submit a previously selected GPU
    // buffer to the backend without exposing native graphics API types.
    static void Bind_Vertex_Buffer(RenderVertexBufferHandle buffer, unsigned stream, unsigned stride);
    static void Bind_Index_Buffer(RenderIndexBufferHandle buffer, unsigned baseVertexIndex);

    // Transitional dynamic-buffer accessors used by W3D primitives and
    // sorted transparent geometry. They preserve the existing ring-buffer
    // offset/sorting behavior; their allocations remain legacy-owned until
    // the dynamic access classes are made fully backend-independent.
    static void Bind_Dynamic_Vertex_Buffer(const DynamicVBAccessClass & access);
    static void Bind_Dynamic_Index_Buffer(const DynamicIBAccessClass & access,
                                           unsigned short baseVertexIndex);
    // Persistent W3D index buffers are distinct from ring-buffer accessors.
    static void Bind_Legacy_Index_Buffer(const IndexBufferClass * buffer,
                                          unsigned short baseVertexIndex);

    // W3D retains ownership of existing native texture references for now.
    // Both creation entry points support the original device-reset tracker.
    static RenderTexture2DHandle Create_Texture_2D(const RenderTexture2DDescription & description);
    // Source surface is borrowed. Native format conversion and texture
    // allocation remain inside the backend.
    static RenderTexture2DHandle Create_Texture_From_Surface(
        RenderSurfaceHandle surface, unsigned mipLevels);

    // Level/face mapping retains legacy in-place CPU upload semantics.
    // Textures must stay alive and mapped levels must be unmapped before use.
    // Cube faces use the W3D +X, -X, +Y, -Y, +Z, -Z order (indices 0..5).
    static unsigned Get_Texture_Mip_Count(RenderTextureHandle texture);
    // Returns a newly retained surface level. Caller must Release_Surface().
    static RenderSurfaceHandle Acquire_Texture_Surface_Level(
        RenderTexture2DHandle texture, unsigned mipLevel);
    // Copy between compatible texture allocations, preserving DX8's managed
    // to default memory-pool UpdateTexture behavior during transition.
    static void Update_Texture(RenderTextureHandle source, RenderTextureHandle destination);
    static void Map_Texture_2D(RenderTexture2DHandle texture, unsigned level,
                               RenderSurfaceMap & mapped);
    static void Unmap_Texture_2D(RenderTexture2DHandle texture, unsigned level);
    static void Map_Texture_Cube(RenderTextureCubeHandle texture, unsigned face,
                                 unsigned level, RenderSurfaceMap & mapped);
    static void Unmap_Texture_Cube(RenderTextureCubeHandle texture, unsigned face, unsigned level);
    static void Map_Texture_Volume(RenderTextureVolumeHandle texture, unsigned level,
                                   RenderVolumeMap & mapped);
    static void Unmap_Texture_Volume(RenderTextureVolumeHandle texture, unsigned level);
    static RenderTexture2DHandle Create_Depth_Texture_2D(const RenderTexture2DDescription & description);
    static RenderTextureCubeHandle Create_Texture_Cube(const RenderTextureCubeDescription & description);
    static RenderTextureVolumeHandle Create_Texture_Volume(const RenderTextureVolumeDescription & description);

    // Legacy image surfaces are distinct from textures and may also be
    // constructed from file data through the original W3D asset loader.
    // SurfaceClass retains native surface access for unmigrated lock/copy.
    static RenderSurfaceHandle Create_Surface(const RenderSurfaceDescription & description);
    static RenderSurfaceHandle Load_Surface(const char * filename);
    static void Describe_Surface(RenderSurfaceHandle surface, RenderSurfaceDescription & description);
    static void Retain_Surface(RenderSurfaceHandle surface);
    static void Release_Surface(RenderSurfaceHandle surface);
    static void Lock_Surface(RenderSurfaceHandle surface, RenderSurfaceMap & map,
                             const RenderSurfaceRect * region = nullptr, bool readOnly = false);
    static void Unlock_Surface(RenderSurfaceHandle surface);

    // Source/destination resources are borrowed; no ownership changes.
    // Null rectangles/points select the full-surface copy used by text atlases.
    static void Copy_Surface(RenderSurfaceHandle source, const RenderSurfaceRect * sourceRect,
                             RenderSurfaceHandle destination, const RenderSurfacePoint * destinationPoint);
    // Source and destination rectangles allow format conversion/stretching.
    static void Blit_Surface(RenderSurfaceHandle source, const RenderSurfaceRect & sourceRect,
                             RenderSurfaceHandle destination, const RenderSurfaceRect & destinationRect,
                             RenderSurfaceBlitFilter filter);

    // Texture stage binding retains the original DX8 reference cache,
    // texture-stage invalidation and AddRef/Release semantics. The abstract
    // handle supports all W3D texture kinds, including cube and volume maps.
    static void Bind_Texture(unsigned stage, RenderTextureHandle texture);

    // Explicit COM reference operations remain backend-specific. These
    // methods are also called by texture loader threads and must not assume
    // that the caller is the render thread. Null handles are valid no-ops.
    static void Retain_Texture(RenderTextureHandle texture);
    static void Release_Texture(RenderTextureHandle texture);

    // Apply W3D's resolved texture filtering and wrap/clamp modes through
    // backend-specific texture stage state, preserving DX8 state caching.
    static void Apply_Texture_Sampler(unsigned stage, const RenderSamplerDescription & sampler);
    static void Set_Texture_Max_Anisotropy(unsigned stage, unsigned level);
    static void Set_Texture_Filter(unsigned stage, RenderSamplerFilterField field,
                                   RenderSamplerFilter filter);
    static void Set_Texture_Address(unsigned stage, RenderSamplerAxis axis,
                                    RenderSamplerAddress address);

    // W3D material/shader state. Keep the historic SetRenderState ordering
    // and dirty-field update behavior inside the DX8 backend. Fixed-function
    // texture combiners, fog and patch factors are not migrated yet.
    static void Apply_Blend_State(const RenderBlendState & state);
    // These granular state setters retain the DX8 shader-manager's exact
    // ordering without applying or resetting other blend/depth fields.
    static void Set_Source_Blend(RenderBlendFactor factor);
    static void Set_Destination_Blend(RenderBlendFactor factor);
    static void Set_Blending_Enabled(bool enabled);
    static void Set_Texture_Factor(unsigned packedARGB);
    // Select whether the render target writes RGB channels and alpha.
    // Supports the legacy effects' alpha-only and RGB-only passes.
    static void Set_Color_Write_Mask(bool rgbEnabled, bool alphaEnabled);
    static void Set_Depth_Compare(RenderCompareFunc compare);
    static void Set_Depth_Write(bool enabled);
    static void Set_Cull_Mode(RenderCullMode cullMode);
    static void Set_Alpha_Test_Enabled(bool enabled);

    // W3D vertex-material state; keep the native material colors and
    // texture mapper algorithms on the legacy path until converted.
    static void Set_Vertex_Lighting(bool enabled);
    static void Set_Vertex_Color_Sources(const RenderMaterialColorSources & sources);
    static void Set_Texture_UV_Passthrough(unsigned stage, unsigned uvIndex);

    // Submit the actual material colors and specular exponent without
    // exposing the D3D8 material binary layout.
    static void Set_Vertex_Material(const RenderVertexMaterialDescription & material);

    // Submit the legacy pixel-combiner stage independently for color and
    // alpha, preserving ShaderClass dirty masks and state-write order.
    // uncached is a legacy escape hatch for Zero Hour's Voodoo3 stage 2;
    // new backends may ignore this implementation detail.
    static void Apply_Texture_Stage_Color(unsigned stage, const RenderTextureStageChannel & channel,
                                          bool uncached = false);
    static void Apply_Texture_Stage_Alpha(unsigned stage, const RenderTextureStageChannel & channel,
                                          bool uncached = false);

    // Individual fixed-function stage updates preserve the historical write
    // order and DX8 state cache, while no native D3D8 values reach callers.
    static void Set_Fixed_Stage_Operation(unsigned stage, bool alpha, RenderFixedStageOperation operation);
    static void Set_Fixed_Stage_Argument(unsigned stage, bool alpha, unsigned slot,
                                         RenderTextureArgument source, bool complement = false,
                                         bool replicateAlpha = false);
    static void Set_Texture_Coordinate_Source(unsigned stage, unsigned uvIndex, bool cameraSpacePosition);
    static void Set_Texture_Transform_2D(unsigned stage, bool enabled);

    static void Set_Ambient(const Vector3 & color);
    static void Set_Light_Environment(LightEnvironmentClass * light_env);
};
