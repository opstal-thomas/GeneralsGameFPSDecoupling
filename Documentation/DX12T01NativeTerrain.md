# DX12-T01 — Native heightmap base-pass integration

Status: **Native terrain implementation verified by Windows builds/WARP; real DX12-only gameplay and visual parity not yet validated**. This work does not claim complete T01 or parity with DX8.

## Authoritative terrain source and geometry

`HeightMapRenderObjClass` still constructs map cells using `WorldHeightMap::getDisplayHeight`, `getUVData`, `getAlphaUVData`, `doTheLight`, and the legacy `FLIP_TRIANGLES` corner order. Exclusive DX12 passes the resulting original `m_vertexBufferBackup` directly to the R02 transient geometry bindings and M01 indexed native draw. There are 32x32 source cells and 4 independent vertices per cell, with no artificial global mesh count. Right/bottom partial tile index lists preserve source row stride 32 and exclude out-of-map cells. The existing world/view/projection transforms, opaque PSO, R07 depth attachment, frame-barrier and fence-retained R02 staging are used. Legacy DX8 builds continue to follow the unmodified draw path.

Exclusive DX12 no longer constructs terrain-owned DX8 vertex/index buffers for this base pass. Moving point/spot/directional terrain lights now use the original `doTheDynamicLight()` normal and attenuation math in a separate per-frame CPU vertex snapshot. Terrain geometry changes invalidate this snapshot; if the camera/heightmap is updated after the frame's regular lighting callback, the base draw refreshes the snapshot before native submission, preventing a one-frame static-light flash during continuous camera scrolling. The static map vertex buffer is never overwritten by a dynamic light, and per-frame snapshots reset when lights move or become disabled. This mirrors the original W3D prelit terrain behavior without allocating DX8 hardware vertex buffers. Rendering of those resulting colors in a complete skirmish remains unverified. Height changes and camera-scrolling partial blocks continue to refresh CPU backup geometry.

## Atlas upload

`TerrainTextureClass::update` and `updateFlat` now use R04's map/unmap staging with original W3D A1R5G5B5 texels and row pitch in DX12. `AlphaEdgeTextureClass::update` uses original A8R8G8B8 texels. Native mip generation box-filters the original data; shared alpha-texture handle retains the original atlas resource, without D3D8 COM texture casts. The base pass samples stage zero with clamped UVs. Original DX8 routines remain under opposite backend guards.

## Validation and limitations

`scripts/tests/test_dx12_native_terrain.py` checks source contracts, partial tiles, original texture mappings and GPU test wiring. The existing `W3DDX12NativeDrawSmoke` WARP executable renders two adjacent **full 32×32-cell terrain tiles**, sampled from a real R04 16-bit atlas. The test checks R02 transient snapshot ownership, a height step, coverage at the seam, R07 depth occlusion, out-of-map clipping and GPU color readback; this validates *representative* GPU execution, not full in-game map images. All Windows CI results must be inspected after commit.

**Remaining blockers / explicitly not T01 verified:**
- DX12-only engine initialization remains B04 diagnostic-only, so native terrain has not yet been witnessed during an actual skirmish. Compare DX12 to DX8 in-game after bootstrap gates are removed.
- Texture-layer blends (including complex 2-/3-way cell transitions), lightmap/cloud stages and shoreline are not replicated by this base pass (T02/T05).
- The native **CPU dynamic-light path is implemented**, but in-game light placement/movement and DX8 color parity remain unverified. DX12 shader-manager dependencies and background/roads/shroud render code may still execute unmigrated DX8 operations in non-base paths (T06/T07 and related backend isolation audits).
- T01 must not be marked fully validated until both games boot DX12-only, base terrain survives camera movement, elevation/cliffs, border cells and map-change updates, and reference images show no differences attributable to T01.

## Manual DX12-only visual acceptance (pending B10)

1. Build and run both Generals and Zero Hour from the *exclusive* DX12 configuration (`W3D_RENDER_BACKEND=DX12`); do not use `W3D_ENABLE_DX12_GAMEPLAY_PREVIEW` and do not use DX8 fallback.
2. Use identical installed game data, map, resolution, camera position, visual settings and simulation pause to capture the same location with normal DX8 build and with DX12-only build.
3. Compare flat ground, sharp slopes and cliffs, right/bottom map boundaries, valleys, neighboring terrain tiles, and all base atlas materials for UV seams and missing textures. Also compare local point/spot-lit ground and verify that colors revert after lights move or switch off.
4. Pan/rotate/zoom around the entire map, trigger dynamic map height changes if available, and verify no holes/old geometry or overwritten GPU staging buffers.
5. Record builds/commit hashes, the two game versions, DX12/DX8 GPU logs and labeled screenshots. Do not infer game parity from standalone WARP smoke alone.

Recommended independent next milestone after completing B10/T01 gameplay acceptance: **DX12-T02** (terrain transitions/alpha blends).

## T01 follow-up changes

A second integration commit restores CPU-side dynamic W3D terrain lighting on a separate native vertex snapshot (retaining the untouched static atlas UVs/heights/color baseline), stops registering the DX12 heightmap with D3D8 cleanup, and strengthens WARP checks to draw two adjacent **full** 32x32-cell tiles rather than only two quads. The original W3D DX8 path is guarded and unchanged. These tests cannot substitute for a playable DX12-only skirmish and visual comparison.

## Scene ordering

The native base-pass renderer preserves `m_treeBuffer->setIsTerrain()`, the original per-scene handoff signaling that the terrain's opaque pass was rendered. This setter does not issue DX8 draws and keeps future tree/transparent sorting integration consistent. Actual tree/background rendering remains T11.

## LOD backend isolation

`BaseHeightMapRenderObjClass::adjustTerrainLOD` forces `TERRAIN_LOD_MAX` only in exclusive DX12 configurations. The existing low-detail fallback switches to `FlatHeightMapRenderObjClass`, which still performs legacy DX8 terrain rendering and is therefore forbidden when T01 is selected. Normal DX8 builds retain the original automatic/manual quality selection. Full-resolution native geometry remains active after terrain quality changes.

## Selected-backend real-game scene probe

An opt-in launch attempt is now available via W3D_DX12_T01_GAMEPLAY=1 on actual Generals and Zero Hour DX12-exclusive executables. It enters the original game HWND and WW3D scene (not B10 or the mirrored preview), prints native Present and authoritative heightmap tile/triangle/atlas evidence, and refuses DX8Wrapper device creation. Normal DX12 game startup remains fail-closed. See [DX12-T01 game-scene acceptance guide](DX12T01GameplayProbe.md). These source changes have not established that an interactive skirmish boots, renders, or matches DX8; only user-run installed-game comparison can close this acceptance gate.

### T01-only game-scene isolation

The opt-in game-scene probe keeps unfinished water, shroud and shadow GPU paths disabled under the exclusive DX12 selection, because these still execute D3D8 device methods (notably WaterRenderObjClass::ReAcquireResources). They are not implemented or marked complete; original DX8 builds retain the full code paths. See [T01 gameplay probe notes](DX12T01GameplayProbe.md).
