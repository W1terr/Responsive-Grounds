"""Checks the translations in res/SKSE/Plugins/ResponsiveGrounds/lang against the texts the menu shows.

The English texts are the keys: every string literal in Menu.cpp except ImGui ids and number formats, the kind names
(Settings.h kKindNames), the object materials and Northern Roads road kinds (Terrain.cpp) and "other" (an object
without a known material).
Reports keys missing from a language file, keys the code no longer uses and broken {} placeholders.
usage: python check_lang.py [--keys]   (--keys: print the keys as a json template)
"""
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LANG_DIR = ROOT / "res" / "SKSE" / "Plugins" / "ResponsiveGrounds" / "lang"
LANGUAGES = ["french", "german", "italian", "spanish", "polish", "russian", "japanese", "chinese"]
LIT = r'"((?:[^"\\]|\\.)*)"'

# literals in Menu.cpp that are not shown text
NOT_TEXT = {"Responsive Grounds", "materials", "textures", "roads", "add", "list", "x", "-", "", "auto", "npcs", "(?)",
            "SKSEMenuFramework.h", "Menu.h", "Lang.h", "Particles.h", "Seasons.h", "Settings.h", "Terrain.h", "RenameSection"}


def literals(text):
    text = re.sub(r"//[^\n]*", "", text)
    return [m.encode().decode("unicode_escape").encode("latin-1").decode("utf-8") for m in re.findall(LIT, text)]


def code_keys():
    found = []
    for lit in literals((ROOT / "src" / "Menu.cpp").read_text(encoding="utf-8")):
        if lit in NOT_TEXT or lit.startswith(("#", "%", "[%s]")) or lit in ("{}", "  {}", "{}/{}") or "###" in lit or lit.startswith("##") or "%s" in lit and len(lit) < 12:
            continue
        if lit.startswith("SKSE Menu Framework is not") or lit.startswith("SKSE Menu Framework {}"):
            continue  # log lines
        found.append(lit)
    names = re.search(r"kKindNames\{([^}]*)\}", (ROOT / "src" / "Settings.h").read_text(encoding="utf-8"))
    found += literals(names.group(1))
    terrain = (ROOT / "src" / "Terrain.cpp").read_text(encoding="utf-8")
    found += re.findall(r'\{ "([^"]+)", \{[^}]*\},\s*\{', terrain)                 # materials
    found += re.findall(r'\{ "[^"]+", (?:true|false), "([^"]+)"', terrain)       # road kinds
    found += ["Other road", "other"]
    keys = []
    for key in found:
        if key not in keys:
            keys.append(key)
    return keys


def main():
    keys = code_keys()
    if "--keys" in sys.argv:
        print(json.dumps({k: "" for k in keys}, ensure_ascii=False, indent="\t"))
        return
    ok = True
    for lang in LANGUAGES:
        path = LANG_DIR / f"{lang}.json"
        if not path.exists():
            print(f"{lang}: missing file")
            ok = False
            continue
        table = json.loads(path.read_text(encoding="utf-8"))
        missing = [k for k in keys if not table.get(k)]
        unused = [k for k in table if k not in keys]
        broken = [k for k, v in table.items() if v and sorted(re.findall(r"\{[^}]*\}", k)) != sorted(re.findall(r"\{[^}]*\}", v))]
        for what, items in (("missing", missing), ("unused", unused), ("broken placeholders", broken)):
            for item in items:
                print(f"{lang}: {what}: {item}")
                ok = False
    print(f"{len(keys)} keys, {'all languages complete' if ok else 'problems above'}")


if __name__ == "__main__":
    main()
