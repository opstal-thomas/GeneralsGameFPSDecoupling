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
class Vector3;

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
          maxSimultaneousTextures(0)
    {
    }

    bool hardwareTransformAndLighting;
    bool nPatches;
    bool textureCompression;
    bool anisotropicFiltering;
    bool cubeTextures;
    bool bumpEnvironmentMapping;
    unsigned maxSimultaneousTextures;
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

    // Legacy bridge: resource allocation and final COM release are owned by
    // the backend, while DX8VertexBufferClass/DX8IndexBufferClass still store
    // native pointers for the unmigrated DX8 draw and lock paths. No native
    // types appear in the Renderer interface or the allocation descriptor.
    // The next step is moving buffer mapping and binding to opaque handles.
    static void * Create_Vertex_Buffer(const RenderBufferDescription & description);
    static void * Create_Index_Buffer(const RenderBufferDescription & description);
    static void Release_Vertex_Buffer(void * buffer);
    static void Release_Index_Buffer(void * buffer);
    static void Discard_Managed_Resources();

    static void Set_Ambient(const Vector3 & color);
    static void Set_Light_Environment(LightEnvironmentClass * light_env);
};
