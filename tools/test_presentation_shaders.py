#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 PCSX2 Dev Team
# SPDX-License-Identifier: GPL-3.0+

"""Compile all presentation shaders and optionally run standalone Metal image/timing checks.

(AI-assisted) Requires glslangValidator and dxc on PATH. Metal checks additionally
require macOS and Xcode, and explicit Git references for the two comparisons:
  python3 tools/test_presentation_shaders.py --metal \
    --baseline-ref 395e33577 --legacy-ref archive/shaders-ec23402bf
Only generated test fixtures are used. No emulator configuration is accessed.
"""

import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile

ENTRIES = (
    "ps_copy", "ps_filter_scanlines", "ps_filter_diagonal", "ps_filter_triangular",
    "ps_filter_complex", "ps_filter_lottes", "ps_4x_rgss", "ps_automagical_supersampling",
)
ROOT = Path(__file__).resolve().parents[1]


def run(*args):
    result = subprocess.run(args, cwd=ROOT, text=True, capture_output=True)
    if result.returncode:
        raise RuntimeError(f"{' '.join(map(str, args))}\n{result.stdout}{result.stderr}")
    return result.stdout


def git_source(ref, path):
    return run("git", "show", f"{ref}:{path}")


def compile_shaders(work):
    count = 0
    for backend in ("opengl", "vulkan"):
        source = (ROOT / f"bin/resources/shaders/{backend}/present.glsl").read_text()
        for entry in (*ENTRIES, "vertex"):
            vertex = entry == "vertex"
            defines = "#define VERTEX_SHADER 1\n" if vertex else "#define FRAGMENT_SHADER 1\n"
            if not vertex or backend == "opengl":
                defines += f"#define {'vs_main' if vertex else entry} main\n"
            path = work / f"{backend}-{entry}.{'vert' if vertex else 'frag'}"
            path.write_text("#version 450 core\n" + defines + source)
            run("glslangValidator", "-G" if backend == "opengl" else "-V",
                "--auto-map-locations", "--auto-map-bindings", str(path), "-o", str(path.with_suffix(".spv")))
            count += 1
    for entry in (*ENTRIES, "vs_main"):
        run("dxc", "-T", "vs_6_0" if entry == "vs_main" else "ps_6_0", "-E", entry,
            str(ROOT / "bin/resources/shaders/dx11/present.fx"), "-Fo", str(work / f"{entry}.dxil"))
        count += 1
    print(f"{count}/{count} GLSL/HLSL presentation shader entry points compiled", flush=True)


def metal_checks(work, baseline_ref, legacy_ref):
    vertex = work / "vertex.metal"
    vertex.write_text("""#include <metal_stdlib>
using namespace metal;
struct Data { float4 p [[position]]; float2 t; };
vertex Data vs_test(uint id [[vertex_id]], constant float4& rect [[buffer(0)]])
{
    float2 positions[] = {float2(-1, 1), float2(1, 1), float2(-1, -1), float2(1, -1)};
    float2 uv[] = {float2(0, 0), float2(1, 0), float2(0, 1), float2(1, 1)};
    return {float4(positions[id], 0.5, 1), mix(rect.xy, rect.zw, uv[id])};
}
""")
    run("xcrun", "-sdk", "macosx", "metal", "-std=macos-metal2.2", "-c", str(vertex), "-o", str(work / "vertex.air"))
    libraries = []
    for name, ref in (("current", None), ("baseline", baseline_ref), ("legacy", legacy_ref)):
        directory = work / name
        directory.mkdir()
        common = directory / "Common"
        metal = directory / "Metal"
        common.mkdir()
        metal.mkdir()
        for filename in ("GSMTLSharedHeader.h", "GSMTLShaderCommon.h", "present.metal"):
            path = f"pcsx2/GS/Renderers/Metal/{filename}"
            source = git_source(ref, path) if ref else (ROOT / path).read_text()
            if name == "baseline" and filename == "present.metal":
                source += """
fragment float4 ps_present_copy(ConvertShaderData data [[stage_in]], ConvertPSRes res)
{ return res.sample(data.t); }
"""
            (metal / filename).write_text(source)
        path = "pcsx2/GS/Renderers/Common/GSShaderEnums.h"
        (common / "GSShaderEnums.h").write_text((ROOT / path).read_text())
        air = directory / "present.air"
        run("xcrun", "-sdk", "macosx", "metal", "-std=macos-metal2.2", "-c", str(metal / "present.metal"), "-o", str(air))
        library = directory / "present.metallib"
        run("xcrun", "-sdk", "macosx", "metallib", str(air), str(work / "vertex.air"), "-o", str(library))
        libraries.append(str(library))
    executable = work / "metal-checks"
    run("xcrun", "clang++", "-std=c++20", "-fobjc-arc", "-framework", "Foundation", "-framework", "Metal",
        str(ROOT / "tools/test_presentation_effects.mm"), "-o", str(executable))
    print(run(str(executable), *libraries), end="", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--metal", action="store_true")
    parser.add_argument("--baseline-ref")
    parser.add_argument("--legacy-ref")
    args = parser.parse_args()
    for tool in ("glslangValidator", "dxc"):
        if not shutil.which(tool):
            parser.error(f"Required compiler missing: {tool}")
    if args.metal and not (args.baseline_ref and args.legacy_ref):
        parser.error("--metal requires --baseline-ref and --legacy-ref")
    with tempfile.TemporaryDirectory(prefix="pcsx2-present-") as temporary:
        work = Path(temporary)
        compile_shaders(work)
        if args.metal:
            metal_checks(work, args.baseline_ref, args.legacy_ref)


if __name__ == "__main__":
    main()
