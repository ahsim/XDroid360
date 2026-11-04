"""Makes DAO's game profile for building the game on the phone (build/profile/dao.zip), in the
layout of the recipe format (SD1/toolchain/RECIPE_FORMAT_DRAFT.md, format 2):

    recipe.json               the steps the launcher runs
    lib/libdao-game.a         the runtime and the front end, linked whole (static initializers)
    lib/libdao-deps.a         everything else the game library links: XenonUtils, Xenia's GPU
                              and UI, glslang, dxbc, snappy, imgui, fmt, xxhash, FFmpeg's
                              avcodec/avutil, linkernsbypass
    include/                  what the phone compiles the generated code with: mmio_hook.h,
                              SIMDe, the build descriptor (build_info*)
    xr/                       XenonRecomp's config template, switch tables, ppc_context.h

The archives come from the Android build of the game library (build/tool/android, NDK 27.3,
the launcher's SDL 3.4.18), with debug info stripped. The phone translates the game's code
and compiles it itself; nothing of the game is in the profile.

    python -I scripts/make_dao_profile.py [--build build/tool/android] [--kit <kit folder>] [--check]

--check links a game library on this PC the way the phone will (the recipe's own link line,
the kit's sysroot, this PC's already compiled game code) and checks that nothing is undefined
and that the library carries the ABI note.
"""
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RECOMP = ROOT / "recomp"
LLVM = RECOMP / "tools" / "llvm" / "bin"
NINJA = RECOMP / "tools" / "ninja" / "ninja.exe"
DEFAULT_KIT = ROOT.parent / "SD1" / "toolchain" / "out" / "kit"
VERSION = "dao-1"
TITLE_ID = "454108C0"
# Linked whole into dao-game, or compiled on the phone: not in dao-deps.
NOT_DEPS = {"libdaodroid-runtime.a", "libdaobuildinfo.a", "libdaoppc.a"}
# P0 on the S25: this unit alone takes about 120 s, so it starts first.
COST_HINTS = {"ppc_recomp.17.cpp": 120}


def exe(name):
    return str(LLVM / (name + (".exe" if os.name == "nt" else "")))


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def link_libraries(build):
    """The static libraries the game library links (build.ninja's LINK_LIBRARIES of libgame.so)."""
    text = (build / "build.ninja").read_text(encoding="utf-8", errors="replace")
    m = re.search(r"^build libgame\.so:.*?^  LINK_LIBRARIES = (.*?)$", text, re.M | re.S)
    if not m:
        sys.exit(f"No libgame.so link in {build}/build.ninja: configure the Android build first.")
    libs = []
    for word in m.group(1).split():
        if word.endswith(".a") and word not in libs:
            libs.append(word)
    return libs


def merge(archives, out):
    """One archive with every member of archives (llvm-ar's MRI script)."""
    out.unlink(missing_ok=True)
    script = "CREATE " + str(out) + "\n" + "".join(f"ADDLIB {a}\n" for a in archives) + "SAVE\nEND\n"
    subprocess.run([exe("llvm-ar"), "-M"], input=script, text=True, check=True)
    subprocess.run([exe("llvm-strip"), "-g", str(out)], check=True)


def toml_template():
    """config/DAO.toml with its paths pointing at the phone's folders (absolute, as is)."""
    text = (RECOMP / "config" / "DAO.toml").read_text(encoding="utf-8")
    for key, value in (("file_path", "${GAME}/game/default.xex"), ("out_directory_path", "${WORK}/ppc"),
                       ("switch_table_file_path", "${PROFILE}/xr/DAO_switch_tables.toml")):
        text, n = re.subn(rf"^{key}\s*=.*$", f'{key} = "{value}"', text, count=1, flags=re.M)
        if n != 1:
            sys.exit(f"config/DAO.toml has no {key}")
    return text


def recipe(kit, xex_sha256):
    ppc = RECOMP / "ppc"
    cxx = ["--target=aarch64-none-linux-android29", "--sysroot=${KIT}/sysroot",
           "-resource-dir", "${KIT}/clang/22",
           "-DANDROID", "-fdata-sections", "-ffunction-sections", "-funwind-tables",
           "-fstack-protector-strong", "-D_FORTIFY_SOURCE=2", "-O3", "-DNDEBUG",
           "-std=gnu++20", "-fPIC"]
    ppcflags = ["-fno-strict-aliasing", "-ffp-model=strict", "-ffp-contract=off",
                "-Wno-unused-label", "-Wno-unused-variable", "-Wno-unused-but-set-variable",
                "-I${WORK}/ppc", "-I${PROFILE}/include/simde",
                "-include", "${PROFILE}/include/mmio_hook.h"]
    pch = ["-Xclang", "-include-pch", "-Xclang", "${WORK}/pch/pch.hxx.pch",
           "-Xclang", "-include", "-Xclang", "${WORK}/pch/pch.hxx"]
    builtins = "${KIT}/clang/lib/libclang_rt.builtins-aarch64-android.a"
    lib = "${KIT}/sysroot/usr/lib/aarch64-linux-android"
    return {
        "format": 2,
        "version": VERSION,
        "game": {"id": "dao", "title": "Dragon Age: Origins", "title_ids": [TITLE_ID]},
        "source": {"formats": ["xiso"], "pick": "file"},
        "requires": {"kit": kit["kit"], "abi": abi_version(),
                     "tools": {"clang": kit["tools"]["clang"], "xenonrecomp": kit["tools"]["xenonrecomp"]}},
        # Game files: 6.4 GB on the PC; work: 253 MB of generated C++, 378 MB of objects, the PCH.
        "space": {"game_mb": 6800, "work_mb": 1000, "install_mb": 300},
        "inputs": {"xex": {"path": "${GAME}/game/default.xex", "sha256": [xex_sha256]}},
        "vars": {"CXXFLAGS": cxx, "PPCFLAGS": ppcflags, "PCH": pch},
        "jobs": {"max_rss_mb": 450, "reserve_mb": 1024},
        "steps": [
            {"id": "extract", "kind": "extract", "label": "Extracting game files", "weight": 120,
             "format": "xiso", "to": "${GAME}", "keeps_game": True},
            {"id": "check-xex", "kind": "verify", "label": "Checking game version", "weight": 1,
             "files": {"${GAME}/game/default.xex": [xex_sha256]},
             "message": "unsupported_game_version"},
            {"id": "xr-config", "kind": "write", "weight": 0, "creates": ["${WORK}/xr"],
             "path": "${WORK}/xr/DAO.toml", "from": "${PROFILE}/xr/DAO.toml.in"},
            {"id": "recomp", "kind": "run", "label": "Translating game code", "weight": 10,
             "tool": "xenonrecomp", "args": ["${WORK}/xr/DAO.toml", "${PROFILE}/xr/ppc_context.h"],
             "creates": ["${WORK}/ppc"], "progress": "Recompiling functions\\.\\.\\. ([0-9.]+)%"},
            # The prebuilt runtime is compiled against these generated headers (stdafx.h).
            {"id": "check-generated", "kind": "verify", "weight": 0,
             "files": {"${WORK}/ppc/ppc_config.h": [sha256(ppc / "ppc_config.h")],
                       "${WORK}/ppc/ppc_context.h": [sha256(ppc / "ppc_context.h")]},
             "message": "profile_mismatch"},
            {"id": "pch-stub", "kind": "write", "weight": 0, "creates": ["${WORK}/pch"],
             "path": "${WORK}/pch/pch.hxx",
             "text": "#pragma clang system_header\n#include \"${WORK}/ppc/ppc_recomp_shared.h\"\n"},
            {"id": "pch", "kind": "run", "label": "Preparing compiler", "weight": 2, "tool": "clang",
             "args": ["@CXXFLAGS", "@PPCFLAGS", "-x", "c++-header", "${WORK}/pch/pch.hxx",
                      "-o", "${WORK}/pch/pch.hxx.pch"]},
            {"id": "game-code", "kind": "each", "label": "Compiling game code", "weight": 180,
             "tool": "clang",
             "inputs": {"glob": "${WORK}/ppc/ppc_recomp.*.cpp", "plus": ["${WORK}/ppc/ppc_func_mapping.cpp"]},
             "args": ["@CXXFLAGS", "@PPCFLAGS", "@PCH", "-c", "${IN}", "-o", "${OBJ}"],
             "objects": "${WORK}/obj/game-code",
             "schedule": "longest_first", "cost_hints": COST_HINTS, "fail": "stop"},
            {"id": "build-info", "kind": "each", "label": "Compiling game code", "weight": 2,
             "tool": "clang",
             "inputs": {"plus": ["${PROFILE}/include/build_info_android.cpp"]},
             "args": ["@CXXFLAGS", "-I${WORK}/ppc", "-I${PROFILE}/include", "-I${PROFILE}/include/simde",
                      "-DDAO_BUILD_INFO=DaoBuildDisc", "-DDAO_BUILD_MAPPINGS=PPCFuncMappings",
                      "-c", "${IN}", "-o", "${OBJ}"],
             "objects": "${WORK}/obj/build-info"},
            {"id": "link", "kind": "run", "label": "Linking", "weight": 8, "tool": "lld",
             "message": "link_failed",
             "args": ["-flavor", "gnu",
                      "--sysroot=${KIT}/sysroot", "-EL", "--fix-cortex-a53-843419",
                      "-z", "now", "-z", "relro", "-z", "max-page-size=16384",
                      "--hash-style=gnu", "--eh-frame-hdr", "-m", "aarch64linux", "-shared",
                      "-soname", "libgame.so", "--build-id=sha1", "--no-rosegment",
                      "--no-undefined", "--gc-sections", "-Bsymbolic", "--strip-debug",
                      "-o", "${WORK}/libgame.so",
                      lib + "/29/crtbegin_so.o", "-L" + lib + "/29", "-L" + lib,
                      "--whole-archive", "${PROFILE}/lib/libdao-game.a", "--no-whole-archive",
                      "@objects:build-info", "@objects:game-code",
                      "${PROFILE}/lib/libdao-deps.a",
                      "${TOOLS}/libSDL3.so",
                      "-landroid", "-llog", "-ldl", "-latomic", "-lm",
                      "-Bstatic", "-lc++", "-Bdynamic", "-lm",
                      builtins, "-l:libunwind.a", "-ldl", "-lc",
                      builtins, "-l:libunwind.a", "-ldl",
                      lib + "/29/crtend_so.o"]},
        ],
        "result": {"library": "${WORK}/libgame.so", "install": "${INSTANCE}/libgame.so", "keep": ["${WORK}/logs"]},
    }


def abi_version():
    text = (ROOT / "android" / "app" / "src" / "main" / "cpp" / "dao_abi.h").read_text(encoding="utf-8")
    return int(re.search(r"#define\s+DAO_ABI_VERSION\s+(\d+)", text).group(1))


def stage(build, kit, out):
    libs = link_libraries(build)
    targets = ["dao-front"] + libs
    subprocess.run([str(NINJA), "-C", str(build)] + targets, check=True)
    deps = [build / l for l in libs if Path(l).name not in NOT_DEPS]
    runtime = [build / l for l in libs if Path(l).name == "libdaodroid-runtime.a"]
    if not runtime:
        sys.exit("The game library doesn't link libdaodroid-runtime.a")
    xex = ROOT / "install" / "game" / "default.xex"
    if not xex.is_file():
        sys.exit(f"No {xex}: the supported disc's default.xex gives the recipe its hash.")

    shutil.rmtree(out, ignore_errors=True)
    (out / "lib").mkdir(parents=True)
    merge([build / "libdao-front.a"] + runtime, out / "lib" / "libdao-game.a")
    merge(deps, out / "lib" / "libdao-deps.a")

    inc = out / "include"
    inc.mkdir()
    for f in ("mmio_hook.h", "build_info.h", "build_info.cpp"):
        shutil.copy2(RECOMP / "DAORecompLib" / f, inc / f)
    shutil.copy2(ROOT / "android" / "runtime" / "build_info_android.cpp", inc / "build_info_android.cpp")
    shutil.copytree(RECOMP / "tools" / "XenonRecomp" / "thirdparty" / "simde", inc / "simde",
                    ignore=shutil.ignore_patterns(".git", ".github", "test", "*.md"))

    xr = out / "xr"
    xr.mkdir()
    (xr / "DAO.toml.in").write_text(toml_template(), encoding="utf-8", newline="\n")
    shutil.copy2(RECOMP / "config" / "DAO_switch_tables.toml", xr / "DAO_switch_tables.toml")
    shutil.copy2(RECOMP / "tools" / "XenonRecomp" / "XenonUtils" / "ppc_context.h", xr / "ppc_context.h")

    r = recipe(kit, sha256(xex))
    (out / "recipe.json").write_text(json.dumps(r, indent=2) + "\n", encoding="utf-8", newline="\n")
    return r


def pack(folder, zip_path):
    zip_path.unlink(missing_ok=True)
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as z:
        z.write(folder / "recipe.json", "recipe.json")  # first: the launcher reads it alone
        for p in sorted(folder.rglob("*")):
            if p.is_file() and p.name != "recipe.json":
                z.write(p, p.relative_to(folder).as_posix())


# ---------------------------------------------------------------- --check

def expand(args, vars_, objects, extra=None):
    out = []
    for a in args:
        if a.startswith("@objects:"):
            out += objects[a[len("@objects:"):]]
        elif a.startswith("@"):
            out += [expand_text(x, vars_, extra) for x in vars_[a[1:]]]
        else:
            out.append(expand_text(a, vars_, extra))
    return out


def expand_text(s, vars_, extra=None):
    def sub(m):
        name = m.group(1)
        v = (extra or {}).get(name, vars_.get(name))
        if not isinstance(v, str):
            sys.exit(f"recipe: ${{{name}}} is {'unset' if v is None else 'a list'}")
        return expand_text(v, vars_, extra) if name not in (extra or {}) else v
    return re.sub(r"\$\{([A-Za-z_][A-Za-z0-9_]*)\}", sub, s)


def check(build, kit_dir, profile, r):
    """Links libgame.so on this PC with the recipe's own build-info and link steps."""
    with tempfile.TemporaryDirectory(prefix="daoprofile") as tmp:
        tmp = Path(tmp)
        work = tmp / "work"
        (work / "obj" / "game-code").mkdir(parents=True)
        (work / "obj" / "build-info").mkdir(parents=True)
        vars_ = {"KIT": kit_dir.as_posix(), "PROFILE": profile.as_posix(), "WORK": work.as_posix(),
                 "TOOLS": (build / "sdl").as_posix(), "GAME": "", "INSTANCE": tmp.as_posix(), "JOBS": "8"}
        vars_.update(r["vars"])
        # The game code: this PC's objects of the same units, the same flags.
        game = work / "obj" / "game-code"
        subprocess.run([exe("llvm-ar"), "x", "--output", str(game), str(build / "ppc" / "libdaoppc.a")], check=True)
        # ${WORK}/ppc is the phone's translated code; the build descriptor needs only its headers.
        (work / "ppc").mkdir()
        for h in (RECOMP / "ppc").glob("*.h"):
            shutil.copy2(h, work / "ppc" / h.name)
        steps = {s["id"]: s for s in r["steps"]}
        obj = work / "obj" / "build-info" / "build_info_android.o"
        args = expand(steps["build-info"]["args"], vars_, {},
                      {"IN": (profile / "include" / "build_info_android.cpp").as_posix(), "OBJ": obj.as_posix()})
        subprocess.run([exe("clang")] + args, check=True)
        objects = {"build-info": [obj.as_posix()],
                   "game-code": sorted(p.as_posix() for p in game.iterdir() if p.suffix == ".o")}
        link = expand(steps["link"]["args"], vars_, objects)
        rsp = tmp / "link.rsp"
        rsp.write_text("\n".join(f'"{a}"' for a in link[2:]), encoding="utf-8")
        result = subprocess.run([exe("ld.lld"), "@" + str(rsp)], capture_output=True, text=True)
        if result.returncode != 0:
            print(result.stdout + result.stderr)
            sys.exit("check: the link failed")
        lib = work / "libgame.so"
        notes = subprocess.run([exe("llvm-readelf"), "-n", str(lib)], capture_output=True, text=True).stdout
        if "XDroid360" not in notes:
            sys.exit("check: no XDroid360 note in the library")
        print(f"check: linked {lib.stat().st_size >> 20} MB, {len(objects['game-code'])} game units, ABI note present")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--build", default=str(ROOT / "build" / "tool" / "android"))
    ap.add_argument("--kit", default=str(DEFAULT_KIT))
    ap.add_argument("--out", default=str(ROOT / "build" / "profile"))
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()
    build, kit_dir, out = Path(args.build), Path(args.kit), Path(args.out)
    kit = json.loads((kit_dir / "kit.json").read_text(encoding="utf-8"))
    folder = out / "dao"
    r = stage(build, kit, folder)
    pack(folder, out / "dao.zip")
    print(f"{out / 'dao.zip'}: {(out / 'dao.zip').stat().st_size >> 20} MB")
    if args.check:
        check(build, kit_dir, folder, r)


if __name__ == "__main__":
    main()
