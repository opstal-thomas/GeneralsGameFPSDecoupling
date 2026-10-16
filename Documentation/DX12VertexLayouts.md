# DX12-R03 — Native W3D vertex formats and index widths

**Status:** Implemented and validated in native DX12/WARP. This is a GPU resource
and input-assembly milestone, **not** the M01/B07 authoritative gameplay draw migration.

## Input layout translation

The exclusive DX12 backend translates legacy W3D/D3D8 FVF numbers using
Core/Libraries/Source/WWVegas/WW3D2/Backend/DX12/DX12W3DVertexLayouts.{h,cpp},
without including or linking Direct3D 8 or D3DX8. Each
DX12W3DVertexLayout owns D3D12_INPUT_ELEMENT_DESC entries, source FVF,
the tight byte stride and flags indicating pretransformed positions and
packed-color swizzling. NativeInputLayout() feeds native graphics PSOs.

| W3D / D3D8 FVF components | Native input-assembler representation |
| --- | --- |
| XYZ | POSITION0 float3 |
| XYZRHW, XYZW | POSITION0 float4, transformed flag for XYZRHW |
| XYZB1–XYZB5 | Up to five float weights in BLENDWEIGHT0/1 |
| LASTBETA_UBYTE4, LASTBETA_D3DCOLOR | Packed BLENDINDICES0, final beta replaced by indices |
| NORMAL, PSIZE | NORMAL0 float3; PSIZE0 float |
| DIFFUSE, SPECULAR | Packed COLOR0 and COLOR1, R8G8B8A8_UNORM |
| TEX0–TEX8 | TEXCOORD0–7, each with 1–4 floats |

Legacy D3DCOLOR memory is **BGRA**. IA exposes raw 8-bit channels; a
native shader must explicitly swizzle .bgra back to RGBA. The WARP draw
uses raw 0xFFFF0000 diffuse and verifies the red output channel.

FVF decoding rejects unknown/reserved bits, unsupported position formats,
contradictory LASTBETA variants, blend indices without blend-weight
positions, invalid transformed normals and size flags for undeclared UVs.
CreateVertex validates the FVF and whole-vertex byte size. BindVertex
validates the minimum decoded stride, preserving R02's upload slice offset.

## Index format

RenderBufferDescription gains a backend-neutral RenderIndexElementFormat:
RENDER_INDEX_UINT16 (2 bytes, default) or RENDER_INDEX_UINT32 (4 bytes).
Existing W3D IndexBufferClass and dynamic indexing remain 16-bit by
default. Both DX8 and DX12 backend allocations honor explicit 32-bit
requests. The native DX12 binding selects DXGI_FORMAT_R16_UINT or
DXGI_FORMAT_R32_UINT and scales firstIndex / indexCount byte ranges
according to the selected element size.

The decoded DX12W3DVertexLayout and native index element width remain
available through backend-private handle accessors for future M01/B07
graphics-PSO and draw-submission integration.

## Verification

- [DX12 W3D Shader Validation — 2026-10-16](https://github.com/opstal-thomas/GeneralsGameFPSDecoupling/actions/runs/38913332469):
  all 11 WARP tests passed on **both Win32 and x64**, plus source-contract
  checks and shader compilation. The dedicated R03 test covers 17 valid
  FVF variations, rejected malformed variants, normals, blend weights,
  packed colors, tangent/displacement UV layouts and the native PSO built
  from a decoded FVF. A real WARP DrawIndexedInstanced call uses 32-bit
  indices; readback verifies the rendered red pixel and raw index values.
- [Windows renderer/game build and boundary audit — 2026-10-16](https://github.com/opstal-thomas/GeneralsGameFPSDecoupling/actions/runs/38912001126):
  all seven Windows Generals/Zero Hour game builds and the dependency audit
  passed on the core R03 implementation. Subsequent R03 commits only
  modified the format test/contract sources.

**Scope limit:** R03 handles native input geometry representations, not
the original game's full shader execution. Actual W3D scene PSO/draw
submission, skin animation, transformed-screen draw semantics and full
material permutations remain M01/B07 and related rendering tasks.
DX8 stays the playable reference renderer; the DX12 mesh preview still
uses separately canonicalized vertices until M01/B07 integrates raw W3D draws.
