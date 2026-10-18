"""T01 native terrain boundary regressions (GPU pixels tested by WARP native smoke)."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
HEIGHT = (ROOT / "Core/GameEngineDevice/Source/W3DDevice/GameClient/HeightMap.cpp").read_text()
ATLAS = (ROOT / "Core/GameEngineDevice/Source/W3DDevice/GameClient/TerrainTex.cpp").read_text()
GEOM = (ROOT / "Core/Libraries/Source/WWVegas/WW3D2/Backend/DX12/DX12TerrainTileGeometry.h").read_text()
GPU = (ROOT / "Core/Libraries/Source/WWVegas/WW3D2/Backend/DX12/Presentation/DX12NativeDrawSmoke.cpp").read_text()
HEADER = (ROOT / "Core/GameEngineDevice/Include/W3DDevice/GameClient/HeightMap.h").read_text()
BASE = (ROOT / "Core/GameEngineDevice/Source/W3DDevice/GameClient/BaseHeightMap.cpp").read_text()
WW3D = (ROOT / "Core/Libraries/Source/WWVegas/WW3D2/ww3d.cpp").read_text()
DX12 = (ROOT / "Core/Libraries/Source/WWVegas/WW3D2/Backend/DX12/DX12Backend.cpp").read_text()
DX8_WRAPPER = (ROOT / "Core/Libraries/Source/WWVegas/WW3D2/dx8wrapper.cpp").read_text()
DISPLAY = (ROOT / "Core/GameEngineDevice/Source/W3DDevice/GameClient/W3DDisplay.cpp").read_text()
TERRAIN_VISUAL = (ROOT / "Core/GameEngineDevice/Source/W3DDevice/GameClient/W3DTerrainVisual.cpp").read_text()
SHADOW_GENERALS = (ROOT / "Generals/Code/GameEngineDevice/Source/W3DDevice/GameClient/Shadow/W3DShadow.cpp").read_text()
SHADOW_ZERO_HOUR = (ROOT / "GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/Shadow/W3DShadow.cpp").read_text()
TERRAIN_TRACKS = (ROOT / "Core/GameEngineDevice/Source/W3DDevice/GameClient/W3DTerrainTracks.cpp").read_text()
SMUDGES = (ROOT / "Core/GameEngineDevice/Source/W3DDevice/GameClient/W3DSmudge.cpp").read_text()
MOUSE = (ROOT / "Core/GameEngineDevice/Source/W3DDevice/GameClient/W3DMouse.cpp").read_text()
SNOW = (ROOT / "Core/GameEngineDevice/Source/W3DDevice/GameClient/W3DSnow.cpp").read_text()
DX12_TRACE = (ROOT / "Core/Libraries/Source/WWVegas/WW3D2/Backend/DX12/DX12StartupTrace.h").read_text()
MAIN_GENERALS = (ROOT / "Generals/Code/Main/WinMain.cpp").read_text()
MAIN_ZERO_HOUR = (ROOT / "GeneralsMD/Code/Main/WinMain.cpp").read_text()
CLIENT_GENERALS = (ROOT / "Generals/Code/GameEngine/Source/GameClient/GameClient.cpp").read_text()
CLIENT_ZERO_HOUR = (ROOT / "GeneralsMD/Code/GameEngine/Source/GameClient/GameClient.cpp").read_text()
STARTUP_ERRORS = (ROOT / "Core/GameEngine/Include/Common/Errors.h").read_text()
GAME_ENGINE_GENERAL = (ROOT / "Generals/Code/GameEngine/Source/Common/GameEngine.cpp").read_text()
GAME_ENGINE_ZERO_HOUR = (ROOT / "GeneralsMD/Code/GameEngine/Source/Common/GameEngine.cpp").read_text()


class NativeTerrainContract(unittest.TestCase):
    def test_uses_original_cpu_heightmap_not_preview(self):
        start = HEIGHT.index("void HeightMapRenderObjClass::Render(")
        native = HEIGHT[start:HEIGHT.index("#endif", start)]
        self.assertIn("m_vertexBufferBackup", native)
        self.assertIn("m_treeBuffer->setIsTerrain()", native)
        self.assertIn("getVertexBufferBackup(tileX, tileY)", native)
        self.assertIn("RenderDynamicVertexBinding vertex", native)
        self.assertIn("Renderer::Bind_Dynamic_Vertex_Buffer(vertex)", native)
        self.assertIn("Renderer::Bind_Dynamic_Index_Buffer(index, 0)", native)
        self.assertIn("Renderer::Draw_Triangles", native)
        self.assertNotIn("DX8Wrapper::Draw_Triangles", native)
        self.assertNotIn("Preview_DX12_Gameplay_Mesh", native)

    def test_preserves_original_cells_with_partial_right_bottom_tiles(self):
        for phrase in ("tileLength", "columns > tileLength", "rows > tileLength",
                       "(y * tileLength + x) * 4u", "out.push_back(base + 2)",
                       "out.push_back(base + 3)", "out.push_back(base + 1)"):
            self.assertIn(phrase, GEOM)
        self.assertIn("remainingX = m_x - 1", HEIGHT)
        self.assertIn("remainingY = m_y - 1", HEIGHT)
        self.assertIn("DX12TerrainTileGeometry::BuildIndices", HEIGHT)
        self.assertIn("terrainIndices.size() == 6144", GPU)
        self.assertIn("terrainIndices[12] == 128", GPU)

    def test_uses_actual_original_terrain_atlas(self):
        self.assertIn("Set_Texture(0, m_disableTextures ? nullptr : m_stageZeroTexture)", HEIGHT)
        self.assertIn("RENDER_SAMPLER_CLAMP", HEIGHT)
        self.assertIn("Set_Render_Texture(pBaseTex->Peek_Render_Texture())", ATLAS)
        self.assertIn("Renderer::Map_Texture_2D(texture, 0, mapped)", ATLAS)
        self.assertIn("BuildNativeTerrainMips(this, 2)", ATLAS)
        self.assertIn("BuildNativeTerrainMips(this, 4)", ATLAS)

    def test_gpu_pixels_cover_native_atlas_and_depth(self):
        self.assertIn("BindTransientVertex(device, src.data()", GPU)
        self.assertIn("BindTransientIndex(device, terrainIndices.data()", GPU)
        self.assertIn("readback->Map", GPU)
        self.assertIn("two full 32x32 terrain tiles: GPU UV, elevation, clip, and depth pixels", GPU)
        self.assertIn("terrainPass == 1", GPU)
        self.assertIn("for (unsigned tile=0; tile<terrainTiles.size(); ++tile)", GPU)


    def test_native_dynamic_lighting_preserves_original_baked_vertices(self):
        self.assertIn("std::vector<VERTEX_FORMAT> m_nativeDynamicVertices", HEADER)
        self.assertIn("m_nativeDynamicVertices.assign(m_vertexBufferBackup", HEIGHT)
        self.assertIn("m_nativeDynamicVertices.clear();", HEIGHT)
        self.assertIn("m_nativeDynamicVertices.data() +", HEIGHT)
        self.assertIn("doTheDynamicLight(vb, vbMirror", HEIGHT)
        self.assertIn("m_nativeDynamicVertices.empty() ?", HEIGHT)
        self.assertIn("if (m_nativeDynamicVertices.empty())", HEIGHT)
        self.assertIn("On_Frame_Update();", HEIGHT.split("void HeightMapRenderObjClass::Render(", 1)[1])
        self.assertIn("m_nativeDynamicVertices.clear();", HEIGHT.split("void HeightMapRenderObjClass::freeIndexVertexBuffers()", 1)[1])
        self.assertNotIn("T01 uses original baked terrain lighting", HEIGHT)

    def test_dx12_lod_changes_cannot_select_legacy_flatheightmap(self):
        lod = BASE.split("void BaseHeightMapRenderObjClass::adjustTerrainLOD(Int adj)", 1)[1]
        lod = lod.split("void BaseHeightMapRenderObjClass::ReleaseResources()", 1)[0]
        self.assertIn("#if defined(W3D_RENDER_BACKEND_DX12)", lod)
        self.assertIn("TheWritableGlobalData->m_terrainLOD = TERRAIN_LOD_MAX;", lod)
        self.assertIn("newROBJ = TheHeightMap;", lod)
        self.assertIn("newROBJ = TheFlatHeightMap;", lod)
        self.assertLess(lod.index("TheWritableGlobalData->m_terrainLOD = TERRAIN_LOD_MAX;"),
                        lod.index("newROBJ = TheFlatHeightMap;"))


    def test_real_game_scene_probe_is_explicit_and_not_dx8_preview(self):
        self.assertIn("W3D_DX12_T01_GAMEPLAY", DX12)
        self.assertIn("return b04Diagnostic || TerrainGameplayProbeOptIn();", DX12)
        self.assertIn("if (!DiagnosticOptIn())", DX12)
        self.assertIn("primaryWindow.Shutdown();", DX12)
        self.assertIn("W3D_DX12_T01_GAMEPLAY", HEIGHT)
        self.assertIn("Original heightmap frame=", HEIGHT)
        self.assertIn("tiles=%u triangles=%u atlas=%s", HEIGHT)
        self.assertIn("Native game HWND Present count=", DX12)

    def test_dx8_device_creation_stays_disabled_under_dx12_selection(self):
        for name in ("bool DX8Wrapper::Init(", "bool DX8Wrapper::Set_Render_Device(int dev"):
            body = DX8_WRAPPER.split(name, 1)[1].split("#endif", 1)[0]
            self.assertIn("#if defined(W3D_RENDER_BACKEND_DX12)", body)
            self.assertIn("return false;", body)
        self.assertIn("// BrowserEngine::Initialize receives", DISPLAY)
        self.assertIn("DX8WebBrowser::Initialize();", DISPLAY)
        self.assertIn("#if !defined(W3D_RENDER_BACKEND_DX12)", DISPLAY)

    def test_real_hwnd_matches_native_display_modes(self):
        self.assertIn("EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS", DISPLAY)
        self.assertIn("GetWindowLongPtrW(ApplicationHWnd, GWL_STYLE)", DISPLAY)
        self.assertIn("const bool borderless = (style & WS_POPUP) != 0", DISPLAY)
        self.assertIn("(style & WS_CAPTION) == 0;", DISPLAY)
        self.assertIn("setWindowed(!borderless);", DISPLAY)
        mode = DISPLAY.split("Int W3DDisplay::getDisplayModeCount()", 1)[1]
        self.assertIn("#if defined(W3D_RENDER_BACKEND_DX12)", mode)
        self.assertIn("return 1;", mode.split("#else", 1)[0])

    def test_game_settings_avoid_dx8_render_device_state(self):
        controls = WW3D.split("void WW3D::Set_Texture_Bitdepth(int bitdepth)", 1)[1]
        self.assertIn("NativeTerrainTextureBitdepth", controls)
        self.assertIn("#if defined(W3D_RENDER_BACKEND_DX12)", controls)
        self.assertIn("return MULTISAMPLE_MODE_NONE;", controls)
        self.assertIn("return Has_Active_Device() ? 2u : 0u;", DX12)


    def test_t01_game_probe_does_not_execute_unported_water_shroud_or_shadows(self):
        self.assertIn("#if !defined(W3D_RENDER_BACKEND_DX12)", TERRAIN_VISUAL)
        self.assertIn("TheWaterRenderObj=m_waterRenderObject = NEW_REF( WaterRenderObjClass", TERRAIN_VISUAL)
        self.assertIn("DX8-only T03 water RTT / shadow projection", DISPLAY)
        self.assertIn("T06 is not ported: shroud updates", DISPLAY)
        for source in (SHADOW_GENERALS, SHADOW_ZERO_HOUR):
            start = source.split("void DoShadows(RenderInfoClass & rinfo, Bool stencilPass)", 1)[1]
            self.assertIn("#if defined(W3D_RENDER_BACKEND_DX12)", start[:500])
            self.assertIn("return;", start.split("#endif", 1)[0])

    def test_dx12_uninitialized_shadow_children_never_enter_reset_or_shutdown(self):
        # Without init(), W3DProjectedShadowManager::reset/ReleaseResources
        # dereferences m_W3DShadowTextureManager (which stays null). This
        # happens in GameEngine::resetSubsystems after GameClient init.
        self.assertIn("TheW3DShadowManager->Reset();", TERRAIN_VISUAL)
        for shadow in (SHADOW_GENERALS, SHADOW_ZERO_HOUR):
            with self.subTest(game=shadow[:60]):
                ctor = shadow.split("W3DShadowManager::W3DShadowManager()", 1)[1]
                ctor = ctor.split("W3DShadowManager::~W3DShadowManager()", 1)[0]
                self.assertIn("#if !defined(W3D_RENDER_BACKEND_DX12)", ctor)
                self.assertIn("NEW W3DVolumetricShadowManager", ctor)
                self.assertIn("NEW W3DProjectedShadowManager", ctor)
                self.assertIn("TheW3DProjectedShadowManager->reset();", shadow)

    def test_selected_dx12_shroud_shader_and_snow_gpu_paths_are_isolated(self):
        ctor = BASE.split("BaseHeightMapRenderObjClass::BaseHeightMapRenderObjClass()", 1)[1]
        self.assertIn("#if defined(W3D_RENDER_BACKEND_DX12)", ctor[:4000])
        self.assertIn("m_shroud = nullptr;", ctor[:4000])
        reacquire = BASE.split("void BaseHeightMapRenderObjClass::ReAcquireResources()", 1)[1]
        self.assertIn("#if !defined(W3D_RENDER_BACKEND_DX12)", reacquire[:800])
        self.assertIn("W3DShaderManager::init();", reacquire)
        self.assertIn("TheW3DShadowManager->ReAcquireResources();", reacquire)
        for signature in ("Bool W3DSnowManager::ReAcquireResources()",
                          "void W3DSnowManager::render(RenderInfoClass &rinfo)"):
            with self.subTest(signature=signature):
                native = SNOW.split(signature, 1)[1].split("#endif", 1)[0]
                self.assertIn("#if defined(W3D_RENDER_BACKEND_DX12)", native)
                self.assertIn("return", native)
        self.assertIn("No legacy shader registry was initialized by the native T01 path", DISPLAY)

    def test_dx12_cursor_mode_uses_native_2d_instead_of_dx8_hardware_thread(self):
        init = MOUSE.split("void W3DMouse::init()", 1)[1].split("void W3DMouse::reset()", 1)[0]
        setter = MOUSE.split("void W3DMouse::setRedrawMode(RedrawMode mode)", 1)[1]
        self.assertIn("#if defined(W3D_RENDER_BACKEND_DX12)", init)
        self.assertIn("setRedrawMode(RM_POLYGON);", init)
        self.assertIn("#if defined(W3D_RENDER_BACKEND_DX12)", setter[:330])
        self.assertIn("mode = RM_POLYGON;", setter[:330])
        self.assertIn("TheDisplay->drawImage(image", MOUSE)

    def test_t01_omits_unported_terrain_track_and_smudge_gpu_operations(self):
        # The original terrain visual still creates its CPU effect managers;
        # T01 must not silently call their unported GPU paths.
        functions = (
            (TERRAIN_TRACKS, "void TerrainTracksRenderObjClassSystem::ReAcquireResources()"),
            (TERRAIN_TRACKS, "void TerrainTracksRenderObjClassSystem::flush()"),
            (SMUDGES, "void W3DSmudgeManager::ReAcquireResources()"),
            (SMUDGES, "void W3DSmudgeManager::render(RenderInfoClass &rinfo)"),
            (SMUDGES, "Bool W3DSmudgeManager::testHardwareSupport()"),
        )
        for source, signature in functions:
            with self.subTest(signature=signature):
                native = source.split(signature, 1)[1].split("#endif", 1)[0]
                self.assertIn("#if defined(W3D_RENDER_BACKEND_DX12)", native)
                self.assertRegex(native, r"\breturn(?:\s+FALSE)?;")
                self.assertNotIn("NEW_REF(DX8", native)
                self.assertNotIn("Renderer::Draw_", native)
        self.assertIn("TheTerrainTracksRenderObjClassSystem->init(W3DDisplay::m_3DScene);", TERRAIN_VISUAL)
        self.assertIn("TheSmudgeManager->init();", TERRAIN_VISUAL)
        self.assertIn("original heightmap render object constructed", TERRAIN_VISUAL)

    def test_t01_traces_terrain_init_and_native_first_frame_boundaries(self):
        for stage in (
            "W3DTerrainVisual::init entered",
            "terrain-track CPU manager initialized",
            "shadow manager bookkeeping constructed",
            "smudge CPU manager initialized",
            "W3DTerrainVisual::init completed",
        ):
            self.assertIn(stage, TERRAIN_VISUAL)
        for stage in (
            "W3DDisplay::draw first game-frame entry",
            "first WW3D::Begin_Render returned OK",
            "first drawViews completed",
            "first scene/UI frame reached WW3D::End_Render",
        ):
            self.assertIn(stage, DISPLAY)
        self.assertIn("first native game HWND frame requested", DX12)
        self.assertIn("first native command list and render targets ready", DX12)
        self.assertIn("first actual native game HWND present succeeded", DX12)

    def test_dx12_startup_does_not_show_the_legacy_dx8_requirement(self):
        self.assertIn("ERROR_INVALID_D3D", STARTUP_ERRORS)
        self.assertIn("ERROR_DX12_PROBE_NOT_ENABLED", STARTUP_ERRORS)
        self.assertIn("ERROR_INVALID_DX12", STARTUP_ERRORS)
        start = DISPLAY.index("void W3DDisplay::init()")
        body = DISPLAY[start:]
        self.assertLess(body.index('isDX12DeveloperOptIn("W3D_DX12_T01_GAMEPLAY")'),
                        body.index("WW3D::Init( ApplicationHWnd )"))
        self.assertIn("throw ERROR_DX12_PROBE_NOT_ENABLED;", body)
        self.assertEqual(body.count("throw ERROR_INVALID_DX12;"), 2)
        self.assertEqual(body.count("throw ERROR_INVALID_D3D;"), 2)
        self.assertIn("#if defined(W3D_RENDER_BACKEND_DX12)", body)
        for game_engine in (GAME_ENGINE_GENERAL, GAME_ENGINE_ZERO_HOUR):
            self.assertIn('RELEASE_CRASHLOCALIZED("ERROR:D3DFailurePrompt", '
                          '"ERROR:D3DFailureMessage")', game_engine)
            self.assertIn("ec == ERROR_DX12_PROBE_NOT_ENABLED", game_engine)
            self.assertIn("ec == ERROR_INVALID_DX12", game_engine)
            self.assertIn("W3D_DX12_T01_GAMEPLAY=1", game_engine)
            self.assertIn("Capture the [DX12 startup]", game_engine)

    def test_dx12_resolution_errors_are_diagnostic_instead_of_dx8_errors(self):
        self.assertIn("RejectNativeResolution(", DX12)
        for reason in ("renderer is not initialized", "unsupported bit depth or window mode",
                       "window mode disagrees with existing HWND style",
                       "invalid game HWND", "Win32 window resize failed",
                       "native swap-chain synchronization failed"):
            self.assertIn(reason, DX12)
        self.assertIn("ReportFailure(\"Renderer::Set_Device_Resolution\")", DX12)

    def test_dx12_startup_trace_reaches_the_real_window_and_game_client(self):
        self.assertIn("#if defined(W3D_RENDER_BACKEND_DX12) && defined(_WIN32)", DX12_TRACE)
        self.assertIn('GetTempPathA(MAX_PATH, temp)', DX12_TRACE)
        self.assertIn('GeneralsDX12Startup.log', DX12_TRACE)
        self.assertIn('CreateFileA(file, FILE_APPEND_DATA', DX12_TRACE)
        self.assertIn('OutputDebugStringA(line)', DX12_TRACE)
        for main in (MAIN_GENERALS, MAIN_ZERO_HOUR):
            self.assertIn('Main game HWND created; showing initial splash', main)
            self.assertIn('Main game HWND shown and stored; game startup can begin', main)
            self.assertIn('WinMain entering GameMain', main)
            self.assertIn('if (!hWnd)', main)
        for client in (CLIENT_GENERALS, CLIENT_ZERO_HOUR):
            for stage in ('GameClient::init entered', 'load mapped UI images',
                          'font library initialized', 'mouse cursor resources initialized',
                          'enter W3DDisplay::init', 'W3DDisplay::init completed',
                          'window manager initialized', 'shell UI initialized',
                          'terrain visual initialized'):
                self.assertIn(stage, client)
        for game_engine in (GAME_ENGINE_GENERAL, GAME_ENGINE_ZERO_HOUR):
            self.assertIn('GameEngine::init entered', game_engine)
            self.assertIn('original game subsystems reset successfully', game_engine)
            self.assertIn('GameEngine::execute entered original game loop', game_engine)
            self.assertIn('first original game update begins', game_engine)
            self.assertIn('GameEngine: TheGameClient initialized', game_engine)
            self.assertIn('GameEngine::init caught ErrorCode', game_engine)
            self.assertIn('GameEngine::init caught unexpected exception', game_engine)
        self.assertIn('Renderer::Init: native DX12 backend ready', DX12)
        self.assertIn('Renderer::End_Scene: first actual native game HWND present succeeded', DX12)

    def test_dx12_t01_scene_does_not_initialize_the_legacy_shader_registry(self):
        native = DISPLAY.split('void W3DDisplay::init()', 1)[1]
        self.assertIn('W3DShaderManager::init();', native)
        start = native.index('DX8 terrain transitions, screen filters')
        self.assertIn('#else', native[start:start+390])
        self.assertIn('skipping unsupported DX8 shader manager initialization', native)
        self.assertIn('W3DDisplay: W3D assets initialized', native)
        self.assertIn('W3DDisplay: scene and debug display initialized', native)

    def test_dx12_terrain_does_not_register_dx8_cleanup(self):
        self.assertIn("#if !defined(W3D_RENDER_BACKEND_DX12)", BASE)
        self.assertIn("// The DX12 heightmap never registers with the D3D8 reset/cleanup device.", BASE)
        self.assertIn("DX8Wrapper::SetCleanupHook(this);", BASE)


if __name__ == "__main__":
    unittest.main()
