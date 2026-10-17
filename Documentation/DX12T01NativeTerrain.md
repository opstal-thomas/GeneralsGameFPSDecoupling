# DX12-T01 — Native heightmap base-pass integration

Status: **Implemented in code; Windows build/WARP and DX12-only gameplay not yet validated**. This work does not claim complete T01 or parity with DX8.

## Authoritative terrain source and geometry

`HeightMapRenderObjClass` still constructs map cells using `WorldHeightMap::getDisplayHeight`, `getUVData`, `getAlphaUVData`, `doTheLight`, and the legacy `FLIP_TRIANGLES` corner order. Exclusive DX12 passes the resulting original `m_vertexBufferBackup` directly to the R02 transient geometry bindings and M01 indexed native draw. There are 32x32 source cells and 4 independent vertices per cell, with no artificial global mesh count. Right/bottom partial tile index lists preserve source row stride 32 and exclude out-of-map cells. The existing world/view/projection transforms, opaque PSO, R07 depth attachment, frame-barrier and fence-retained R02 staging are used. Legacy DX8 builds continue to follow the unmodified draw path.

Exclusive DX12 no longer constructs terrain-owned DX8 vertex/index buffers for this base pass. Dynamic lights are deliberately disabled for the exclusive DX12 base path until native per-vertex dynamic terrain lighting is migrated; original static diffuse is baked by `updateVB`. Height changes and camera-scrolling partial blocks continue to refresh CPU backup geometry.

## Atlas upload

`TerrainTextureClass::update` and `updateFlat` now use R04's map/unmap staging with original W3D A1R5G5B5 texels and row pitch in DX12. `AlphaEdgeTextureClass::update` uses original A8R8G8B8 texels. Native mip generation box-filters the original data; shared alpha-texture handle retains the original atlas resource, without D3D8 COM texture casts. The base pass samples stage zero with clamped UVs. Original DX8 routines remain under opposite backend guards.

## Validation and limitations

`scripts/tests/test_dx12_native_terrain.py` checks source contracts, partial tiles, original texture mappings and GPU test wiring. The existing `W3DDX12NativeDrawSmoke` WARP executable gains two cells sampled from a real R04 16-bit atlas, buffer lifetime on R02 pages and color/depth readback; this validates *representative* GPU execution, not full in-game map images. All Windows CI results must be inspected after commit.

**Remaining blockers / explicitly not T01 verified:**
- DX12-only engine initialization remains B04 diagnostic-only, so native terrain has not yet been witnessed during an actual skirmish. Compare DX12 to DX8 in-game after bootstrap gates are removed.
- Texture-layer blends (including complex 2-/3-way cell transitions), lightmap/cloud stages and shoreline are not replicated by this base pass (T02/T05).
- Dynamic terrain lights have not been migrated. DX12 shader-manager dependencies and background/roads/shroud render code may still execute unmigrated DX8 operations in non-base paths (T06/T07 and related backend isolation audits).
- T01 must not be marked fully validated until both games boot DX12-only, base terrain survives camera movement, elevation/cliffs, border cells and map-change updates, and reference images show no differences attributable to T01.

## Manual DX12-only visual acceptance (pending B10)

1. Build and run both Generals and Zero Hour from the *exclusive* DX12 configuration (`W3D_RENDER_BACKEND=DX12`); do not use `W3D_ENABLE_DX12_GAMEPLAY_PREVIEW` and do not use DX8 fallback.
2. Use identical installed game data, map, resolution, camera position, visual settings and simulation pause to capture the same location with normal DX8 build and with DX12-only build.
3. Compare flat ground, sharp slopes and cliffs, right/bottom map boundaries, valleys, neighboring terrain tiles, and all base atlas materials for UV seams and missing textures.
4. Pan/rotate/zoom around the entire map, trigger dynamic map height changes if available, and verify no holes/old geometry or overwritten GPU staging buffers.
5. Record builds/commit hashes, the two game versions, DX12/DX8 GPU logs and labeled screenshots. Do not infer game parity from standalone WARP smoke alone.

Recommended independent next milestone after completing B10/T01 gameplay acceptance: **DX12-T02** (terrain transitions/alpha blends).
