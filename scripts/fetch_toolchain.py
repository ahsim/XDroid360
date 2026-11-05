"""Downloads what the APK takes from other repositories' releases (android/toolchain.lock.json):
the on-phone toolchain (clang, lld, XenonRecomp, the STFS extractor), the kit, and State of
Decay's profile. Each asset is checked against the SHA-256 pinned in the lock file, not only the
release's own list. The result has the layout of SD1/toolchain/out, which the app's Gradle build
takes with -Pxdroid360.toolchain=<out>:

    out/jniLibs/arm64-v8a/lib*.so   out/kit.zip   out/kit/kit.json   out/games/<id>.zip

    python -I scripts/fetch_toolchain.py --out build/toolchain
"""
import argparse
import hashlib
import json
import shutil
import sys
import urllib.request
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LOCK = ROOT / "android" / "toolchain.lock.json"


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def download(url, dest):
    part = dest.with_name(dest.name + ".part")
    request = urllib.request.Request(url, headers={"User-Agent": "xdroid360-ci"})
    with urllib.request.urlopen(request, timeout=120) as r, open(part, "wb") as f:
        shutil.copyfileobj(r, f, 1 << 20)
    part.replace(dest)


def safe_extract(zf, folder):
    root = folder.resolve()
    for name in zf.namelist():
        target = (folder / name).resolve()
        if root not in target.parents and target != root:
            sys.exit(f"bad entry in the zip: {name}")
    zf.extractall(folder)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=str(ROOT / "build" / "toolchain"))
    ap.add_argument("--cache", default=str(ROOT / "build" / "toolchain-downloads"))
    ap.add_argument("--lock", default=str(LOCK))
    args = ap.parse_args()
    out, cache = Path(args.out), Path(args.cache)
    cache.mkdir(parents=True, exist_ok=True)
    lock = json.loads(Path(args.lock).read_text(encoding="utf-8"))
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    for a in lock["assets"]:
        if not a.get("sha256"):
            sys.exit(f"{Path(args.lock).name}: no sha256 pinned for {a['asset']} yet")
        file = cache / a["asset"]
        if not (file.is_file() and sha256(file) == a["sha256"]):
            url = f"https://github.com/{a['repo']}/releases/download/{a['tag']}/{a['asset']}"
            print(f"download {url}")
            download(url, file)
        got = sha256(file)
        if got != a["sha256"]:
            sys.exit(f"{a['asset']}: SHA-256 {got}, the lock file pins {a['sha256']}")
        if a.get("unpack") == "jniLibs":
            # The zip holds lib/arm64-v8a/*.so: the app's jniLibs layout is <abi>/*.so.
            with zipfile.ZipFile(file) as z:
                safe_extract(z, cache / "jnilibs")
            shutil.copytree(cache / "jnilibs" / "lib", out / "jniLibs", dirs_exist_ok=True)
        else:
            target = out / a["as"]
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(file, target)
            if a["as"] == "kit.zip":
                # make_dao_profile.py reads the kit's versions from kit/kit.json.
                (out / "kit").mkdir(exist_ok=True)
                with zipfile.ZipFile(file) as z:
                    (out / "kit" / "kit.json").write_bytes(z.read("kit.json"))
        print(f"ok {a['asset']}")
    print(f"toolchain in {out}")


if __name__ == "__main__":
    main()
