#!/usr/bin/env python3
"""Repository-wide lexical Direct3D 8 dependency inventory and CI ratchet.

This intentionally complements (not replaces) check_w3d_dx8_boundaries.py.
"""
from __future__ import annotations

import argparse
from bisect import bisect_right
from collections import Counter
import json
from pathlib import Path
import re
import subprocess
import sys
import tarfile

ROOT = Path(__file__).resolve().parents[1]
BASELINE_FILE = "scripts/dx8_inventory_baseline.txt"
VERSION = 1

SOURCE_EXTENSIONS = {".c", ".cc", ".cpp", ".cxx", ".h", ".hpp", ".hxx", ".inl", ".rc"}
BUILD_EXTENSIONS = {
    ".cmake", ".vcxproj", ".vcproj", ".props", ".targets",
    ".ps1", ".bat", ".cmd", ".yml", ".yaml", ".py", ".mk",
}
BUILD_FILENAMES = {"CMakeLists.txt", "CMakePresets.json", "Makefile", "GNUmakefile"}

# The retained DX8 adapter is allowed to refer to DX8. Other DX8-named
# renderers (notably mesh/polygon batching) remain live migration blockers.
BACKEND_FILES = {
    "dx8wrapper.cpp", "dx8wrapper.h", "dx8caps.cpp", "dx8caps.h",
    "dx8fvf.cpp", "dx8fvf.h", "dx8texman.cpp", "dx8texman.h",
    "dx8vertexbuffer.cpp", "dx8vertexbuffer.h",
    "dx8indexbuffer.cpp", "dx8indexbuffer.h",
}
PERMITTED_GROWTH_SCOPES = {"legacy_backend", "tools_tests", "third_party"}

SOURCE_PATTERNS = (
    ("wrapper_dispatch", re.compile(r"\bDX8Wrapper\s*::\s*[A-Za-z_]\w*")),
    ("direct3d8_com", re.compile(r"\b(?:IDirect3D[A-Za-z0-9_]*8|LPDIRECT3D[A-Z0-9_]*8)\b")),
    ("direct3d8_factory", re.compile(r"\bDirect3DCreate8\b")),
    ("d3dx_legacy", re.compile(r"\bD3DX[A-Za-z0-9_]*\b")),
    ("legacy_renderer_type", re.compile(r"\bDX8[A-Za-z0-9_]*\b")),
    ("legacy_d3d_type", re.compile(
        r"\bD3D(?!12|11|10|9|_FEATURE_LEVEL|_COMPILE|COMPILE|"
        r"_SHADER_|_ROOT_SIGNATURE|_PRIMITIVE_TOPOLOGY)[A-Z0-9_]+\b"
    )),
)
INCLUDE_PATTERN = re.compile(
    r'(?mi)^[ \t]*#[ \t]*include[ \t]*[<"]'
    r'([^>"]*(?:d3d8|d3dx8|dx8[a-z0-9_]*)\.(?:h|hpp))[>"]'
)
BUILD_PATTERN = re.compile(
    r"(?i)(?<![A-Za-z0-9_])(?:min-dx8-sdk|d3d8(?:\.lib)?|"
    r"d3dx8(?:\d+)?(?:\.lib)?|dx8\.cmake|dx8backend\.cpp|"
    r"dx8wrapper\.(?:h|cpp))(?![A-Za-z0-9_])"
)
COMMENT_AND_STRING = re.compile(
    r'//[^\r\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|'
    r"'(?:\\.|[^'\\])*'"
)


def eligible(path: str) -> bool:
    if path.startswith((".git/", "build/", "build-", ".cache/")):
        return False
    posix = Path(path)
    return posix.suffix.lower() in SOURCE_EXTENSIONS | BUILD_EXTENSIONS or posix.name in BUILD_FILENAMES


def is_build_file(path: str) -> bool:
    p = Path(path)
    return p.suffix.lower() in BUILD_EXTENSIONS or p.name in BUILD_FILENAMES


def scope_for(path: str) -> str:
    p = path.replace("\\", "/")
    name = p.rsplit("/", 1)[-1].lower()
    if p.startswith(("Tests/", "Benchmarks/", "Tools/", ".github/", "scripts/")) or "/Tools/" in p:
        return "tools_tests"
    if p.startswith(("Dependencies/", "third_party/", "ThirdParty/")):
        return "third_party"
    if is_build_file(p):
        return "build_configuration"
    if p.endswith("/Backend/DX8Backend.cpp"):
        return "legacy_backend"
    if p.startswith("Core/Libraries/Source/WWVegas/WW3D2/") and name in BACKEND_FILES:
        return "legacy_backend"
    if "/Backend/DX12/" in p:
        return "dx12_preview"
    if "/WW3D2/" in p:
        return "w3d_runtime"
    if p.startswith(("Core/GameEngineDevice/", "Generals/Code/", "GeneralsMD/Code/")):
        return "game_runtime"
    return "shared_runtime"


def mask_comments_and_strings(text: str) -> str:
    return COMMENT_AND_STRING.sub(
        lambda match: re.sub(r"[^\r\n]", " ", match.group(0)), text
    )


def scan_text(path: str, text: str) -> list[dict]:
    if not eligible(path):
        return []
    scope = scope_for(path)
    build_file = is_build_file(path) and Path(path).suffix.lower() not in SOURCE_EXTENSIONS
    newlines = [-1] + [match.start() for match in re.finditer("\n", text)]
    spans: list[tuple[int, int, str, str]] = []

    if build_file:
        prepared = re.sub(r"(?m)^[ \t]*#.*$", "", text)
        for match in BUILD_PATTERN.finditer(prepared):
            spans.append((match.start(), match.end(), "sdk_build_reference",
                          match.group(0).lower()))
        # Plain 'dx8' is a real CMake/MSVC link target, whereas its appearance
        # in a CI job title is not a dependency. Only inspect build files.
        if Path(path).name in BUILD_FILENAMES or Path(path).suffix.lower() in {
            ".cmake", ".vcxproj", ".vcproj", ".props", ".targets", ".mk"
        }:
            occupied = set()
            for start, end, _, _ in spans:
                occupied.update(range(start, end))
            for match in re.finditer(r"(?i)\bdx8\b", prepared):
                if not any(i in occupied for i in range(match.start(), match.end())):
                    spans.append((match.start(), match.end(),
                                  "sdk_build_reference", "dx8"))
    else:
        masked = mask_comments_and_strings(text)
        # Original include filename is masked as a string; check only if the
        # preprocessor directive itself survives comment masking.
        for match in INCLUDE_PATTERN.finditer(text):
            if re.match(r"\s*#[ \t]*include\b", masked[match.start():match.end()]):
                spans.append((match.start(), match.end(), "dx8_include",
                              match.group(1).lower()))
        occupied = set()
        for start, end, _, _ in spans:
            occupied.update(range(start, end))
        for kind, pattern in SOURCE_PATTERNS:
            for match in pattern.finditer(masked):
                if any(i in occupied for i in range(match.start(), match.end())):
                    continue
                symbol = re.sub(r"\s*::\s*", "::", match.group(0))
                spans.append((match.start(), match.end(), kind, symbol))
                occupied.update(range(match.start(), match.end()))

    output = []
    source_lines = text.splitlines()
    for start, _, kind, symbol in sorted(spans):
        line = bisect_right(newlines, start)
        excerpt = source_lines[line - 1].strip() if line <= len(source_lines) else ""
        output.append({
            "path": path, "line": line, "scope": scope,
            "kind": kind, "symbol": symbol, "snippet": excerpt[:180],
        })
    return output


def collect_current(root: Path) -> list[dict]:
    result = subprocess.run(
        ["git", "-C", str(root), "ls-files", "-z"], capture_output=True, check=True,
    )
    files = [f.decode("utf-8", "surrogateescape")
             for f in result.stdout.split(b"\0") if f]
    if not files:
        raise RuntimeError("No tracked files; refusing a false-green inventory")
    findings = []
    for path in sorted(files):
        if eligible(path):
            contents = (root / path).read_text(encoding="utf-8", errors="replace")
            findings.extend(scan_text(path, contents))
    return findings


def collect_revision(root: Path, revision: str) -> list[dict]:
    # Scan the actual immutable Git tree, not a hand-maintained suppression
    # list. A local full-history clone is enough; no network access needed.
    process = subprocess.Popen(
        ["git", "-C", str(root), "archive", "--format=tar", revision],
        stdout=subprocess.PIPE,
    )
    findings = []
    try:
        assert process.stdout is not None
        with tarfile.open(fileobj=process.stdout, mode="r|") as archive:
            for item in archive:
                if item.isfile() and eligible(item.name):
                    fileobj = archive.extractfile(item)
                    if fileobj:
                        contents = fileobj.read().decode("utf-8", errors="replace")
                        findings.extend(scan_text(item.name, contents))
    except BaseException:
        process.kill()
        raise
    finally:
        if process.stdout:
            process.stdout.close()
    if process.wait() != 0:
        raise RuntimeError("Baseline Git revision is unavailable: " + revision)
    return findings


def summarize(findings: list[dict]) -> dict:
    scopes = Counter(f["scope"] for f in findings)
    kinds = Counter(f["kind"] for f in findings)
    files = Counter(f["path"] for f in findings)
    return {
        "references": len(findings),
        "files_with_references": len(files),
        "by_scope": dict(sorted(scopes.items())),
        "by_kind": dict(sorted(kinds.items())),
        "top_files": [{"path": p, "references": n} for p, n in
                      sorted(files.items(), key=lambda p: (-p[1], p[0]))[:30]],
    }


def compare_findings(current: list[dict], baseline: list[dict]) -> dict:
    # Compare by path/kind/symbol multiplicity so unrelated line insertions
    # do not trigger false alarms. This is a ratchet, not an AST diff.
    def counts(data: list[dict]) -> Counter:
        return Counter((f["path"], f["scope"], f["kind"], f["symbol"])
                       for f in data)

    now, old = counts(current), counts(baseline)
    added, removed = [], []
    for key in sorted(set(now) | set(old)):
        delta = now[key] - old[key]
        if delta == 0:
            continue
        entry = {
            "path": key[0], "scope": key[1], "kind": key[2], "symbol": key[3],
            "baseline_count": old[key], "current_count": now[key], "delta": abs(delta),
        }
        if delta > 0:
            entry["allowed"] = key[1] in PERMITTED_GROWTH_SCOPES
            added.append(entry)
        else:
            removed.append(entry)
    return {
        "new_references": sum(entry["delta"] for entry in added),
        "resolved_references": sum(entry["delta"] for entry in removed),
        "blocking_additions": [entry for entry in added if not entry["allowed"]],
        "allowed_additions": [entry for entry in added if entry["allowed"]],
        "resolved": removed,
    }


def report_markdown(report: dict) -> str:
    summary = report["summary"]
    result = [
        "# Direct3D 8 dependency inventory", "",
        "Lexical inventory of tracked source/build files; not proof of binary "
        "or runtime independence from DX8.", "",
        f"**DX8 references:** {summary['references']} across "
        f"{summary['files_with_references']} tracked files.", "",
        "## Reference ownership", "", "| Scope | References |",
        "|---|---:|",
    ]
    for scope, count in summary["by_scope"].items():
        result.append(f"| {scope} | {count} |")
    result += ["", "## Reference kinds", "", "| Kind | References |", "|---|---:|"]
    for kind, count in summary["by_kind"].items():
        result.append(f"| {kind} | {count} |")
    result += ["", "## Top dependent files", "", "| File | References |", "|---|---:|"]
    for item in summary["top_files"]:
        path = item["path"].replace("|", "\\|")
        result.append(f"| {path} | {item['references']} |")
    if "comparison" in report:
        comparison = report["comparison"]
        result += [
            "", "## Baseline ratchet", "",
            "Baseline commit: " + report["baseline_ref"], "",
            f"- New references: {comparison['new_references']}",
            f"- Resolved references: {comparison['resolved_references']}",
            f"- Distinct blocking increases: {len(comparison['blocking_additions'])}",
            f"- Allowed adapter/tooling increases: {len(comparison['allowed_additions'])}",
        ]
        if comparison["blocking_additions"]:
            result += ["", "### New runtime/build dependencies (CI failure)", ""]
            for entry in comparison["blocking_additions"][:70]:
                result.append(
                    "- " + entry["path"] + ": " + entry["kind"] + " " +
                    entry["symbol"] + f" (+{entry['delta']})"
                )
    result += [
        "", "The existing migrated-caller boundary check remains active. "
        "DX12-V07 must also verify actual binary imports and DX8 device creation.", "",
    ]
    return "\n".join(result)


def read_baseline_ref(root: Path) -> str:
    for line in (root / BASELINE_FILE).read_text(encoding="utf-8").splitlines():
        value = line.strip()
        if value and not value.startswith("#"):
            if not re.fullmatch(r"[0-9a-f]{40}", value):
                raise ValueError("Expected an immutable full 40-hex baseline SHA")
            return value
    raise ValueError("Missing immutable baseline revision")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--baseline-ref")
    parser.add_argument("--check", action="store_true",
                        help="Fail on new references outside the retained backend/tools")
    parser.add_argument("--json-out", type=Path)
    parser.add_argument("--markdown-out", type=Path)
    args = parser.parse_args(argv)
    try:
        root = args.root.resolve()
        current = collect_current(root)
        reference = args.baseline_ref or read_baseline_ref(root)
        baseline = collect_revision(root, reference)
        report = {
            "schema_version": VERSION, "summary": summarize(current),
            "findings": current, "baseline_ref": reference,
            "baseline_summary": summarize(baseline),
            "comparison": compare_findings(current, baseline),
        }
        markdown = report_markdown(report)
        if args.json_out:
            args.json_out.parent.mkdir(parents=True, exist_ok=True)
            args.json_out.write_text(json.dumps(report, sort_keys=True, indent=2) + "\n",
                                     encoding="utf-8")
        if args.markdown_out:
            args.markdown_out.parent.mkdir(parents=True, exist_ok=True)
            args.markdown_out.write_text(markdown, encoding="utf-8")
        print(markdown)
        blocked = len(report["comparison"]["blocking_additions"])
        if args.check and blocked:
            print(f"FAIL: {blocked} new runtime/build DX8 dependency keys", file=sys.stderr)
            return 1
        print("DX8 dependency inventory: PASS")
        return 0
    except (OSError, RuntimeError, ValueError, subprocess.CalledProcessError) as error:
        print("DX8 dependency inventory error: " + str(error), file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
