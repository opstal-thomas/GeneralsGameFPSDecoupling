# DX12-B01 — DirectX 8 dependency inventory

This is a source-backed inventory of remaining Direct3D 8 dependencies,
**not** proof that a DX12 executable can launch without a Direct3D 8 device.

## Run locally

Use Python 3 and a normal Git checkout containing the full history:

~~~bash
python3 -m unittest discover -s scripts/tests -p 'test_dx8_dependency_inventory.py'
python3 scripts/check_w3d_dx8_boundaries.py
python3 scripts/dx8_dependency_inventory.py --check \
    --json-out build/dx8-inventory.json \
    --markdown-out build/dx8-inventory.md
~~~

The JSON inventory contains each file, line, exact symbol, dependency kind,
source ownership and code excerpt. The Markdown report summarizes totals,
the most affected files, and changes against the baseline. GitHub Actions
publishes both in its dx8-dependency-inventory artifact and Markdown job summary.

## Scope

The tool scans tracked C/C++ sources, headers, and build configurations
(CMake, MSVC project/link files, CI scripts). It detects:

- Direct DX8Wrapper method calls and legacy DX8 renderer types;
- D3D8 COM types and direct method calls on explicitly typed COM variables;
- D3DX legacy symbols and D3D8-era constants;
- Direct3D8 and DX8 wrapper includes;
- DX8 SDK, CMake, and native link/configuration references.

Findings are grouped into legacy_backend (allowed retained DX8 adapter),
w3d_runtime, game_runtime, shared_runtime, dx12_preview,
build_configuration, tools_tests and third_party. Only backend, test/tool,
and vendored source increases are exempt from the CI ratchet.

The immutable baseline commit in
[scripts/dx8_inventory_baseline.txt](../scripts/dx8_inventory_baseline.txt)
provides a reproducible source-of-truth without a hand-edited allowlist or
copy of thousands of entries. CI uses full Git history to scan that baseline
with git archive on each run.

A new occurrence of a (file, scope, dependency kind, symbol) beyond its
baseline count fails CI in a runtime or build file. Removing dependencies
always passes. Moving the same symbol inside a file without changing its
count does not register as a new use; this is a **ratchet**, not a full
code-semantic diff.

This check complements the stricter
[per-caller DX8 audit](../scripts/check_w3d_dx8_boundaries.py), which
continues to prohibit direct DX8 calls in explicitly migrated sources.

## Important limits

Lexical analysis is intentionally conservative and cannot prove the absence
of dependencies hidden behind macros, indirect function calls, arbitrary
dynamically loaded DLLs, or DX8-owned resources passed through neutral types.
Unreachable/legacy code is included when still tracked. Direct COM calls
through type aliases declared in another source file are not always resolved. The actual
absence of D3D8 runtime usage and DLL imports is checked in the later
DX12-V07 milestone.

Run this on every DX12 migration commit. Start DX12-B02 from the established
remaining runtime/build dependencies while keeping the original DX8 renderer
functional until the native DX12 game path passes its acceptance gates.
