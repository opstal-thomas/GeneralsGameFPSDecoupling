#!/usr/bin/env python3
"""Guard only W3D callers already migrated away from direct DX8 dispatch.

This is deliberately not a repo-wide DX8 ban: DX8Backend, remaining W3D
resources, custom stage combiners and the legacy fallback must still compile.
"""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
WW3D = Path("Core/Libraries/Source/WWVegas/WW3D2")
FULLY_ROUTED = [
    WW3D / "sortingrenderer.cpp",
    WW3D / "textureloader.cpp",
    WW3D / "surfaceclass.cpp",
    WW3D / "ringobj.cpp",
    WW3D / "line3d.cpp",
    WW3D / "pointgr.cpp",
    WW3D / "streakRender.cpp",
    Path("Core/GameEngineDevice/Source/W3DDevice/GameClient/W3DScorch.cpp"),
    Path("Core/GameEngineDevice/Source/W3DDevice/GameClient/W3DSmudge.cpp"),
    Path("Core/GameEngineDevice/Source/W3DDevice/GameClient/W3DMouse.cpp"),
    Path("Core/GameEngineDevice/Source/W3DDevice/GameClient/W3DSnow.cpp"),
    Path("Core/GameEngineDevice/Source/W3DDevice/GameClient/W3DTerrainBackground.cpp"),
    Path("Core/GameEngineDevice/Source/W3DDevice/GameClient/W3DTerrainParticles.cpp"),
    Path("Core/GameEngineDevice/Source/W3DDevice/GameClient/W3DTerrainTracks.cpp"),
    Path("Generals/Code/Libraries/Source/WWVegas/WW3D2/mesh.cpp"),
    Path("GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/mesh.cpp"),
]

CUSTOM_SHADER = Path(
    "Core/GameEngineDevice/Source/W3DDevice/GameClient/W3DShaderManager.cpp"
)
CUSTOM_SHADER_HEADER = Path(
    "Core/GameEngineDevice/Include/W3DDevice/GameClient/W3DShaderManager.h"
)
SHADER_MIGRATED_PATTERNS = [
    r"DX8Wrapper::_Get_D3D_Device8\s*\(\s*\)\s*->\s*SetTexture\s*\(",
    r"\bpDev\s*->\s*DrawPrimitiveUP\s*\(",
    r"\bpDev\s*->\s*SetVertexShader\s*\(",
    r"\bDX8Wrapper::Get_Current_Caps\s*\(",
    r"\bIDirect3D8\s*\*\s*d3d8Interface\b",
    r"\bDX8Wrapper::Set_DX8_Texture\s*\(",
    r"\bDX8Wrapper::Set_DX8_Render_State\s*\(",
    r"\bDX8Wrapper::Set_DX8_Texture_Stage_State\s*\(",
    r"\bDX8Wrapper::_Set_DX8_Transform\s*\(",
    r"\bDX8Wrapper::_Get_DX8_Transform\s*\(",
    r"DX8Wrapper::_Get_D3D_Device8\s*\(\s*\)\s*->\s*(GetRenderTarget|GetDepthStencilSurface|CreateTexture|SetRenderTarget)\s*\(",
    r"DX8Wrapper::_Get_D3D_Device8\s*\(\s*\)\s*->\s*(SetPixelShader|DeletePixelShader|SetPixelShaderConstant|CreatePixelShader|CreateVertexShader)\s*\(",
    r"DX8Wrapper::_Get_D3D_Device8\s*\(\s*\)\s*->\s*SetRenderState\s*\(\s*D3DRS_ALPHABLENDENABLE",
]

def executable_source(content: str) -> str:
    # Ignore comments so documentation or historical snippets do not block CI.
    return re.sub(r"/\*.*?\*/|//[^\r\n]*", "", content, flags=re.DOTALL)

def check(path: Path, patterns: list[str]) -> list[str]:
    target = ROOT / path
    if not target.is_file():
        return [f"{path}: missing source file"]
    text = executable_source(target.read_text(encoding="utf-8"))
    errors = []
    for pattern in patterns:
        for match in re.finditer(pattern, text):
            line = text.count("\n", 0, match.start()) + 1
            errors.append(f"{path}:{line}: direct legacy DX8 call: {match.group(0)}")
    return errors

def main() -> int:
    errors = []
    for path in FULLY_ROUTED:
        errors.extend(check(path, [r"\bDX8Wrapper::"]))
    errors.extend(check(Path("Core/GameEngineDevice/Source/W3DDevice/GameClient/W3DMouse.cpp"), [
        r"\bLPDIRECT3DDEVICE8\b",
        r"\bIDirect3DDevice8\b",
        r"\bD3DCURSOR_IMMEDIATE_UPDATE\b",
        r"\bPeek_D3D_Surface\s*\(",
    ]))
    errors.extend(check(WW3D / "surfaceclass.h", [
        r"\bIDirect3DSurface8\s*\*\s*D3DSurface\b",
    ]))
    errors.extend(check(WW3D / "texture.cpp", [
        r"\bPeek_D3D_Texture\s*\(\s*\)\s*->\s*GetLevelDesc\s*\(",
        r"\bD3DTexture\s*->\s*(?:GetPriority|SetPriority)\s*\(",
        r"\bd3d_texture\s*->\s*AddRef\s*\(",
        r"\bd3d_tex\s*->\s*Release\s*\(",
    ]))
    errors.extend(check(CUSTOM_SHADER, SHADER_MIGRATED_PATTERNS + [
        r"\bIDirect3D(?:Texture|Surface|BaseTexture|Device)8\b",
        r"\bPeek_D3D_Texture\s*\(",
    ]))
    errors.extend(check(Path("Core/GameEngineDevice/Source/W3DDevice/GameClient/W3DSmudge.cpp"), [
        r"\bIDirect3D(?:Texture|Surface|BaseTexture|Device)8\b",
        r"\bLPDIRECT3DDEVICE8\b",
        r"\bD3DLOCKED_RECT\b",
    ]))
    errors.extend(check(Path("Core/GameEngineDevice/Source/W3DDevice/GameClient/W3DSnow.cpp"), [
        r"\bIDirect3D(?:VertexBuffer|Device)8\b",
        r"\bLPDIRECT3DDEVICE8\b",
        r"\bD3D(?:USAGE|LOCK|FVF|RS)_\w+",
    ]))
    errors.extend(check(CUSTOM_SHADER_HEADER, [
        r"\bIDirect3DSurface8\s*\*\s*m_(oldRenderSurface|newRenderSurface|oldDepthSurface)",
        r"\bIDirect3DTexture8\s*\*\s*m_renderTexture",
        r"\bIDirect3D(?:Texture|Surface|BaseTexture|Device)8\b",
    ]))
    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    print("Migrated WW3D DX8 dispatch boundaries: PASS")
    return 0

if __name__ == "__main__":
    sys.exit(main())
