"""Checks out the sources the game profile's build (scripts/make_dao_profile.py) needs, at the
commits DAORecomp pins (recomp/installer/deps.py), as its installer does, plus the xenia-canary
parts only the Vulkan backend uses, then patches XenonRecomp (its ppc_context.h template goes
into the profile). For a clean checkout: GitHub's CI, or a new PC.

    python -I scripts/fetch_build_sources.py
"""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RECOMP = ROOT / "recomp"
sys.path.insert(0, str(RECOMP))

from installer import deps, sources  # noqa: E402  (DAORecomp's installer)
from installer.util import Reporter  # noqa: E402

# Not built for Android: AMD's FidelityFX SDK (Windows DLLs and headers).
SKIP = {"tools/FidelityFX-SDK"}
# What the Android build takes from xenia-canary beyond DAORecomp's sparse checkout: the Vulkan
# backend's sources and shaders, and the submodules it compiles with.
CANARY_PATHS = ("src/xenia/gpu", "src/xenia/ui", "src/xenia/base", "cmake", "third_party/CMakeLists.txt")
CANARY_SUBMODULES = ("third_party/Vulkan-Headers", "third_party/VulkanMemoryAllocator", "third_party/glslang",
                     "third_party/SPIRV-Tools", "third_party/FidelityFX-CAS", "third_party/FidelityFX-FSR")


def git(*args):
    subprocess.run(["git", *args], check=True)


def main():
    reporter = Reporter()
    for source in deps.GIT_SOURCES:
        if source.path not in SKIP:
            sources.ensure(reporter, source)
    canary = RECOMP / "tools" / "xenia-canary"
    git("-C", str(canary), "sparse-checkout", "add", *CANARY_PATHS)
    git("-C", str(canary), "submodule", "update", "--init", "--depth", "1", *CANARY_SUBMODULES)
    subprocess.run([sys.executable, "-I", str(RECOMP / "scripts" / "patch_recompiler.py"),
                    str(RECOMP / "tools" / "XenonRecomp" / "XenonRecomp" / "recompiler.cpp")], check=True)
    print("sources ready")


if __name__ == "__main__":
    main()
