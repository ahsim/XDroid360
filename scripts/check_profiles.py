"""Checks that the game profiles fit the toolchain kit the APK carries: the same kit version and
the same tool versions as each recipe requires (the phone refuses a profile that doesn't, with
"this launcher can't build this game"). The APK build runs it before packing them.

    python -I scripts/check_profiles.py <kit.json> <profile.zip>...
"""
import json
import sys
import zipfile


def main():
    kit = json.loads(open(sys.argv[1], encoding="utf-8").read())
    have = kit.get("tools", {})
    bad = False
    for path in sys.argv[2:]:
        recipe = json.loads(zipfile.ZipFile(path).read("recipe.json"))
        need = recipe.get("requires", {})
        problems = []
        if need.get("kit") != kit.get("kit"):
            problems.append(f"kit {need.get('kit')} (the kit is {kit.get('kit')})")
        for tool, version in need.get("tools", {}).items():
            if have.get(tool) != version:
                problems.append(f"{tool} {version} (the kit has {have.get(tool)})")
        name = recipe.get("version", path)
        if problems:
            bad = True
            print(f"{name}: needs " + ", ".join(problems))
        else:
            print(f"{name}: fits the kit")
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
