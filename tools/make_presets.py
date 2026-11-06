"""Make the ready-made settings files that ship with the mod (release package).

    python tools/make_presets.py [out_folder]      (default: release\\FF7RCoopMod_Profiles)

Each file is a COMPLETE FF7RCoopMod.ini - the mod's own default file (settings/03a DEFAULT_INI, so it always
matches the build) with the [Balance] / [Balance1-3] values of one of the settings app's difficulty presets
(FF7RCoopMod_Settings.pyw DIFFICULTY_BUILTIN) - plus a [Profile] section (Name / Description / ReadOnly=1).
So one file works two ways (the author 2026-10-21):
  - without the settings app: copy it next to the DLL and rename it to FF7RCoopMod.ini (the mod ignores [Profile])
  - with the settings app: it is listed under Profiles (FF7RCoopMod_Profiles\\ next to FF7RCoopMod.ini); loading it
    never touches the PC's controller choices ([Input2] / [Pad.*] are skipped by the app).
Re-run this for every release so the files carry that build's default settings text.
"""
import ast, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SETTINGS_INC = os.path.join(ROOT, "settings", "03a_default_ini.inc")   # (f1.192: DEFAULT_INI moved here from settings/03)
APP = os.path.join(ROOT, "FF7RCoopMod_Settings.pyw")

FILE_NAMES = {   # preset name -> file name (sorted in this order in a folder listing)
    "Vanilla": "Difficulty 1 - Vanilla.ini",   # f1.157: was "Default"
    "Co-op Balanced": "Difficulty 2 - Co-op Balanced.ini",
    "Co-op Hard": "Difficulty 3 - Co-op Hard.ini",
    "Co-op Extreme": "Difficulty 4 - Co-op Extreme.ini",
}


def default_ini():
    """The text the mod writes when FF7RCoopMod.ini is missing (C string literals of DEFAULT_INI, joined)."""
    src = open(SETTINGS_INC, encoding="latin-1").read()
    m = re.search(r"static const char\* DEFAULT_INI =\s*\n(.*?)\n;\s*\n", src, re.S)
    if not m:
        sys.exit("DEFAULT_INI not found in " + SETTINGS_INC)
    macros = {}   # f1.151: #define NAME "..." \ lines (DEFAULT_INI_PLAYER_COMMON, used for players 2 and 3) - this broke the script
    for mm in re.finditer(r"#define (\w+) \\\n((?:.*\\\n)*.*)", src):
        macros[mm.group(1)] = "".join(ast.literal_eval(l.strip().rstrip("\\").strip()) for l in mm.group(2).splitlines() if l.strip().rstrip("\\").strip())
    out = []
    for line in m.group(1).splitlines():
        line = line.strip()
        if not line or line.startswith("//"):
            continue
        out.append(macros[line] if line in macros else ast.literal_eval(line))   # C escapes used here (\r \n \") are the same in Python
    return "".join(out)


def difficulty_presets():
    """DIFFICULTY_BUILTIN from the settings app, without importing it (no tkinter needed)."""
    tree = ast.parse(open(APP, encoding="utf-8").read())
    keep = [n for n in tree.body if (isinstance(n, ast.FunctionDef) and n.name == "_diff")
            or (isinstance(n, ast.Assign) and any(getattr(t, "id", "") in ("DIFFICULTY_BUILTIN", "_NORMAL") for t in n.targets))]   # f1.151: + _NORMAL
    ns = {}
    exec(compile(ast.Module(body=keep, type_ignores=[]), APP, "exec"), ns)
    return ns["DIFFICULTY_BUILTIN"]


def set_key(lines, section, key, value):
    """Set section/key in the CRLF-split lines; a missing key goes before the next Player*Rates / at the section's end."""
    start = next((i for i, l in enumerate(lines) if l.strip().lower() == "[%s]" % section.lower()), None)
    if start is None:   # f1.272: [BalanceGlobal] isn't in the default file - added at the end (the mod reads it anywhere)
        while lines and not lines[-1].strip():
            lines.pop()
        lines += ["", "[%s]" % section]
        if section == "BalanceGlobal":
            lines.insert(len(lines) - 1, "; ---- f1.272: the enemy / command menu settings used for ANY number of people when [Balance] DynamicDifficulty=0 ----")
        start = len(lines) - 1
    end = next((i for i in range(start + 1, len(lines)) if lines[i].startswith("[")), len(lines))
    for i in range(start + 1, end):
        if lines[i].split("=", 1)[0].strip().lower() == key.lower() and not lines[i].lstrip().startswith(";"):
            lines[i] = "%s=%s" % (key, value)
            return
    at = end
    while at > start + 1 and not lines[at - 1].strip():
        at -= 1
    if key == "Player1Rates":   # keep the three together
        at = next((i for i in range(start + 1, end) if lines[i].startswith("Player2Rates=")), at)
    lines.insert(at, "%s=%s" % (key, value))


def main():
    out_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "release", "FF7RCoopMod_Profiles")
    os.makedirs(out_dir, exist_ok=True)
    base = default_ini()
    for name, (desc, values) in difficulty_presets().items():
        lines = base.split("\r\n")
        for (sec, k), v in values.items():   # f1.151: (section, key) - [Balance] + [Balance1] / [Balance2] / [Balance3] (+ f1.272 [BalanceGlobal])
            set_key(lines, sec, k, v)
        one_line = " ".join(desc.split())
        head = [
            "; =====================================================================",
            "; READY-MADE SETTINGS: %s difficulty (every other setting = the mod's default)" % name,
            "; To use it without the settings app: copy this file next to xinput1_3.dll (End\\Binaries\\Win64),",
            ";   delete or rename the old FF7RCoopMod.ini there, and rename this file to FF7RCoopMod.ini.",
            "; With the settings app: leave it in FF7RCoopMod_Profiles and pick it under Profiles.",
            ";",
        ]
        tail = [   # at the end, so no setting above it can fall into this section (the mod ignores [Profile])
            "",
            "[Profile]",
            "; Shown by the settings app's Profiles list (ReadOnly=1: the app can't overwrite or delete it).",
            "Name=%s" % name,
            "Description=%s Every other setting goes back to the mod's default (your controller choices stay)." % one_line,
            "ReadOnly=1",
            "",
        ]
        path = os.path.join(out_dir, FILE_NAMES.get(name, name + ".ini"))
        with open(path, "wb") as f:
            while lines and not lines[-1].strip():
                lines.pop()
            f.write("\r\n".join(head + lines + tail).encode("latin-1"))
        print("wrote", path)


if __name__ == "__main__":
    main()
