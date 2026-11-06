"""FF7R co-op mod - settings window.

Double-click this file to open it. It edits FF7RCoopMod.ini, which
sits next to the game's exe (End\\Binaries\\Win64). Save, then press the reload key (F6) in game;
settings marked * only change when the game is restarted.

Needs Python 3 for Windows (python.org - the normal installer includes everything this uses).
"""
import os, sys, time, shutil, ctypes, re, stat
try:
    import winreg
except ImportError:   # not Windows (testing)
    winreg = None
import tkinter as tk
from tkinter import ttk, messagebox, filedialog, simpledialog

APP_TITLE = "FF7R Co-op Mod - Settings  (by Christian Budd)"   # f1.158: credit (the author: "Christian Budd is fine")
APP_VERSION = "1.0 beta (app f1.337)"   # f1.337: the Reload button is gone (the author); f1.333: code review - the settings file keeps its encoding (UTF-16 / UTF-8 / code page), safe save (temporary file + swap, backup without the read-only flag), notes after values kept, every number box checked (+ whole numbers where the mod reads them), metre boxes accept "2,6" / "2.6 m" and report bad input, no empty file on a fresh Save, a warning when the mod isn't next to the file, the game folder found through Steam (no fixed path), Windows device names as profile names, profiles / presets saved as UTF-8, three character ATB default 85 (Co-op Balanced), thirds texts mention Multiple monitors; f1.332: window camera distance default 2.6 m (was 2.7 - the author's own value); f1.331: "Each player sees" default = their own character + AI-played teammates (the author); f1.330: defaults - Individual health / ATB panels (each player sees their own), windows on Ultra (sharpness 100, detail 1) (the author); f1.327: "Allow Thirds on a normal monitor" (ThirdsLayout) moved to the Troubleshooting tab (the author); Experimental notes updated (the menus' bend is fixed); f1.311: split screen Experimental box - Multiple monitors (MultiMonitor / MonitorPlayer1 / MonitorShare); 1.0 beta (app f1.284): the release name - shipped as FF7RCoopMod_Settings.exe (tools/build_exe.sh); f1.281c: every built-in difficulty preset sets "Dynamic difficulty" ([Balance] DynamicDifficulty=1 - the author: "They should ALL default to dynamic difficulty"); a preset applied while "Same difficulty" is picked switches it back to Dynamic; "Save as preset" stores the choice too; f1.281b: the Co-op Balanced preset = the author's settings: three character party ATB 85 % (was 90); f1.280b: Balance tab - "One row per player - only the two people who are playing count" under the ATB / command menu boxes of the two / three character and Global sets (the author asked why the two character party has a 3rd row); f1.276: the one character party has player 1's ATB speed too (default 100; Hard 85 / Extreme 80 presets); f1.275: players' ATB speed is part of each party-size set / the Global set; presets set it per set; f1.274e: "Player N: darken their view when menu is open" (the author - "(split screen)" dropped); f1.274d: "Player N's menu slowdown speed (%)" labels (the author); f1.274c: Balance tab - the Same / Dynamic difficulty choice is a drop-down (the author); f1.274b: window cameras - "Player 2 and Player 3's cameras use the same values" (the author's wording); ticked = player 3's box closed and grayed out, player 2's box named for both; f1.274: Troubleshooting "Apply enemy setting changes immediately" ([General] EnemyChangesApplyNow, testing); the Experimental tab is gone - its Thirds settings are a closed "Experimental" + / - box on the Split screen tab (the author); f1.273: the Global box's numbers are checked on Save like the others (code review #64), the box name in the message; f1.272b: "(recommended)" on "Dynamic difficulty based on human player count" (the author); f1.272: Balance tab - "Same difficulty no matter how many humans are playing" (one Global box, [BalanceGlobal], DynamicDifficulty=0) or "Dynamic difficulty based on human player count" (the three boxes); the command menus' slowdown and darkening moved into each box (per set; a missing key = the old shared one); presets set the slowdown per set and fill the Global set with the two character values; the per-set "Dynamic difficulty" tick box is gone (always 1); f1.271: Balance tab - the one / two / three character party settings are three boxes, each with a + / - button, all open (was a drop-down showing one at a time - the author); their note: settings go up mid-battle, never down; f1.265: the Co-op Hard preset = the author's ini again (one character 110 / 90 / 110 / 110, attack rate 120 / 130 for two / three); f1.263: the Co-op Hard preset = the author's own values (one character 150 / 85 / 130 / 150); f1.238: version only; f1.237: version only; f1.236: version only; f1.235: version only; f1.234: version only; f1.233: version only; f1.232: version only; f1.231: version only; f1.230: version only; f1.229: version only; f1.228: version only; f1.227: the EnemyHP tooltip (bosses take less damage instead); f1.226: version only; f1.225: the BossSpreadNearest tooltip (now only a fallback); f1.224: Troubleshooting "Bosses that chase the nearest player spread out too" (BossSpreadNearest, testing); f1.223: version only; f1.222: the Co-op Hard preset has no menu slowdown (0 for each player, was 50); f1.221 / f1.220 / f1.219 / f1.218 / f1.217 / f1.216 / f1.215: version only; f1.214: "Bosses spread their attention" (BossTargetSpread) removed - bosses follow the enemy settings; f1.213: Troubleshooting "Bosses treat AI teammates like players" (testing); f1.208 (the author): Balance tab - "Command menus: slowdown and darkening": each player's menu slowdown with their own "darken their view (split screen)" box under it ([General] MenuDarkenSplitPlayer1 / 2 / 3), then "Shared screen: darken when anyone's menu is open" (MenuDarken, moved from Screen effects); f1.200 (tooltip review, the author): round 2 - Claude's suggestions applied (the author: "go ahead and apply them"): repeated first lines and duplicate notes gone, clearer camera / key labels, window camera distance / height in meters, no button-icons row on Player 1's tab; the detailed log always on - its check box removed, the Log box just says where the log is; the author's typed wording from the review doc (rows 5, 16, 18 - 22, 24, 27, 28, 31, 57, 62, 63, 68, 69); "Only while that many people play" -> "Dynamic difficulty based on human player count" with a clearer tooltip, the note under it removed; "Go after weakened players" / "Weakened below" -> "Target weakened players (chance)" / "...weakened = below this % health"; "Extra look-down" (ExtraTilt) removed - always 5 degrees; "...walls and scenery only" (WallsOnly) removed - always on; "...if it zooms out at most" -> "Extra zoom out for nearby enemies"; "Screen space used" -> "Screen fill before zooming out" with a clearer tooltip; "Extra distance back" -> "Distance back (m)" - now the plain distance from player 1 (1 - 30 m, default 8 m = the game's usual 5 m + the old 3 m; presets Close 5.5 m, Wide 9 m); "Aim" -> "Camera aim" with a clearer tooltip; the split screen "When the game starts" tooltip shortened; Battle messages - the author's tooltips, the note under the box removed; "Swing-back speed" -> "Auto return speed"; DynamicTargetFps default 55 (was 60) + the "higher than the game's setting = off" note in its tooltip; "Swing back after (s)" -> "Auto return (s)" with the author's tooltip; "Move relative to the window" (WindowControlsFollowView) removed - always on; the allowed range beside each number box again (gray), not in the tooltip; split screen key "Split screen is activated when:" (Never / Battle only / Always tooltip), "Change split screen layout"; "Keep player 1 centred by" (SplitAim Turn / Slide) and the window camera "Direction" (WindowView Behind / Main) removed - always Turn / Behind; "When the game starts" (In / Out / Auto) -> Auto / Off "Join P2 / P3 when the game starts", default Auto; the join-button hold time (JoinHoldSeconds) removed - always 1 second; "P2 / P3 badges" -> "P2 / P3 joined status indicator" with the author's tooltip; "gray" spelling; "P2 / P3 reticle color" (Accents / Whole) removed - always the accents; "color" spelling everywhere; "Co-op keys reminder on join" (ShowTips) and "P2 / P3 command HUD in their window" (OwnCommandHud) removed - the reminder is gone, the command HUD always on; "P2 / P3 target markers" -> "target reticles"; the keyboard hot swap row reworded; f1.199: "No hot swap for a keyboard leader" on by default and ticked automatically when "Player 1 uses a controller" is unticked (the author); f1.192: tooltip rows #5 / #16 - "A knocked-out leader still hands over either way" dropped (wrong since f1.126; the author: "Yes"); f1.191: Split screen tab - "KO: keep P1's view on P1" (KOCameraOnPlayer1) removed with the old KO hand-off (review row #43); f1.189: the split screen's "When the game starts" row gets its own tooltip (it showed the joining one - review row #64); f1.188: General > On screen - "...showing their own character" (OwnHudCharacter) removed, always on (the author); f1.187: tooltips without the repeated "- click Set... and press the new key" / "Save, then F6 in game" endings (the author); f1.178: General tab > On screen - GameReticleTint (Accents / Whole); the drawn-HUD options (EnemyHPNumbers, EnemyNameSize) gone - players 2 / 3 always get the game's own numbers / enemy bar / reticle; f1.162: Battle messages at the right edge (MessageLogX default 0); f1.161: Split screen tab > Battle messages (MessageLogX / MessageLogY); f1.158: title credit; f1.157: defaults - split screen on (battle only), Co-op Balanced (2 / 3 people; 1 person = the normal game), preset "Default" -> "Vanilla"; f1.156: players 2 and 3 can't pick the same color; f1.155: hover tooltips - short labels on screen, the full explanation / default / range in a tooltip; long gray notes -> one line + tooltip; f1.152: Troubleshooting > Log - Detailed log check box; f1.151b: note on the Balance tab - enemy settings never change during a battle (F6 too); f1.151: enemy settings per party size ([Balance1] / [Balance2] / [Balance3], Balance tab drop-down; presets fill all three); f1.149: EnemyNameSize (Balance > Enemy info); f1.148: size defaults 90 / 85 / 85 (panel + command HUD), ShowFPS on the Troubleshooting tab; f1.146: Thirds nudges (Experimental > Split screen); f1.145: Command HUD / menus size per view size (Split screen tab); f1.144: Dynamic quality guardrail note (off when the game's frame rate limit is below the target); f1.143: QuietPageSetup (Experimental > Command menus) removed; f1.142: Panel size per view size (half / third / quarter); f1.141: gap fix always on - PartyPanelCloseGaps box + known-issue note removed; f1.140: PartyPanelCloseGaps Off / Try 1 / Try 2; f1.139: Experimental > Health / ATB panel: PartyPanelCloseGaps; f1.138b: known-issue note (Individual + own + AI teammates: a gap where a middle row is hidden); f1.138: no Blue color; f1.136: [PlayerN] Color (player tabs); f1.135: Panel size (PartyPanelScale); f1.133: Health / ATB panel rows - Whole party / Own + AI teammates / Only their own; f1.125: Bias default 0.4, Knock-outs box removed (always on); f1.124: KoKeepLeader (General > Knock-outs), ScreenMargin 0.6 / ExtraTilt 5 defaults; f1.123: [Camera] WallsOnly / ExtraTilt (Fine-tune); f1.121: TalkScenes text (+ cinematic shots); f1.120: [Camera] TalkScenes (Co-op camera tab); f1.118: ShowClock (Troubleshooting tab); f1.117b: dynamic-quality target note; f1.117: Health / ATB panel (Split screen tab); f1.116: OwnCommandHud (General tab); f1.113: Dynamic quality (split-screen tab); f1.111: the author's co-op camera values are the defaults; f1.108: EnemyHPNumbers; f1.106: DangerPerScreen / DangerHealth; f1.105: MenuDarken; f1.104: Troubleshooting tab (WindowsForAI); settings app version (f1.98 = 2026-10-22 review fixes E3 / E7 / E13 + dead code; works with f1.89 and later)


def installed_mod_version(ini_path):
    """Version of the mod DLL next to the settings file (xinput1_3.dll), or None."""
    try:
        with open(os.path.join(os.path.dirname(ini_path), "xinput1_3.dll"), "rb") as f:
            data = f.read()
        i = data.find(b"FF7RCOOPMOD_VERSION=")
        if i < 0:
            return "older than 10.0"
        j = data.find(b";", i)
        return data[i + 20:j].decode("ascii", "replace") if j > i else None
    except OSError:
        return None
INI_NAME = "FF7RCoopMod.ini"

# --------------------------------------------------------------------------------------------
# Controller reading (XInput, the same API the mod uses)
# --------------------------------------------------------------------------------------------
PAD_BITS = [  # name used in the ini, XInput bit (LT/RT are the triggers)
    ("DPadUp", 0x0001), ("DPadDown", 0x0002), ("DPadLeft", 0x0004), ("DPadRight", 0x0008),
    ("Start", 0x0010), ("Back", 0x0020), ("L3", 0x0040), ("R3", 0x0080),
    ("LB", 0x0100), ("RB", 0x0200), ("A", 0x1000), ("B", 0x2000), ("X", 0x4000), ("Y", 0x8000),
    ("LT", 0x10000), ("RT", 0x20000),
]
PAD_NAMES = {n.lower(): n for n, _ in PAD_BITS}


class _Gamepad(ctypes.Structure):
    _fields_ = [("wButtons", ctypes.c_ushort), ("bLeftTrigger", ctypes.c_ubyte), ("bRightTrigger", ctypes.c_ubyte),
                ("sThumbLX", ctypes.c_short), ("sThumbLY", ctypes.c_short), ("sThumbRX", ctypes.c_short), ("sThumbRY", ctypes.c_short)]


class _State(ctypes.Structure):
    _fields_ = [("dwPacketNumber", ctypes.c_uint), ("Gamepad", _Gamepad)]


class _CapsEx(ctypes.Structure):   # XINPUT_CAPABILITIES_EX (xinput1_4 ordinal 108, undocumented but stable)
    _fields_ = [("Type", ctypes.c_ubyte), ("SubType", ctypes.c_ubyte), ("Flags", ctypes.c_ushort), ("Gamepad", _Gamepad),
                ("wLeftMotor", ctypes.c_ushort), ("wRightMotor", ctypes.c_ushort),
                ("VendorId", ctypes.c_ushort), ("ProductId", ctypes.c_ushort), ("ProductVersion", ctypes.c_ushort),
                ("Unk1", ctypes.c_ushort), ("Unk2", ctypes.c_uint)]


KNOWN_PADS = {  # (vendor, product) -> name, used when Windows has no name for it
    (0x045E, 0x028E): "Xbox 360 Controller", (0x045E, 0x028F): "Xbox 360 Wireless Controller",
    (0x045E, 0x0719): "Xbox 360 Wireless Controller", (0x045E, 0x02D1): "Xbox One Controller",
    (0x045E, 0x02DD): "Xbox One Controller", (0x045E, 0x02EA): "Xbox One S Controller",
    (0x045E, 0x02FD): "Xbox One S Controller (Bluetooth)", (0x045E, 0x0B12): "Xbox Series X|S Controller",
    (0x045E, 0x0B13): "Xbox Series X|S Controller (Bluetooth)", (0x045E, 0x02E3): "Xbox Elite Controller",
    (0x045E, 0x0B00): "Xbox Elite Controller Series 2", (0x046D, 0xC21D): "Logitech Gamepad F310",
    (0x046D, 0xC21E): "Logitech Gamepad F510", (0x046D, 0xC21F): "Logitech Gamepad F710",
    (0x054C, 0x05C4): "PlayStation 4 Controller", (0x054C, 0x09CC): "PlayStation 4 Controller",
    (0x054C, 0x0CE6): "PlayStation 5 Controller", (0x057E, 0x2009): "Nintendo Switch Pro Controller",
    (0x0E6F, 0x02A1): "PDP Xbox Controller", (0x24C6, 0x581A): "PowerA Xbox Controller",
}


class XInput:
    def __init__(self):
        self.dll = None
        self.caps_ex = None   # v11.9: controller model per slot
        self.fake = os.environ.get("FF7R_FAKE_PADS")  # testing only, e.g. "0,2"
        if sys.platform == "win32":
            for name in ("xinput1_4", "xinput9_1_0", "xinput1_3"):
                try:
                    self.dll = getattr(ctypes.windll, name)
                    break
                except OSError:
                    continue
            try:
                x14 = ctypes.WinDLL("xinput1_4")
                f = x14[108]   # XInputGetCapabilitiesEx
                f.argtypes = [ctypes.c_uint, ctypes.c_uint, ctypes.c_uint, ctypes.POINTER(_CapsEx)]
                f.restype = ctypes.c_uint
                self.caps_ex = f
            except (OSError, AttributeError):
                self.caps_ex = None

    def vidpid(self, slot):
        """f1.11: (vendor, product) of the controller in that slot, or None."""
        if not self.caps_ex:
            return None
        c = _CapsEx()
        try:
            if self.caps_ex(1, slot, 0, ctypes.byref(c)) != 0 or not c.VendorId:
                return None
        except OSError:
            return None
        return (c.VendorId, c.ProductId)

    @property
    def available(self):
        return self.dll is not None or self.fake is not None

    def read(self, slot):
        """Returns (connected, button_mask)."""
        if self.fake is not None:
            ok = str(slot) in self.fake.split(",")
            return ok, 0
        if not self.dll:
            return False, 0
        st = _State()
        if self.dll.XInputGetState(slot, ctypes.byref(st)) != 0:
            return False, 0
        g = st.Gamepad
        mask = g.wButtons
        if g.bLeftTrigger > 100:
            mask |= 0x10000
        if g.bRightTrigger > 100:
            mask |= 0x20000
        return True, mask


# DirectInput / other controllers (v8.2) - read through Windows' joystick API (winmm) for the
# live button display and "Learn". Button numbers are the ones Windows shows (1 = first).
class _JoyInfoEx(ctypes.Structure):
    _fields_ = [(n, ctypes.c_uint) for n in ("dwSize", "dwFlags", "dwXpos", "dwYpos", "dwZpos", "dwRpos", "dwUpos",
                                              "dwVpos", "dwButtons", "dwButtonNumber", "dwPOV", "dwReserved1", "dwReserved2")]


class _JoyCapsW(ctypes.Structure):   # v13.4
    _fields_ = [("wMid", ctypes.c_ushort), ("wPid", ctypes.c_ushort), ("szPname", ctypes.c_wchar * 32)] + \
               [(n, ctypes.c_uint) for n in ("wXmin", "wXmax", "wYmin", "wYmax", "wZmin", "wZmax", "wNumButtons", "wPeriodMin",
                                              "wPeriodMax", "wRmin", "wRmax", "wUmin", "wUmax", "wVmin", "wVmax", "wCaps",
                                              "wMaxAxes", "wNumAxes", "wMaxButtons")] + \
               [("szRegKey", ctypes.c_wchar * 32), ("szOEMVxD", ctypes.c_wchar * 260)]


class Joysticks:
    def __init__(self):
        self.dll = None
        if sys.platform == "win32":
            try:
                self.dll = ctypes.windll.winmm
            except OSError:
                self.dll = None

    def held(self, jid):
        """Set of button numbers (1-32) held on Windows joystick 'jid' (f1.98: the unused "any of the 16 joysticks" path removed)."""
        out = set()
        if not self.dll:
            return out
        ji = _JoyInfoEx()
        ji.dwSize = ctypes.sizeof(ji)
        ji.dwFlags = 0x80  # JOY_RETURNBUTTONS
        if self.dll.joyGetPosEx(jid, ctypes.byref(ji)) == 0:
            for b in range(32):
                if ji.dwButtons & (1 << b):
                    out.add(b + 1)
        return out

    def pov(self, jid):
        """f1.11: D-pad of joystick 'jid' as XInput names (set)."""
        out = set()
        if not self.dll:
            return out
        ji = _JoyInfoEx(); ji.dwSize = ctypes.sizeof(ji); ji.dwFlags = 0x40   # JOY_RETURNPOV
        if self.dll.joyGetPosEx(jid, ctypes.byref(ji)) != 0 or ji.dwPOV == 0xFFFF:
            return out
        a = ji.dwPOV % 36000
        if a >= 31500 or a <= 4500: out.add("DPadUp")
        if 4500 <= a <= 13500: out.add("DPadRight")
        if 13500 <= a <= 22500: out.add("DPadDown")
        if 22500 <= a <= 31500: out.add("DPadLeft")
        return out

    def scan(self, xi=None):
        """f1.11: the game controllers plugged in right now, in Windows' order: a list of dicts with
        jid, vid, pid, name, value ("VID:PID", or "VID:PID#n" for the 2nd, 3rd ... of the same model), kind
        ("Xbox" = read through XInput, "PlayStation", "other"), buttons, axes, gamepad (not pedals / a wheel ...)."""
        out = []
        fake = os.environ.get("FF7R_FAKE_LIST")   # testing only: "054C:0CE6=DualSense,0D22:0C34=msi GC30 V2"
        if fake:
            for jid, item in enumerate(fake.split(",")):
                vp, nm = item.split("=")
                v, p_ = (int(x, 16) for x in vp.split(":"))
                out.append({"jid": jid, "vid": v, "pid": p_, "name": nm, "buttons": 12, "axes": 4})
        elif self.dll:
            for jid in range(16):
                ji = _JoyInfoEx(); ji.dwSize = ctypes.sizeof(ji); ji.dwFlags = 0xFF
                if self.dll.joyGetPosEx(jid, ctypes.byref(ji)) != 0:
                    continue
                caps = _JoyCapsW()
                if self.dll.joyGetDevCapsW(jid, ctypes.byref(caps), ctypes.sizeof(caps)) != 0:
                    continue
                name = None
                if winreg:
                    sub = r"System\CurrentControlSet\Control\MediaProperties\PrivateProperties\Joystick\OEM\VID_%04X&PID_%04X" % (caps.wMid, caps.wPid)
                    for root in (winreg.HKEY_CURRENT_USER, winreg.HKEY_LOCAL_MACHINE):
                        try:
                            with winreg.OpenKey(root, sub) as k:
                                name = winreg.QueryValueEx(k, "OEMName")[0]
                                break
                        except OSError:
                            continue
                name = name or KNOWN_PADS.get((caps.wMid, caps.wPid)) or caps.szPname or "controller"
                out.append({"jid": jid, "vid": caps.wMid, "pid": caps.wPid, "name": name,
                            "buttons": caps.wNumButtons, "axes": caps.wNumAxes})
        xpads = set()
        if xi is not None:
            for i in range(4):
                if xi.read(i)[0]:
                    vp = xi.vidpid(i)
                    if vp:
                        xpads.add(vp)
        seen = {}
        for d in out:
            key = (d["vid"], d["pid"])
            seen[key] = seen.get(key, 0) + 1
            d["nth"] = seen[key]
            d["kind"] = "PlayStation" if d["vid"] == 0x054C else \
                        "Xbox" if key in xpads or d["vid"] == 0x045E or xbox_type_name(d["name"]) else "other"
            d["gamepad"] = d["kind"] != "other" or (gamepad_name(d["name"]) and d["buttons"] >= 8 and d["axes"] >= 2)
        for d in out:
            d["value"] = "%04X:%04X" % (d["vid"], d["pid"]) + ("#%d" % d["nth"] if seen[(d["vid"], d["pid"])] > 1 else "")
        return out


DI_AXIS_NAMES = ["X", "Y", "Z", "Rx", "Ry", "Rz", "Slider1", "Slider2"]
# f1.11: which controller each player uses = [Input2] Seat1-3 (Keyboard / None / Auto / VID:PID / VID:PID#n)
SEAT_KEYS = {"Player1": "Seat1", "Player2": "Seat2", "Player3": "Seat3"}
AUTO_TEXT = "Auto - the first free gamepad"
KIND_TEXT = {"Xbox": "Xbox-type (XInput)", "PlayStation": "PlayStation", "other": "other controller"}


def gamepad_name(name):
    """v13.2: a DirectInput device worth offering as a player's controller (not pedals / wheels / shifters)."""
    n = name.strip().lower()
    return bool(n) and not any(w in n for w in ("pedal", "wheel", "shift", "gear", "throttle", "rudder", "handbrake", "button box", "stick", "hotas"))


def xbox_type_name(name):
    """v12.1: a DirectInput name that is really an XInput pad (Xbox, Logitech F310 ...)."""
    n = name.strip().lower()
    return n.startswith("controller (") or any(w in n for w in ("xbox", "xinput", "f310", "f710", "f510"))


class JoyCapture(tk.Toplevel):
    """Waits for a button press on any non-XInput-style controller. result = button number (str) or None."""
    TIMEOUT = 10.0

    def __init__(self, master, joy, what, jid):
        super().__init__(master)
        self.joy, self.result, self.jid = joy, None, jid
        self.title("Press a button")
        self.resizable(False, False)
        self.transient(master)
        ttk.Label(self, text="Press the button for:", padding=(20, 15, 20, 0)).pack()
        ttk.Label(self, text=what, font=("Segoe UI", 13, "bold"), padding=(20, 4)).pack()
        ttk.Label(self, text="on the controller you're setting up   (Esc or Cancel to stop)", padding=(20, 4)).pack()
        ttk.Button(self, text="Cancel", command=self.destroy).pack(pady=(4, 15))
        self.bind("<Escape>", lambda e: self.destroy())
        self.base = joy.held(jid)
        self.start = time.time()
        self.grab_set()
        self.after(30, self.poll)

    def poll(self):
        if not self.winfo_exists():
            return
        now = self.joy.held(self.jid)
        new = sorted(now - self.base)
        self.base &= now
        if new:
            self.result = str(new[0])
            self.destroy()
            return
        if time.time() - self.start > self.TIMEOUT:
            self.destroy()
            return
        self.after(30, self.poll)


# f1.11: button maps per controller model ([Pad.VID:PID] in the ini - the mod's input2/map.h)
MAP_KEYS = [("A", "Cross / A"), ("B", "Circle / B"), ("X", "Square / X"), ("Y", "Triangle / Y"), ("LB", "L1 / LB"),
            ("RB", "R1 / RB"), ("LT", "L2 / LT"), ("RT", "R2 / RT"), ("Back", "Share / Back"), ("Start", "Options / Start"),
            ("LS", "L3 (left stick press)"), ("RS", "R3 (right stick press)")]
MAP_XNAMES = ["A", "B", "X", "Y", "LB", "RB", "LT", "RT", "Back", "Start", "L3", "R3"]   # the Buttons section's names
PS_NUMBERS = ["2", "3", "1", "4", "5", "6", "7", "8", "9", "10", "11", "12"]          # Windows numbering of PlayStation pads
MAP_AXES = [("LeftX", "Left stick left/right", "X"), ("LeftY", "Left stick up/down", "Y"),
            ("RightX", "Right stick left/right", "Z"), ("RightY", "Right stick up/down", "Rz")]


class JoyPadCapture(tk.Toplevel):
    """f1.11: waits for a press on ONE non-XInput controller (Windows joystick id) and gives its name in the Buttons
    section's terms (A, LB, DPadUp ...), translated through that controller's button map. result = name or None."""
    TIMEOUT = 10.0

    def __init__(self, master, joy, jid, numbers, what):
        super().__init__(master)
        self.joy, self.jid, self.numbers, self.result = joy, jid, numbers, None
        self.title("Press a button")
        self.resizable(False, False)
        self.transient(master)
        ttk.Label(self, text="Press the button for:", padding=(20, 15, 20, 0)).pack()
        ttk.Label(self, text=what, font=("Segoe UI", 13, "bold"), padding=(20, 4)).pack()
        self.msg = ttk.Label(self, text="on this player's controller   (Esc or Cancel to stop)", padding=(20, 4))
        self.msg.pack()
        ttk.Button(self, text="Cancel", command=self.destroy).pack(pady=(4, 15))
        self.bind("<Escape>", lambda e: self.destroy())
        self.base, self.pbase = joy.held(jid), joy.pov(jid)
        self.start = time.time()
        self.grab_set()
        self.after(30, self.poll)

    def poll(self):
        if not self.winfo_exists():
            return
        now, pnow = self.joy.held(self.jid), self.joy.pov(self.jid)
        new, pnew = sorted(now - self.base), sorted(pnow - self.pbase)
        self.base &= now
        self.pbase &= pnow
        if pnew:
            self.result = pnew[0]
            self.destroy()
            return
        for n in new:
            if str(n) in self.numbers:
                self.result = MAP_XNAMES[self.numbers.index(str(n))]
                self.destroy()
                return
            self.msg.config(text="Button %d isn't in this controller's map (Controller maps tab)." % n)
        if time.time() - self.start > self.TIMEOUT:
            self.destroy()
            return
        self.after(30, self.poll)


def _num_eq(a, b):
    try:
        return abs(float(a) - float(b)) < 1e-6
    except ValueError:
        return a == b


def mask_to_names(mask):
    return [n for n, b in PAD_BITS if mask & b]


# --------------------------------------------------------------------------------------------
# The ini file: change values in place, keep every comment
# --------------------------------------------------------------------------------------------
# app f1.333 (code review #1): the file's own encoding is kept - UTF-16 ("Unicode" in Notepad, the mod reads it too),
# UTF-8 with or without a BOM, else the Windows code page. Lines split only on \r\n / \n (not on \x85 etc.).
def read_text_file(path):
    with open(path, "rb") as f:
        b = f.read()
    if b.startswith(b"\xff\xfe"):
        return b[2:].decode("utf-16-le", "replace"), "utf-16-le"
    if b.startswith(b"\xfe\xff"):
        return b[2:].decode("utf-16-be", "replace"), "utf-16-be"
    if b.startswith(b"\xef\xbb\xbf"):
        return b[3:].decode("utf-8", "replace"), "utf-8-sig"
    try:
        return b.decode("utf-8"), "utf-8"
    except UnicodeDecodeError:
        return b.decode("cp1252", "replace"), "cp1252"


def text_lines(text):
    lines = text.split("\n")
    if lines and lines[-1] == "":
        lines.pop()
    return [ln[:-1] if ln.endswith("\r") else ln for ln in lines]


def encode_text(text, enc):
    if enc == "utf-16-le":
        return b"\xff\xfe" + text.encode("utf-16-le")
    if enc == "utf-16-be":
        return b"\xfe\xff" + text.encode("utf-16-be")
    if enc == "utf-8-sig":
        return b"\xef\xbb\xbf" + text.encode("utf-8")
    if enc == "cp1252":
        return text.encode("cp1252", "replace")
    return text.encode("utf-8")


def write_file_safely(path, data):
    """app f1.333 (code review #2): write a temporary file, then swap it in - a failure half-way can't leave the file cut short."""
    tmp = path + ".tmp"
    with open(tmp, "wb") as f:
        f.write(data)
    try:
        os.replace(tmp, path)
    except OSError:
        # the swap can be refused (another program holding the file, a read-only file): write it directly, as before
        try:
            os.remove(tmp)
        except OSError:
            pass
        with open(path, "wb") as f:
            f.write(data)


class IniFile:
    def __init__(self, path):
        self.path = path
        self.lines = []
        self.exists = False
        self.encoding = "utf-8"
        self.load()

    def load(self):
        self.lines = []
        self.exists = os.path.isfile(self.path)
        if self.exists:
            text, self.encoding = read_text_file(self.path)
            self.lines = text_lines(text)

    def _find(self, section, key):
        """Returns (index of key line or None, index where a new key for this section should go or None)."""
        sec = None
        insert_at = None
        for i, raw in enumerate(self.lines):
            line = raw.strip()
            if line.startswith("[") and line.endswith("]"):
                sec = line[1:-1].strip().lower()
                if sec == section.lower():
                    insert_at = i + 1
                continue
            if sec == section.lower():
                if line and not line.startswith(";"):
                    insert_at = i + 1
                if "=" in line and not line.startswith(";"):
                    k = line.split("=", 1)[0].strip()
                    if k.lower() == key.lower():
                        return i, insert_at
        return None, insert_at

    def get(self, section, key, default=""):
        i, _ = self._find(section, key)
        if i is None:
            return default
        v = self.lines[i].split("=", 1)[1]
        if ";" in v:
            v = v.split(";", 1)[0]
        return v.strip()

    def set(self, section, key, value):
        i, ins = self._find(section, key)
        text = "%s=%s" % (key, value)
        if i is not None:
            m = re.search(r"\s;", self.lines[i].split("=", 1)[1])   # app f1.333 (code review #10): a note after the value stays
            self.lines[i] = text + (self.lines[i].split("=", 1)[1][m.start():] if m else "")
        elif ins is not None:
            self.lines.insert(ins, text)
        else:
            if self.lines and self.lines[-1].strip():
                self.lines.append("")
            self.lines.append("[%s]" % section)
            self.lines.append(text)

    def save(self):
        if self.exists:
            bak = self.path + ".bak"
            try:   # app f1.333 (code review #2): a read-only flag isn't copied onto the backup; a backup that fails doesn't stop the save
                if os.path.exists(bak):
                    os.chmod(bak, stat.S_IWRITE | stat.S_IREAD)
                shutil.copyfile(self.path, bak)
            except OSError:
                pass
        write_file_safely(self.path, encode_text("\r\n".join(self.lines) + "\r\n", self.encoding))
        self.exists = True


# --------------------------------------------------------------------------------------------
# Defaults (same as the mod's own defaults)
# --------------------------------------------------------------------------------------------
GENERAL = [
    # key, label, kind, default, extra
    ("JoinKey", "Keyboard key to join / leave", "key", "F10", None),                       # shown on the Player 2 tab
    ("JoinAtStart", "When the game starts (Auto / 1 / 0)  *", "choice", "Auto", ["Auto", "1", "0"]),   # Player 2 tab (f1.11: a list there)
    ("LeadKeyPlayer1", "Hot swap: player 1 takes the lead", "key", "F9", None),
    ("LeadKeyPlayer2", "Hot swap: player 2 takes the lead", "key", "Ctrl+F9", None),
    ("LeadKeyPlayer3", "Hot swap: player 3 takes the lead", "key", "Shift+F9", None),
    ("ReloadKey", "Reload these settings while the game is running", "key1", "F6", None),
    ("HelpKey", "Show / hide the list of co-op keys and buttons in game", "key1", "F5", None),
    ("ShowFPS", "Show the frame rate at the top of the screen", "bool", "0", None),
    ("ShowClock", "Show a clock in the top right corner (the same time as the log)", "bool", "0", None),
    ("EnemyChangesApplyNow", "Testing only: enemy setting changes apply during a battle", "bool", "0", None),   # f1.274 (the author: "put it in troubleshooting")
    ("BossTargetsAITeammates", "Testing only: bosses treat AI teammates like players when spreading their attention", "bool", "0", None),   # f1.213
    ("BossSpreadNearest", "Testing only: bosses that go after the nearest party member spread their attention too", "bool", "0", None),   # f1.224
    ("MenuDarken", "Shared screen (no split screen): darken it while anyone's command menu is open (the game's normal)", "bool", "1", None),   # f1.105 (the author); f1.208: the shared screen only
    # f1.208 (the author: "a setting for each player ... Only their own screen should darken" / "a separate filter for the co-op camera")
    ("MenuDarkenSplitPlayer1", "Split screen: darken player 1's own view while their command menu is open", "bool", "1", None),
    ("MenuDarkenSplitPlayer2", "Split screen: darken player 2's own view while their command menu is open", "bool", "1", None),
    ("MenuDarkenSplitPlayer3", "Split screen: darken player 3's own view while their command menu is open", "bool", "1", None),
    ("DangerPerScreen", "Near-death red edge only in that player's own view (split screen)", "bool", "1", None),   # f1.106 (the author)
    ("DangerHealth", "Near death = at or below this much of the character's health (%)", "num", "25", (1, 99)),   # f1.106
    ("KeyboardLeaderKeepsLead", "Disallow hot swap when Player 1 is using the keyboard", "bool", "1", None),   # f1.78; f1.80: General tab (the author: confirmed working)
    ("ShowTargetMarker", "Show players 2 and 3's target reticles (and their enemy's name / health bar on the shared screen)", "bool", "1", None),
    # f1.39g: GameStyleMarker / GameStyleMarkerOnTop no longer offered (the author: "The only option for target markers should be
    #   the game style. We do not need the old drawn style anymore") - both stay 1 (the DLL's default); the old drawn marker +
    #   the widget-copy path go in a clean-up build (removal checklist B)
    ("ShowOffscreenArrow", "Show an arrow at the screen edge when a player is off screen. P1 is always blue, P2 and P3 colors are set by their own color setting (player tabs)", "bool", "1", None),   # the author's wording (review row 68)
    ("ShowStatusCircles", "Visual indicator (top right) shows whether P2 / P3 are joined human players, AI controlled, or unavailable", "bool", "1", None),   # the author's wording, 2026-10-26
    ("StatusCirclesRight", "User can adjust the location of the status indicator (adjustment is measured in pixels from the right edge of the screen)", "num", "12", (0, 1800)),
    ("StatusCirclesTop", "User can adjust the location of the status indicator (adjustment is measured in pixels from the top edge of the screen)", "num", "12", (0, 1000)),
    ("StatusCirclesSize", "Adjusts the size of the status indicators - a bigger number makes them larger", "num", "0.5", (0.3, 4)),
    # f1.22: PauseInMiniGames removed (the mod's mini-game stand-down is gone - the author)
    # f1.54: MenusWithoutSplitScreen, MenuSpacing, OwnMenuPaging, OwnMenuAllyCommands, Player1MenuInOwnView,
    #   ReserveQueuedItems (and OwnMenuTargetCursor / HideP1ReticleInWindows, f1.46) are no longer settings in the mod either
    # f1.59: OwnCommandMenu removed - the Shared menu style was dropped (the author 2026-10-21: "Drop the shared menu")
    ("AIGuardCopy", "AI teammates guard when a player guards", "bool", "1", None),
    # f1.21b: DirectInputPads is no longer offered - Off only does harm since f1.9 (it would let a P2/P3 PlayStation
    #   controller drive player 1 too). Leave it on Auto in the ini.
]
CAMERA = [
    ("Enabled", "Begin the game with the co-op camera on", "bool", "1", None),   # f1.39ac: the author's wording
    ("ToggleKey", "Key to switch the co-op camera on / off in game", "key", "F8", None),
    # f1.85: OffWithWindows removed - it did nothing (the co-op camera is always off while the split-screen windows show)
    ("Bias", "Where the co-op camera points: 0 = on player 1, 1 = the middle of the group (players and nearby enemies). In between leans toward player 1.", "num", "0.4", (0, 1)),   # review row 70 (the author OK'd)   # f1.125: 0.6 -> 0.4 (the author: "Yeah, this is better")
    ("BaseZoomOut", "The closest the co-op camera gets to player 1", "num", "800", (100, 3000)),   # f1.200 (the author, review row 71): a plain distance from player 1
    ("MaxDistance", "The farthest the co-op camera may pull back from player 1", "num", "3500", (300, 20000)),
    ("ScreenMargin", "How much of the screen the players (and nearby enemies) can fill before the co-op camera pulls back", "num", "0.6", (0.2, 1)),   # f1.124: 0.6 (the author)   # f1.98 (E3): the mod accepts 0.2 - 1
    ("MaxTilt", "The steepest the co-op camera looks down, reached at full tilt (below)", "num", "35", (0, 89)),
    ("TiltStartSpread", "The camera starts tilting down once everyone is this far apart", "num", "800", (0, 20000)),
    ("TiltFullSpread", "The camera reaches its steepest tilt once everyone is this far apart", "num", "3000", (1, 30000)),
    ("EnemyRadius", "Also keep enemies within this distance of player 1 in view (limited by the setting below)", "num", "2500", (0, 20000)),
    ("EnemyExtraAllowance", "How much further back the camera may pull to also fit the nearby enemies (setting above). 0.25 = up to 25% further back. If fitting them would take more, they're left out and the camera stays on the players and their targets.", "num", "0.25", (0, 5)),
    ("Smoothing", "How quickly the co-op camera follows to its new framing (higher = snappier)", "num", "3.0", (0.1, 50)),
    ("CutsceneDistance", "If the game's camera moves this far from player 1, the mod treats it as a cutscene and hands the camera back to the game", "num", "3000", (500, 100000)),
    ("TalkScenes", "Step aside for the game's camera during talking scenes and cinematic shots (also hides the split screen)", "bool", "1", None),   # f1.120 (the author); f1.121: + cinematic shots
    ("WallCheck", "Move in front of walls that block the view (characters, enemies and props are ignored)", "bool", "1", None),
    ("AutoOrient", "Turn to look from behind player 1 when everyone won't fit", "bool", "1", None),
    ("AutoOrientSpeed", "Auto-turn speed (degrees per second)", "num", "60", (5, 720)),
]
CAMERA_PRESETS = {
    "Default": {},
    "Close (tighter)": {"Bias": "0.4", "BaseZoomOut": "550", "MaxDistance": "1800", "MaxTilt": "35",
                                   "ScreenMargin": "0.85", "EnemyRadius": "1500"},
    "Wide (overhead, sees more)": {"Bias": "0.7", "BaseZoomOut": "900", "MaxDistance": "3500", "MaxTilt": "70",
                                   "TiltStartSpread": "300", "TiltFullSpread": "1600", "ScreenMargin": "0.75",
                                   "EnemyRadius": "3000"},
}   # f1.39aa (the author): "Off (the game's normal camera)" preset removed - the "Use the co-op camera" check box does that
CAMERA_PRESET_NOTES = {   # f1.39ac (the author: "Balanced - this is not descriptive enough. Explain what it attempts to do.")
    "Default": "at least 8 m back, aimed between player 1 and the group.\n"
               "    As players spread apart it pulls back (up to 35 m) and tilts down a little to keep everyone in view;\n"
               "    nearby enemies stay in view too",   # f1.111: the author's tested values are the default
    "Close (tighter)": "aimed closer to player 1, pulls back less (up to 18 m) - bigger characters, less of the area",
    "Wide (overhead, sees more)": "aimed at the middle of everyone and looks down much more steeply (up to 35 m back) -\n"
                                  "    sees more of the area and the enemies, smaller characters"}
CAMERA_FRAMING = ["Bias", "BaseZoomOut", "MaxDistance", "ScreenMargin", "MaxTilt", "TiltStartSpread", "TiltFullSpread",
                  "EnemyRadius", "EnemyExtraAllowance", "Smoothing", "AutoOrient", "AutoOrientSpeed", "WallCheck", "CutsceneDistance"]
CAMERA_METRES = ("BaseZoomOut", "MaxDistance", "TiltStartSpread", "TiltFullSpread", "EnemyRadius", "CutsceneDistance")   # cm in the file, m in the app
PIP = [
    ("Enabled", "Split screen on when the game starts (always on if 'When' is Battle only)  *", "bool", "1", None),   # f1.157: default on, battle only (the author)
    ("ToggleKey", "Split screen is activated when:", "key", "F7", None),   # the author's wording (tooltip review row 32)
    ("LayoutKey", "Press in game to cycle the layout: Right -> Left -> Bottom -> Top (-> Thirds, if allowed)", "key", "Ctrl+F7", None),   # f1.39u: shorter; f1.80: thirds
    ("Show", "When: Always, or Battle only (one shared camera in the field)", "choice", "Battle", ["Always", "Battle"]),
    ("Side", "Sets the default layout for Players 2 & 3's windows", "choice", "Right", ["Right", "Left", "Bottom", "Top", "Thirds"]),   # f1.80: Thirds
    ("FullSizeHudQuality", "Window sharpness (%): 100 = sharpest, lower = blurrier but faster", "num", "100", (25, 100)),   # app f1.330: Ultra default (the author)
    ("Detail", "Detail: 1 = normal, higher = simpler models (faster)", "num", "1", (0.5, 10)),   # app f1.330: Ultra default
    ("ViewDistance", "Draw distance (0 = no limit; 5000 - 10000 = faster)", "num", "0", (0, 1000000)),
    # f1.81: [PictureInPicture] TallWindowFov / WindowVignette / WindowBrightness are file-only for now (defaults = the fixes)
    ("ThirdsLayout", "Allow the Thirds split-screen layout on a normal (16:9) monitor - with 'Multiple monitors / ultrawide' on, one wide screen gets thirds by itself", "bool", "0", None),   # app f1.327: Troubleshooting tab (the author)
    ("ThirdsCommandNudge", "Thirds: move the command HUD / menus / shortcuts left by (px at 1080p)", "num", "40", (0, 300)),   # f1.146 (the author)
    ("ThirdsPanelNudge", "Thirds: move the health / ATB panels right by (px at 1080p)", "num", "40", (0, 300)),   # f1.146
    # f1.311 (the author: "Two monitor support should be an option under the experimental settings for split screen" + three
    # monitors): [PictureInPicture] MultiMonitor / MonitorPlayer1 / MonitorShare (the mod also still reads MonitorSpanTest)
    ("MultiMonitor", "Multiple monitors: the game spans every monitor, one player per monitor (borderless window)", "bool", "0", None),
    ("MonitorPlayer1", "Player 1's monitor", "choice", "Main", ["Main", "Left", "Middle", "Right"]),
    ("MonitorShare", "Two monitors, players 2 and 3: how they share the second monitor", "choice", "Columns", ["Columns", "Rows"]),
    ("MultiMonitorAsOneScreen", "Test: treat all monitors as one wide screen (ultrawide layout - halves / thirds of the whole picture)", "bool", "0", None),
    ("WindowsForAI", "Show player 2's / 3's split-screen windows even while the AI plays them (testing only)", "bool", "0", None),
    ("DynamicQuality", "Automatically lower the split-screen windows' quality when the frame rate drops below the target", "bool", "0", None),   # f1.113 (the author)
    ("DynamicTargetFps", "Frame rate to keep (fps)", "num", "55", (20, 240)),   # the author: 55, not 60 (60 is a common game setting)   # f1.113   # f1.104: Troubleshooting tab   # f1.84: Experimental tab
]
# f1.54: the window camera is per player only ([Player2] / [Player3] Window*); these are its labels / defaults / ranges
# (the shared [PictureInPicture] View / Distance / ... keys and the [PlayerN] CameraSpeed % are no longer read by the mod)
WINDOW_BASE = [
    ("Distance", "Distance behind the player (bigger = further out)", "num", "260", (50, 3000)),   # f1.82: 320 -> 270; app f1.332: 260 (the author)
    ("Height", "Height above the player", "num", "60", (-100, 500)),
    ("Pitch", "Look-down angle (degrees)", "num", "20", (-30, 80)),   # f1.82: 12 -> 20
    ("FOV", "Field of view (degrees)", "num", "80", (40, 120)),
    ("TurnSpeed", "How fast the camera turns back behind the player - a bigger number is faster", "num", "3", (0.2, 30)),   # the author OK'd (review row 54)
    ("AutoFollowDelay", "After the designated time (seconds after the right stick is let go), the camera will automatically reorient itself to the default position behind the player. 0 = never", "num", "3", (0, 60)),   # the author's wording (review row 47)
    ("CameraControl", "Right stick turns the window's camera (a flick switches targets while locked on)", "bool", "1", None),
    ("CameraSpeed", "Right-stick turn speed (degrees per second)", "num", "150", (20, 600)),
]
# f1.39u (the author: "expandable boxes, one for Player 2 and one for Player 3"): the window camera's fine-tune numbers per
# player - [Player2] / [Player3] WindowDistance ... (f1.54: a missing key = the default, like the mod)
# f1.39v (the author: "Two people may want different options"): ALL window camera settings are per player now
WINDOW_CAM = [   # (per-player key, shared [PictureInPicture] key), in the order the boxes show them
    ("WindowCameraControl", "CameraControl"), ("WindowCameraSpeed", "CameraSpeed"),
    ("WindowAutoFollowDelay", "AutoFollowDelay"),
    ("WindowDistance", "Distance"), ("WindowHeight", "Height"), ("WindowPitch", "Pitch"), ("WindowFOV", "FOV"), ("WindowTurnSpeed", "TurnSpeed")]
WINDOW_SHARED = dict(WINDOW_CAM)
_PIP_BY = {f[0]: f for f in WINDOW_BASE}
WINDOW_FIELDS = [(wk,) + tuple(_PIP_BY[sk][1:]) for wk, sk in WINDOW_CAM]
# by key, not by position (f1.200: a fixed index broke when WindowView / WindowControlsFollowView were removed)
_WF_AT = {f[0]: i for i, f in enumerate(WINDOW_FIELDS)}
WINDOW_FIELDS[_WF_AT["WindowCameraSpeed"]] = ("WindowCameraSpeed", "Right-stick turn speed (degrees per second)") + tuple(_PIP_BY["CameraSpeed"][2:])
WINDOW_FIELDS.insert(_WF_AT["WindowDistance"], ("InvertY", "Invert up / down on the right stick", "bool", "0", None))   # [PlayerN] InvertY (was on the Player tabs), before the distance


def window_default(ini_get, section, key, default):
    """f1.39v: an OLD profile / file's shared [PictureInPicture] value for a [PlayerN] Window* key (WindowCameraSpeed = the
    shared speed x the player's old [PlayerN] CameraSpeed %). f1.54: only used to convert older profiles - the mod
    itself no longer reads the shared keys (a missing Window* key = the default)."""
    d = ini_get("PictureInPicture", WINDOW_SHARED[key], default)
    if key == "WindowCameraSpeed":
        try:
            d = "%g" % (float(d) * float(ini_get(section, "CameraSpeed", "100")) / 100.0)
        except (TypeError, ValueError):
            pass
    return d


SHARP_CHOICES = [("Full (100%)", "100"), ("High (90%)", "90"), ("Medium (75%)", "75"), ("Low (50%)", "50")]   # f1.39z: FullSizeHudQuality
DETAIL_CHOICES = [("Full (like the main view)", "1"), ("High", "1.5"), ("Medium", "2.5"), ("Low (fastest)", "4")]   # f1.39y: [PictureInPicture] Detail
VIEWDIST_CHOICES = [("No limit", "0"), ("100 m", "10000"), ("60 m", "6000"), ("40 m", "4000")]   # f1.39y: ViewDistance (game units = cm)
# f1.113: dynamic quality (the author: "select which things it would attempt to adjust and in what order ... under a drop-down
# advanced settings"): [PictureInPicture] DynamicOrder = the levers' names in order (left out = never touched)
PARTY_PANEL_CHOICES = [("Shared health/ATB", "Shared"), ("Individual health/ATB", "Individual")]   # f1.117 (the author's names)
PARTY_ROWS_CHOICES = [("Whole party", "All"), ("Their own character + AI teammates", "OwnAndAI"), ("Only their own character", "Own")]   # f1.133: OwnAndAI (the author)
DYN_LEVERS = [("Model detail", "Detail"), ("Draw distance", "DrawDistance"), ("Sharpness (short flicker)", "Sharpness"),
              ("Half-rate windows (every other frame)", "HalfRate")]
DYN_NOT_USED = "(not used)"
DYN_DETAIL_FLOOR = [(t, v) for t, v in DETAIL_CHOICES if v != "1"]
DYN_DIST_FLOOR = [(t, v) for t, v in VIEWDIST_CHOICES if v != "0"]
DYN_SHARP_FLOOR = [(t, v) for t, v in SHARP_CHOICES if v != "100"]

SPLIT_START = {   # what the split screen does when the game starts -> (Enabled, Show); F7 cycles the same three in game
    "Always on": ("1", "Always"), "Battle only": ("1", "Battle"), "Off": ("0", "Always"),
}
PIP_QUALITY = {
    "Low (slower PCs)": {"FullSizeHudQuality": "50", "Detail": "2.5", "ViewDistance": "6000"},   # f1.39u: no Resolution (only for the mod's HUD copy)
    "Medium": {"FullSizeHudQuality": "75", "Detail": "1.5", "ViewDistance": "0"},
    "High": {"FullSizeHudQuality": "90", "Detail": "1", "ViewDistance": "0"},
    "Ultra (sharpest, fast PCs)": {"FullSizeHudQuality": "100", "Detail": "1", "ViewDistance": "0"},
}
WEAK_LEVELS = ["Off", "Low", "Medium", "High", "Extreme"]   # [Balance] PreferWeakTargets - f1.93: a 25 / 50 / 75 / 100 % chance per new target pick
BALANCE = [
    ("EnemyHP", "Enemy health (%)", "num", "100", (25, 1000)),
    ("StaggerRate", "How fast enemies get staggered (%)", "num", "100", (10, 500)),
    # f1.92: ShareEnemyAttention gone (the game already shares enemies between the people playing)
    ("StayOnHitChance", "Chance an enemy that hits the player it's after stays on them (%, 0 = the game's normal)", "num", "0", (0, 100)),
    ("TurnOnHeavyDamage", "An enemy turns on a player who hits it hard (% of its health, 0 = off)", "num", "0", (0, 100)),
    ("FocusOnPlayer2", "How much enemies go after players 2 and 3 (%, 100 = as much as player 1)", "num", "100", (0, 1000)),
    # f1.21b: PlayerRulesForHumans is no longer offered - since v13.5 Player2Rates / Player3Rates decide it (above 0 =
    #   player 1's ATB + stagger rules); the mod only reads the old key when those are missing.
    ("Player1Rates", "Player 1's ATB fill rate (%)", "num", "90", (0, 100)),   # f1.157: default 90 = Co-op Balanced (only while someone else plays)   # f1.47: never above 100
    ("Player2Rates", "Player 2's ATB fill rate (%)", "num", "90", (0, 100)),   # f1.47: never above 100
    ("Player3Rates", "Player 3's ATB fill rate (%)", "num", "90", (0, 100)),   # f1.47: never above 100
    ("MenuSlowdown", "Slowdown while player 1's command menu is open (% of the game's own)", "num", "100", (0, 100)),
    # f1.48 (the author: "Separate slowdown for player 2 and 3"): MenuSlowdownOthers split in two (f1.54: the old key is gone)
    ("MenuSlowdownPlayer2", "Slowdown while player 2's command menu is open (% of the game's own)", "num", "100", (0, 100)),
    ("MenuSlowdownPlayer3", "Slowdown while player 3's command menu is open (% of the game's own)", "num", "100", (0, 100)),
    ("EnemiesTreatHumansLikePlayer1", "Enemies take players 2 and 3 as seriously as player 1 when picking a target", "bool", "1", None),   # f1.39ad: wording
    # f1.39ad (the author's difficulty presets): new - read by a coming DLL build, not by f1.39
    ("EnemyDamage", "Enemy damage (%)", "num", "100", (25, 500)),
    ("EnemyAttackRate", "How often enemies attack (%)", "num", "100", (50, 300)),
    ("PreferWeakTargets", "Enemies go after a weakened player (low health) more often", "choice", "Off", WEAK_LEVELS),   # f1.48 (the author): levels
    ("WeakTargetHealth", "A player counts as weakened below this much health (%)", "num", "20", (1, 99)),   # f1.93 (the author); f1.94: default 20
    # f1.151 (the author): [Balance2] / [Balance3] only - "Only applies when human players are connected - otherwise based on 1 character settings"
    ("OnlyWithHumanPlayers", "Ticked: these settings are only used when every character is played by a person", "bool", "1", None),   # review row 95 (the author's name for it)
]
# f1.151 (the author 2026-10-25: "delineate enemy settings under 3 drop down tabs... Single character party / Two character
# party / Three character party"): the enemy values are one set per party size - [Balance1] / [Balance2] / [Balance3]
PARTY_SETS = [("Balance1", "One character party"), ("Balance2", "Two character party"), ("Balance3", "Three character party")]
# f1.272 (the author: "if you selected 'Same difficulty no matter how many humans are playing' then it would just be one
# 'Global' dropdown box. If you select 'Dynamic difficulty based on human player count' then it would show the three"):
# [Balance] DynamicDifficulty=0 -> the mod always uses [BalanceGlobal]
GLOBAL_SET = ("BalanceGlobal", "Global - any number of people")
ALL_SETS = PARTY_SETS + [GLOBAL_SET]
# f1.272 (the author: "the menu slowdown and darkening should also be based on player 1, player 2, and player 3 settings"):
# per set too. A key missing from a set = the old shared one ([Balance] / [General]) - in the mod and in this app.
SET_MENU_SLOW = ["MenuSlowdown", "MenuSlowdownPlayer2", "MenuSlowdownPlayer3"]
SET_MENU_DARK = ["MenuDarkenSplitPlayer1", "MenuDarkenSplitPlayer2", "MenuDarkenSplitPlayer3", "MenuDarken"]
# f1.275 (the author: "Player's ATB speed should also be part of dynamic settings"): ATB per set too. f1.276 (the author:
# "How do you know someone wouldn't want to adjust their ATB when playing single player?"): the one character party has
# player 1's ATB speed too (default 100 = the game's normal; its missing key is 100, not the shared [Balance] value)
SET_ATB = ["Player1Rates", "Player2Rates", "Player3Rates"]
MENU_SHARED_SEC = dict([(k, "Balance") for k in SET_MENU_SLOW + SET_ATB] + [(k, "General") for k in SET_MENU_DARK])
SET_TOUGH = ["EnemyHP", "StaggerRate", "EnemyDamage", "EnemyAttackRate"]
SET_TARGET = ["StayOnHitChance", "TurnOnHeavyDamage", "FocusOnPlayer2", "PreferWeakTargets", "WeakTargetHealth"]


# f1.157 (the author: "With one human player, it should be the Game's original default difficulty. With 2 or 3 human players,
# the default should be Co-op Balanced"): the two / three character sets' defaults (= the DLL's BalanceSetDefault)
SET_DEFAULTS = {("Balance2", "EnemyHP"): "125", ("Balance2", "StaggerRate"): "90", ("Balance2", "EnemyDamage"): "105",
                ("Balance2", "StayOnHitChance"): "75",
                ("Balance3", "EnemyHP"): "150", ("Balance3", "StaggerRate"): "85", ("Balance3", "EnemyDamage"): "110",
                ("Balance3", "StayOnHitChance"): "75",
                ("BalanceGlobal", "EnemyHP"): "125", ("BalanceGlobal", "StaggerRate"): "90", ("BalanceGlobal", "EnemyDamage"): "105",
                ("BalanceGlobal", "StayOnHitChance"): "75",   # f1.272: the global set starts as the two character party's
                ("Balance1", "Player1Rates"): "100",
                ("Balance3", "Player1Rates"): "85", ("Balance3", "Player2Rates"): "85", ("Balance3", "Player3Rates"): "85"}   # app f1.333 (code review #6): Co-op Balanced's three character ATB   # f1.276: solo = the game's normal ATB unless the player lowers it


def set_fields(section):
    """BALANCE's fields with this party-size set's defaults."""
    return [(k, l, kind, SET_DEFAULTS.get((section, k), d), e) for k, l, kind, d, e in BALANCE]


def set_keys(section):
    """The keys of one party-size set (the single-character party has no players 2 / 3 to focus on, and nobody to wait for)."""
    keys = SET_TOUGH + SET_TARGET
    if section == "Balance1":
        keys = [k for k in keys if k != "FocusOnPlayer2"]
    elif section != "BalanceGlobal":
        keys = ["OnlyWithHumanPlayers"] + keys   # (f1.272: not shown any more - always 1; the presets still write it)
    return keys + SET_MENU_SLOW + (["Player1Rates"] if section == "Balance1" else SET_ATB)   # f1.272: menu slowdown; f1.275 / f1.276: ATB speed
# f1.39ad (the author): difficulty presets = the [Balance] keys below; built-ins can't be changed or deleted, the player's own
# presets are files in FF7RCoopMod_Difficulty\ next to the settings file ([Difficulty] Name / Description + [Balance] keys)
DIFF_DIR = "FF7RCoopMod_Difficulty"
# f1.151: (section, key) pairs - the per-player values in [Balance] + every party-size set's enemy values
DIFFICULTY_KEYS = [("Balance", k) for k in ["EnemiesTreatHumansLikePlayer1", "DynamicDifficulty"]] + \
                  [(sec, k) for sec, _ in ALL_SETS for k in set_keys(sec)]   # f1.272: + the global set; the menu slowdown per set


def _diff(atb, slow, s1, s2, s3, atb1="100", atb3=None):
    """f1.151: s1 / s2 / s3 = the one / two / three character party's (EnemyHP, StaggerRate, EnemyDamage, EnemyAttackRate,
    StayOnHitChance, PreferWeakTargets). Self-contained (tools/make_presets.py runs it without the rest of the app)."""
    d = {("Balance", "EnemiesTreatHumansLikePlayer1"): "1",
         ("Balance", "DynamicDifficulty"): "1"}   # f1.281c (the author: "They should ALL default to dynamic difficulty")
    for i, (hp, stag, dmg, atk, stay, weak) in enumerate((s1, s2, s3, s2)):   # f1.272: the global set = the two character party's
        sec = "Balance%d" % (i + 1) if i < 3 else "BalanceGlobal"
        if i == 0:
            d[(sec, "Player1Rates")] = atb1   # f1.276: player 1 alone (Vanilla / Balanced 100 = the game's normal)
        for k in ("MenuSlowdown", "MenuSlowdownPlayer2", "MenuSlowdownPlayer3"):   # f1.272: per set (was [Balance])
            d[(sec, k)] = slow
        d.update({(sec, "EnemyHP"): hp, (sec, "StaggerRate"): stag, (sec, "EnemyDamage"): dmg, (sec, "EnemyAttackRate"): atk,
                  (sec, "StayOnHitChance"): stay, (sec, "TurnOnHeavyDamage"): "0", (sec, "PreferWeakTargets"): weak, (sec, "WeakTargetHealth"): "20"})
        if i > 0:
            d.update({(sec, "FocusOnPlayer2"): "100"})
            for k in ("Player1Rates", "Player2Rates", "Player3Rates"):   # f1.275: ATB per set (not the one character party's)
                d[(sec, k)] = atb3 if (i == 2 and atb3) else atb   # f1.281b: the three character party's own ATB (Co-op Balanced 85)
        if i in (1, 2):
            d.update({(sec, "OnlyWithHumanPlayers"): "1"})
    return d


_NORMAL = ("100", "100", "100", "100", "0", "Off")
DIFFICULTY_BUILTIN = {   # name -> (description, values) - the author's four presets (2026-10-20), numbers proposed by Claude, OK'd
    # f1.151 (the author: "Scale by party size"): three characters = the old values; two = about halfway; one = the game's normal
    # on Balanced, a little tougher on Hard / Extreme (numbers proposed by Claude - to be tuned in play)
    # f1.157: "Default" -> "Vanilla" (the mod's default is now Co-op Balanced - the author)
    "Vanilla": ("The normal game for any number of players: players 2 and 3 get the same ATB speed and menu slowdown as player 1.\n"
                "Best for casual or new players - likely easier than a single-player run.",
                _diff("100", "100", _NORMAL, _NORMAL, _NORMAL)),   # f1.92: StayOnHitChance 0 = the game's normal
    "Co-op Balanced": ("THE MOD'S DEFAULT. Player 1 alone (or with AI teammates) = the normal game. With 2 or 3 people playing it\n"
                       "aims for about the same difficulty as a single-player run: a slightly slower ATB (90%, 85% with three people)\n"
                       "and tougher enemies (more health, slower to stagger, a bit more damage) make up for more people attacking.",
                       _diff("90", "100", _NORMAL, ("125", "90", "105", "100", "75", "Off"), ("150", "85", "110", "100", "75", "Off"), atb3="85")),   # f1.281b (the author: "Update my ... preset to my current settings" -> Co-op Balanced): three characters ATB 85
    # f1.222 (the author): Co-op Hard - menu slowdown 0 for every player (was 50)
    # f1.263 (the author, 2026-11-01: "Set the hard preset settings to my current .ini settings."): his own tuned values -
    # one character 150 / 85 / 130 / 150 (was 125 / 90 / 110 / 105); two and three characters unchanged
    # f1.265 (the author, "update the Hard preset to your current values"): his ini again - one character 110 / 90 / 110 / 110;
    # attack rate two characters 120 (was 105), three 130 (was 110)
    "Co-op Hard": ("Harder than a normal single-player run: slower ATB (85%), no menu slowdown, and enemies that attack\n"
                   "more often, hit harder and have more health - also with player 1 alone.",
                   _diff("85", "0", ("110", "90", "110", "110", "0", "Off"), ("160", "80", "115", "120", "80", "Off"), ("200", "75", "125", "130", "80", "Off"), atb1="85")),   # f1.276: harder also alone
    "Co-op Extreme": ("For the most hardcore players: no menu slowdown, slower ATB (80%), and enemies that attack even more often,\n"
                      "hit even harder, have even more health and stay locked on to weakened players (with 2 or 3 people playing).",
                      _diff("80", "0", ("150", "80", "125", "110", "0", "Off"), ("200", "70", "135", "115", "90", "Extreme"), ("250", "65", "150", "120", "90", "Extreme"), atb1="80")),   # f1.276
}
# f1.39ad: the old enemy-only BALANCE_PRESETS (Normal / Co-op balanced / Co-op hard / Relaxed) became DIFFICULTY_BUILTIN above
# (f1.74: PLAYER1_KEYS - the Player 1 tab's "Keyboard keys in the battle command menu" - removed with the DLL's keyboard path)
PAD_GROUPS = [
    ("Battle", [("Attack", "Attack", "X"), ("Dodge", "Dodge", "B"), ("Guard", "Guard", "RB"), ("Ability", "Ability", "Y"),
                ("CommandMenu", "Open command menu", "A"), ("LockOn", "Lock on / switch target", "R3")]),
    ("Command menu (only for a turn in player 1's menu; their own menu uses D-pad / A / B)", [("MenuUp", "Up", "DPadUp"), ("MenuDown", "Down", "DPadDown"), ("MenuConfirm", "Confirm", "A"),
                      ("MenuBack", "Back", "B"), ("MenuPreviousCharacter", "Previous character", "LB, LT"),
                      ("MenuNextCharacter", "Next character", "RB, RT")]),
    ("Joining", [("JoinButton", "Hold to join / leave", "Start")]),
    ("Shortcuts", [("ShortcutHold", "Hold for shortcuts", "LB"), ("Shortcut1", "Shortcut 1 (square)", "X"),
                   ("Shortcut2", "Shortcut 2 (triangle)", "Y"), ("Shortcut3", "Shortcut 3 (circle)", "B"), ("Shortcut4", "Shortcut 4 (cross)", "A")]),
    ("Team (PS4 style)", [("AllyCommand1", "Command AI teammate (L2)", "LT"), ("AllyCommand2", "Command AI teammate (R2)", "RT"),
                          ("SwitchCharacterPrevious", "Switch to another AI teammate (up)", "DPadUp"),
                          ("SwitchCharacterNext", "Switch to another AI teammate (down)", "DPadDown")]),
]
# f1.39e: [PlayerN] ButtonIcons - (shown text, value in the file). f1.89 (the author): Auto stays the default; + Default
# f1.136 (the author: "could we make the reticles colors for P2 and P3 user defined?" - "a list of maybe 6-7 colors"):
# [PlayerN] Color - name, swatch (the DLL's colors, settings/03 kPlayerColors)
PLAYER_COLORS = [("Green", "#4DFF73"), ("Orange", "#FF9E26"), ("Yellow", "#FFEB40"),   # f1.138: no Blue - player 1's color (the author)
                 ("Purple", "#B873FF"), ("Pink", "#FF73CC"), ("Red", "#FF4D47")]


def player_color(section, value):
    """f1.156: the color a [PlayerN] Color value really gives (Default = player 2 green / player 3 orange; unknown = Default)."""
    v = (value or "").strip().lower()
    name = next((n for n, _ in PLAYER_COLORS if n.lower() == v), None)
    return name or ("Orange" if section == "Player3" else "Green")
BUTTON_ICONS = [("Auto", "Auto"), ("PlayStation", "PlayStation"), ("Xbox", "Xbox"), ("Default (original game behavior)", "Default")]
PLAYER_DEFAULTS = {"Player2": {"MoveDeadzone": "0.24"},   # f1.54: PartySlot fixed (player 2 = 2, player 3 = 3)
                   "Player3": {"MoveDeadzone": "0.24"}}

TK_KEY_NAMES = {"Up": "Up", "Down": "Down", "Left": "Left", "Right": "Right", "Return": "Enter", "KP_Enter": "Enter",
                "Escape": "Escape", "space": "Space", "Tab": "Tab", "BackSpace": "Backspace", "Insert": "Insert",
                "Delete": "Delete", "Home": "Home", "End": "End", "Prior": "PageUp", "Next": "PageDown"}
MODIFIERS = {"Control_L", "Control_R", "Shift_L", "Shift_R", "Alt_L", "Alt_R", "Meta_L", "Meta_R"}


def tk_key_to_name(keysym):
    if keysym in TK_KEY_NAMES:
        return TK_KEY_NAMES[keysym]
    if len(keysym) == 1 and keysym.isalnum():
        return keysym.upper()
    if keysym.startswith("F") and keysym[1:].isdigit() and 1 <= int(keysym[1:]) <= 12:
        return keysym
    if keysym.startswith("KP_") and keysym[3:].isdigit():
        return "Num" + keysym[3:]
    return None


def norm_pad(text):
    """'lb,lt' -> 'LB, LT'; returns None if a name is unknown."""
    out = []
    for t in text.replace("+", ",").split(","):
        t = t.strip()
        if not t or t.lower() == "none":   # f1.22: "None" (a cleared button) is a valid value, not unknown
            continue
        if t.lower() not in PAD_NAMES:
            return None
        n = PAD_NAMES[t.lower()]
        if n not in out:
            out.append(n)
    return ", ".join(out) if out else "None"


# --------------------------------------------------------------------------------------------
# Capture dialogs
# --------------------------------------------------------------------------------------------
class PadCapture(tk.Toplevel):
    """Waits for a button press on one controller. result = button name or None."""
    TIMEOUT = 10.0

    def __init__(self, master, xi, slot, what):
        super().__init__(master)
        self.xi, self.slot, self.result = xi, slot, None
        self.title("Press a button")
        self.resizable(False, False)
        self.transient(master)
        ttk.Label(self, text="Press the button for:", padding=(20, 15, 20, 0)).pack()
        ttk.Label(self, text=what, font=("Segoe UI", 13, "bold"), padding=(20, 4)).pack()
        self.msg = ttk.Label(self, text="on controller slot %d   (Esc or Cancel to stop)" % slot, padding=(20, 4))
        self.msg.pack()
        ttk.Button(self, text="Cancel", command=self.destroy).pack(pady=(4, 15))
        self.bind("<Escape>", lambda e: self.destroy())
        ok, self.held = xi.read(slot)   # buttons already held when we started don't count
        self.start = time.time()
        self.grab_set()
        self.after(30, self.poll)

    def poll(self):
        if not self.winfo_exists():
            return
        ok, mask = self.xi.read(self.slot)
        if not ok:
            self.msg.config(text="Controller slot %d isn't connected." % self.slot)
        else:
            new = mask & ~self.held
            self.held &= mask           # a held button that's released can be used again
            names = mask_to_names(new)
            if names:
                self.result = names[0]
                self.destroy()
                return
        left = self.TIMEOUT - (time.time() - self.start)
        if left <= 0:
            self.destroy()
            return
        self.after(30, self.poll)


class KeyCapture(tk.Toplevel):
    """Waits for a key (optionally with Ctrl/Shift/Alt). result = 'Ctrl+F12' style text or None."""

    def __init__(self, master, what, allow_mods):
        super().__init__(master)
        self.result, self.allow_mods = None, allow_mods
        self.title("Press a key")
        self.resizable(False, False)
        self.transient(master)
        ttk.Label(self, text="Press the key for:", padding=(20, 15, 20, 0)).pack()
        ttk.Label(self, text=what, font=("Segoe UI", 13, "bold"), padding=(20, 4)).pack()
        hint = ("A plain key works best (F9-F12 are free). Ctrl/Alt/Shift also guard, dash or open shortcuts in game.\nEsc = cancel." if allow_mods
                else "Esc = cancel.")
        ttk.Label(self, text=hint, padding=(20, 4)).pack()
        ttk.Button(self, text="Cancel", command=self.destroy, takefocus=False).pack(pady=(4, 15))
        self.bind("<KeyPress>", self.key)
        self.grab_set()
        self.focus_force()

    def key(self, e):
        if e.keysym in MODIFIERS:
            return
        if e.keysym == "Escape":   # f1.22: Esc closes the window instead of binding a hotkey to Escape
            self.destroy()
            return
        name = tk_key_to_name(e.keysym)
        if not name:
            return
        mods = []
        if self.allow_mods:
            if e.state & 0x4:
                mods.append("Ctrl")
            if e.state & 0x1:
                mods.append("Shift")
            if e.state & 0x20000:   # Alt on Windows (0x8 there is NumLock)
                mods.append("Alt")
        self.result = "+".join(mods + [name])
        self.destroy()


# --------------------------------------------------------------------------------------------
# f1.39: profiles - other settings files the user (or the release package) keeps in FF7RCoopMod_Profiles\ next to
# FF7RCoopMod.ini. Loading one copies its values into the app (Save then writes them to FF7RCoopMod.ini, which is the
# only file the mod reads). Controller picks ([Input2]: who uses which controller, player 3 on / off) and the
# per-model button maps ([Pad.*]) belong to the PC and are never taken from or put into a profile (the author
# 2026-10-20: "Keep my controllers"). A profile may hold only some settings - the rest stay as they are. Optional
# [Profile] section: Name=, Description=, ReadOnly=1 (the profiles that come with the mod: can't be overwritten or
# deleted from the app).
# --------------------------------------------------------------------------------------------
PROFILE_DIR = "FF7RCoopMod_Profiles"
PROFILE_DEFAULTS = "Mod defaults (built in)"


def profile_key_ok(section, key):
    """True = this setting goes into / comes from profiles (not the PC's controller picks or button maps)."""
    s = section.lower()
    return not (s == "input2" or s.startswith("pad.") or s == "profile" or key.lower().startswith("directinput"))


def read_profile(path):
    """{(section, key): value} of a profile file + its [Profile] info (dict)."""
    vals, info, sec = {}, {}, None
    if True:   # app f1.333: any encoding (read_text_file)
        for raw in text_lines(read_text_file(path)[0]):
            line = raw.strip()
            if not line or line.startswith(";"):
                continue
            if line.startswith("[") and line.endswith("]"):
                sec = line[1:-1].strip()
                continue
            if sec is None or "=" not in line:
                continue
            k, v = line.split("=", 1)
            if ";" in v:
                v = v.split(";", 1)[0]
            k, v = k.strip(), v.strip()
            if sec.lower() == "profile":
                info[k.lower()] = v
            else:
                vals[(sec.lower(), k.lower())] = v
    return vals, info


RESERVED_NAMES = {"CON", "PRN", "AUX", "NUL"} | {"COM%d" % i for i in range(1, 10)} | {"LPT%d" % i for i in range(1, 10)}


def profile_file_name(name):
    keep = "".join(ch if ch.isalnum() or ch in " -_" else ("" if ch in "'`" else " ") for ch in name).strip()   # "Author's" -> "Authors"
    keep = " ".join(keep.split())[:60]
    if keep.upper() in RESERVED_NAMES:   # app f1.333 (code review #9): Windows device names can't be file names
        keep += " profile"
    return keep


# --------------------------------------------------------------------------------------------
# The window
# --------------------------------------------------------------------------------------------
P1_NOTE_PAD = ("The keyboard and mouse always work for player 1 as well.\n"
               "Auto: players get the plugged-in gamepads in order - with one controller, player 1 gets it.\n"
               "Press buttons on a controller - if they show up here, that's the controller player 1 will use.")
P1_NOTE_KB = "Player 1 plays with the keyboard and mouse."


# app f1.333 (code review #4): settings the mod reads as whole numbers (IniInt in settings/03) - "85.5" is refused
INT_KEYS = {"CommandMenuScale", "CommandMenuScaleThird", "CommandMenuScaleQuarter", "PartyPanelScale", "PartyPanelScaleThird",
            "PartyPanelScaleQuarter", "MessageLogX", "MessageLogY", "MenuSlowdown", "MenuSlowdownPlayer2", "MenuSlowdownPlayer3",
            "StayOnHitChance", "TurnOnHeavyDamage", "WeakTargetHealth", "Player1Rates", "Player2Rates", "Player3Rates",
            "DynamicTargetFps", "DynamicLowestSharpness", "FullSizeHudQuality", "ThirdsCommandNudge", "ThirdsPanelNudge",
            "DangerHealth", "MarkerSizeInWindows", "WindowWallRadius", "Rumble"}


def _rng(x):
    return "%d" % x if float(x) == int(x) else "%g" % x


# --------------------------------------------------------------------------------------------
# f1.155 (the author: "every setting gets a short on-screen label, and the full explanation (what it does, default, tips)
# moves into a tooltip, so the tabs get more compact"): hover tooltips
# --------------------------------------------------------------------------------------------
class Tooltip:
    """Shows `text` in a small yellow box next to the mouse after a short hover over `widget`."""
    def __init__(self, widget, text, delay=400):
        self.widget, self.text, self.delay = widget, text, delay
        self.win = None
        self.job = None
        widget.bind("<Enter>", self.enter, add="+")
        widget.bind("<Leave>", self.leave, add="+")
        widget.bind("<ButtonPress>", self.leave, add="+")

    def enter(self, e=None):
        self.cancel()
        self.job = self.widget.after(self.delay, self.show)

    def cancel(self):
        if self.job:
            try:
                self.widget.after_cancel(self.job)
            except tk.TclError:
                pass
            self.job = None

    def show(self):
        self.job = None
        if self.win or not self.text:
            return
        try:
            x = self.widget.winfo_pointerx() + 14
            y = self.widget.winfo_pointery() + 18
        except tk.TclError:
            return
        self.win = tw = tk.Toplevel(self.widget)
        tw.wm_overrideredirect(True)
        lbl = tk.Label(tw, text=self.text, justify="left", background="#ffffe6", foreground="#222", relief="solid",
                       borderwidth=1, wraplength=460, padx=8, pady=5)
        lbl.pack()
        tw.update_idletasks()
        sw, sh = tw.winfo_screenwidth(), tw.winfo_screenheight()   # keep it on the screen
        w, h = tw.winfo_reqwidth(), tw.winfo_reqheight()
        if x + w > sw:
            x = max(0, sw - w - 4)
        if y + h > sh:
            y = max(0, y - h - 30)
        tw.wm_geometry("+%d+%d" % (x, y))

    def leave(self, e=None):
        self.cancel()
        if self.win:
            try:
                self.win.destroy()
            except tk.TclError:
                pass
            self.win = None


def tip(widget, text):
    """Attach a hover tooltip (nothing for an empty text)."""
    if text:
        Tooltip(widget, text)
    return widget


# Short on-screen labels (f1.155). The long label in the field lists becomes the tooltip's first line. Keyed by
# "Section.Key" first, then "Key" (the window-camera keys also without their "Window" prefix).
SHORT = {
    "General.ReloadKey": "Reload settings key", "General.HelpKey": "Co-op keys list key",
    "ShowFPS": "Show frame rate", "ShowClock": "Show clock",
    "MenuDarken": "Shared screen: darken when anyone's menu is open", "DangerPerScreen": "Near-death edge per player view",
    "MenuDarkenSplitPlayer1": "Player 1: darken their view when menu is open", "MenuDarkenSplitPlayer2": "Player 2: darken their view when menu is open",   # f1.274e (the author)
    "MenuDarkenSplitPlayer3": "Player 3: darken their view when menu is open",
    "DangerHealth": "Near death below (% health)", "KeyboardLeaderKeepsLead": "No hot swap when P1 uses the keyboard",
    "ShowTargetMarker": "P2 / P3 target reticles", "ShowOffscreenArrow": "Off-screen arrows",
    "ShowStatusCircles": "P2 / P3 joined status indicator", "StatusCirclesRight": "P2 / P3 joined status indicator - horizontal location adjustment",
    "StatusCirclesTop": "P2 / P3 joined status indicator - vertical location adjustment", "StatusCirclesSize": "P2 / P3 joined status indicator - size adjustment", "AIGuardCopy": "AI teammates copy guarding",
    "JoinAtStart": "Joined when the game starts  *",
    "LeadKeyPlayer1": "Hot swap: P1 takes the lead", "LeadKeyPlayer2": "Hot swap: P2 takes the lead", "LeadKeyPlayer3": "Hot swap: P3 takes the lead",
    "Camera.Enabled": "Co-op camera on at start", "Camera.ToggleKey": "Co-op camera on / off key",
    "Bias": "Camera aim", "BaseZoomOut": "Distance back (m)", "MaxDistance": "Farthest back (m)",
    "ScreenMargin": "Screen fill before zooming out", "MaxTilt": "Steepest tilt (degrees)", "TiltStartSpread": "Tilt starts when players are (m) apart",
    "TiltFullSpread": "Full tilt when players are (m) apart", "EnemyRadius": "Keep enemies within (m)", "EnemyExtraAllowance": "Extra zoom out for nearby enemies",
    "Smoothing": "Camera responsiveness", "CutsceneDistance": "Cutscene distance (m)", "TalkScenes": "Game camera in talking scenes",
    "WallCheck": "Stop in front of walls",
    "AutoOrient": "Auto-turn when everyone won't fit", "AutoOrientSpeed": "Auto-turn speed (deg / s)",
    "PictureInPicture.Enabled": "Split screen on at start  *", "PictureInPicture.ToggleKey": "Split screen mode key",
    "LayoutKey": "Split screen layout key",   # round 2 (was "Change split screen layout")
    "Show": "When", "Side": "Layout at start", "FullSizeHudQuality": "Window sharpness (%)",
    "Detail": "Model detail", "ViewDistance": "Draw distance", "ThirdsLayout": "Allow Thirds on a normal monitor",
    "ThirdsCommandNudge": "Thirds: command HUD left (px)", "ThirdsPanelNudge": "Thirds: panels right (px)",
    "MultiMonitor": "Multiple monitors / ultrawide", "MonitorPlayer1": "Player 1's monitor", "MonitorShare": "P2 / P3 on one monitor",   # f1.311
    "MultiMonitorAsOneScreen": "Test: monitors as one wide screen",
    "MessageLogX": "Move right (px)", "MessageLogY": "Move down (px)",   # f1.161
    "WindowsForAI": "Windows for AI-played characters", "DynamicQuality": "Dynamic quality", "DynamicTargetFps": "Frame rate to keep (fps)",
    "Distance": "Distance behind (m)", "Height": "Height (m)", "Pitch": "Look-down (degrees)", "FOV": "Field of view (degrees)",
    "TurnSpeed": "Auto return speed", "AutoFollowDelay": "Auto return (s)", "CameraControl": "Right stick turns the camera",
    "CameraSpeed": "Turn speed (deg / s)", "InvertY": "Invert up / down",
    "EnemyHP": "Enemy health (%)", "StaggerRate": "Stagger speed (%)", "StayOnHitChance": "Stay on hit (%)",
    "TurnOnHeavyDamage": "Enemy turns on heavy hitters (% of its HP)", "FocusOnPlayer2": "Focus on P2 / P3 (%)",
    "Player1Rates": "Player 1 ATB speed (%)", "Player2Rates": "Player 2 ATB speed (%)", "Player3Rates": "Player 3 ATB speed (%)",
    "MenuSlowdown": "Player 1's menu slowdown speed (%)", "MenuSlowdownPlayer2": "Player 2's menu slowdown speed (%)",   # f1.274d (the author)
    "MenuSlowdownPlayer3": "Player 3's menu slowdown speed (%)",
    "EnemiesTreatHumansLikePlayer1": "Enemies treat P2 / P3 like P1", "EnemyDamage": "Enemy damage (%)",
    "BossTargetsAITeammates": "Bosses treat AI teammates like players",
    "EnemyChangesApplyNow": "Apply enemy setting changes immediately",
    "BossSpreadNearest": "Bosses that chase the nearest player spread out too",
    "EnemyAttackRate": "How often enemies attack (%)", "PreferWeakTargets": "Target weakened players (chance)",
    "WeakTargetHealth": "...weakened = below this % health", "OnlyWithHumanPlayers": "Dynamic difficulty based on human player count",
}
# Extra tips beyond the long label (f1.155)
TIPS = {
    "PictureInPicture.ToggleKey": "Never (split screen is off)\nBattle only (split screen turns on when battle begins, then off when battle ends)\n"
        "Always (split screen is always active, except for cutscenes or certain scripted events)\nPress this key in game to switch between them.",
    "ShowStatusCircles": "The player's color with \"PLAYER\" = human controlled\nRed with \"AI\" = AI controlled\nFaded gray = no character available",
    "EnemyAttackRate": "Their movement stays the same - only the pauses between attacks change.",
    "StayOnHitChance": "Enemies normally move on to another player after almost every attack. With this, an enemy that lands a hit "
                       "keeps going after that player until it misses.",
    "TurnOnHeavyDamage": "That much damage within about 4 seconds turns the enemy on that player.",
    "PreferWeakTargets": "Low / Medium / High / Extreme = a 25 / 50 / 75 / 100 % chance that an enemy picking a new target goes for "
                         "the weakest player below the health line instead.",
    "OnlyWithHumanPlayers": "If some are AI teammates, the settings for the number of people actually playing are used instead "
                            "(e.g. 2 people + 1 AI teammate = the 2-character settings; player 1 alone = the 1-character settings).\n"
                            "Unticked: these settings are used whenever the party has this many characters, whether people or the AI play them.",
    "Player1Rates": "100 = normal speed (the most), 50 = half as fast, 0 = the slower speed the game gives AI teammates.",
    "MenuSlowdown": "100 = the game's normal slowdown while that command menu is open, 50 = half as much, 0 = none (time keeps running).",
    "ScreenMargin": "Lower = the camera zooms out sooner, leaving more space around everyone.\n"   # review row 73 (the author OK'd)
                    "Higher = the camera stays closer, so players can get nearer the screen edges.\n"
                    "Example: 0.6 = everyone stays within the middle 60% of the screen.",
    "Side": "Right / Left / Bottom / Top = where players 2 and 3's windows go. Thirds = Three vertical columns (Requires setting "   # the author's wording (review row 31)
            "'Allow Thirds on a normal monitor' to be checked on the Troubleshooting tab; with 'Multiple monitors / ultrawide' on, one wide screen uses it by itself).",
    "Show": "Always = the split screen is up the whole time; Battle = only in battles (one shared camera in the field).",
}
TIPS.update({
    "EnemyHP": "150 = enemies have 50% more health, 75 = a quarter less. 100 = the game's normal. "
               "Bosses keep their normal health bar and take less (or more) damage instead, so their phase changes "
               "happen where the game expects.",
    "StaggerRate": "How fast enemies' stagger gauge fills: 80 = 20% slower to stagger, 120 = faster. 100 = the game's normal.",
    "EnemyDamage": "Damage the party takes from enemies: 150 = 50% more, 75 = a quarter less. 100 = the game's normal.",
    "FocusOnPlayer2": "200 = enemies turn on players 2 / 3 twice as readily. 100 = the game's normal.",
    "WeakTargetHealth": "20 = below a fifth of their health, 50 = below half. Used by 'Target weakened players (chance)' (above).",
    "EnemyChangesApplyNow": "Testing only, for tuning difficulty: changed enemy settings (Save, then F6) and a switch between "
                            "\"Same difficulty\" and \"Dynamic\" take effect in the middle of a battle instead of after it. "
                            "Enemies already on the field keep the health they had; damage, attack rate, stagger and targeting change "
                            "at once. Leave it off for normal play. Default: off.",
    "BossTargetsAITeammates": "Testing only: with one person playing, bosses also rotate their attention to the AI-played "
                              "teammates, as if they were people - to test boss targeting alone. Leave it off for normal play.",
    "BossSpreadNearest": "Testing only, a fallback: bosses that pick their target \"by type\" (Eligor) already spread out when "
                         "the mod can tell they mean player 1. This is only for the rare pick where it can't tell: on = rotate it "
                         "between the people playing too, if that boss never targets the leader. Off = the game's own choice.",
    "EnemiesTreatHumansLikePlayer1": "The game pays less attention to AI-run party members. On = characters played by a person "
                                     "count like player 1's when enemies pick a target - bosses included (their script otherwise always "
                                     "goes after player 1). For all party sizes.",
    "DangerPerScreen": "With the split screen on, each player's view gets its own red edge when THEIR character is low on "
                       "health, instead of the game's red filter over the whole screen. Off = the game's own.\n"
                       "Without the split screen the game's own filter is used.",
    "MenuDarken": "With one shared screen (split screen off, co-op camera) the whole screen darkens while anyone's command menu "
                  "is open, like the game. Off = it stays as bright as normal. With the split screen, each player's own box decides.",
    "MenuDarkenSplitPlayer1": "With the split screen, only this player's own view goes darker while their command menu is open. "
                              "Off = their view stays as bright as normal. With one shared screen, the \"Shared screen\" box decides.",
    "ReloadKey": "Press it outside the inventory menu - in the game's menus F6 also sorts your inventory.",   # round 2: moved from the hot swap note
    "KeyboardLeaderKeepsLead": "The keyboard always controls Player 1 - so the swap won't work correctly if allowed.",   # the author's wording (review row 5)
    "ShowClock": "The PC's time (hours:minutes:seconds) in the top right corner - the same clock the log uses, so you can "
                 "note when something happened.",
    "MessageLogX": "The messages when an enemy is defeated, items are received from chests, etc. sit at the right edge of the screen.",   # the author's wording (review rows 40 / 41)
    "MessageLogY": "0 = the game's height.",   # round 2
    "JoinAtStart": "Auto = already in when the game starts only if their controller is picked by name / model (not 'Auto') in "
                   "the Controller list. 1 = always in, 0 = out until they join (key / button).",
    "AIGuardCopy": "Lightly - never instead of their own attacks or dodges.",
    "Height": "How high above the player the window camera sits.",
    "FOV": "Wider = sees more, smaller characters.",
    "CameraSpeed": "How fast the right stick turns this player's window camera.",
    "DynamicQuality": "The windows get plainer by themselves when the frame rate drops below the target, and sharper again "
                      "when there is room. Pick what it may lower (and in which order) under 'Advanced'.",
    "DynamicTargetFps": "Set it a little below the game's own frame rate limit, so it has room to step back up.\n"
                        "If this is set higher than the game's own frame rate setting, dynamic quality is turned off.",
    "ThirdsLayout": "Three columns side by side (player 1 | player 2 | player 3) on a normal 16:9 monitor - very narrow there.\n"
                    "With 'Multiple monitors / ultrawide' on (Split screen > Experimental), one wide screen (an ultrawide, or the wide-screen test) uses thirds with three players by itself.",
    "MultiMonitor": "The game window spans all your monitors (side by side, same height): player 1 gets their own monitor,\n"
                    "players 2 and 3 the others (left to right). Menus and the pause screen go to player 1's monitor; cutscenes\n"
                    "show on player 1's monitor with the others black. Two monitors tested; three monitors not tested yet.\n"
                    "Monitor sizes come from Windows. Also for ONE ultrawide monitor (wider than 2.2:1, e.g. 32:9 / 21:9): the game renders\n"
                    "at the monitor's full size and the split screen uses halves / thirds of it (untested on a real ultrawide). Experimental.",
    "MultiMonitorAsOneScreen": "For testing the ultrawide layout on several monitors: the spanned picture is treated as one wide screen -\n"
                               "player 1 | player 2 (halves), or player 1 | player 2 | player 3 (thirds). Leave off for normal multi-monitor play.",
    "MonitorPlayer1": "Which monitor is player 1's. Main = the Windows main display. The others go to players 2 and 3, left to right.",
    "MonitorShare": "With two monitors and three players, players 2 and 3 share the second monitor: Columns = side by side,\n"
                    "Rows = one above the other.",
    "WindowsForAI": "Testing only: players 2 / 3's windows show even when nobody has joined, following the AI-played "
                    "character. The split screen itself must be on (F7).\nPlayer 3 needs a controller set up (Player 3 tab) for their window.",
})
for _k in ("Player2Rates", "Player3Rates"):
    TIPS[_k] = TIPS["Player1Rates"]
for _k in ("MenuSlowdownPlayer2", "MenuSlowdownPlayer3"):
    TIPS[_k] = TIPS["MenuSlowdown"]
for _k in ("MenuDarkenSplitPlayer2", "MenuDarkenSplitPlayer3"):   # f1.208
    TIPS[_k] = TIPS["MenuDarkenSplitPlayer1"]
METRE_KEYS = ("BaseZoomOut", "MaxDistance", "TiltStartSpread", "TiltFullSpread", "EnemyRadius", "CutsceneDistance",   # = CAMERA_METRES
              "WindowDistance", "WindowHeight")   # + the window cameras (round 2)


def _lookup(table, section, key):
    k2 = key[6:] if key.startswith("Window") and len(key) > 6 else key
    for k in ("%s.%s" % (section, key), key, k2):
        if k in table:
            return table[k]
    return None


def short_label(section, key, label):
    return _lookup(SHORT, section, key) or label


DEFAULT_UNITS = {"AutoFollowDelay": " seconds", "StatusCirclesRight": " px", "StatusCirclesTop": " px"}   # "Default: 3 seconds" (the author's wording)


def field_tip(section, key, label, kind, default, extra):
    """The tooltip for one setting: what it does (the long label + extra tips), default, restart note."""
    desc = label.replace("  *", "").strip()
    if desc.startswith("..."):
        desc = desc[3:]
    desc = desc[:1].upper() + desc[1:]
    # round 2 (the author OK'd): a first line that only repeats the on-screen label is left out
    same = desc.rstrip(" .:").lower() == short_label(section, key, label).replace("  *", "").rstrip(" .:").lower()
    lines = [] if same else [desc]
    more = _lookup(TIPS, section, key)
    if more:
        lines.append(more)
    metres = key in METRE_KEYS
    def shown(v):
        if metres:
            try:
                return "%s m" % _rng(float(v) / 100.0)
            except ValueError:
                return v
        return v
    if kind == "bool":
        lines.append("Default: %s" % ("on" if str(default) == "1" else "off"))
    elif kind == "num" and extra:
        lines.append("Default: %s%s" % (shown(default), DEFAULT_UNITS.get(key, "")))   # (the allowed range is beside the box - the author, tooltip review)
    elif kind == "choice":
        lines.append("Default: %s   (choices: %s)" % (default, " / ".join(str(x) for x in extra)))
    elif kind in ("key", "key1"):
        lines.append("Default: %s" % default)   # (app f1.187: "- click Set... and press the new key" dropped - the author)
    if label.rstrip().endswith("*"):
        lines.append("* Takes effect after restarting the game.")
    # (app f1.187, the author: "remove the '- click Set... and press the new key. / Save, then xx in game.' - Don't need
    #  that on every tool tip": the "Save, then F6 in game" endings are gone)
    return "\n".join(lines) if lines else desc


# f1.155: rows built by hand (not from the field lists) - exact on-screen text -> (short text, tooltip)
CUSTOM_ROWS = {
    "Left stick dead zone (0.0 - 0.9)": ("Stick dead zone",
        "How far the left stick must move before the character moves.\nRaise it if the character creeps on its own.\n"
        "Default: 0.24"),
    "Rumble when this player's character is hit (%, 0 = off)": ("Rumble (%)",
        "Controller rumble when this player's character is hit.\n100 = normal, 200 = strongest, 0 = off.\n"
        "Default: 100"),
    "Keyboard key to join / leave": ("Join / leave key",
        "The keyboard key that joins / leaves this player.\n"   # the author's wording (review row 24); round 2: this player's key
        "Default: F10 for player 2, Ctrl+F10 for player 3"),
    "Join / leave by holding a button on this player's controller (the button: Advanced > Joining, below)": (
        "Join / leave by holding a button",
        "This player can join / leave by holding a designated button on their own controller for one second "   # the author's wording (review row 22)
        "(default button: Start).\nDefault: on"),
    "If the controller disconnects, the AI takes over until it's back": ("AI takes over if the controller drops",
        "If this player's controller disconnects, after 3 seconds the AI is given control of their character. Control is "   # the author's wording (review row 21)
        "returned to the player when the controller is again detected.\nDefault: on"),
    "Join when the game starts  *": ("Join P2 / P3 when the game starts  *",   # the author's wording
        "Auto: if controllers are connected, P2 (and P3) start as joined players, and a controller connected later joins "   # round 2
        "automatically.\nOff: P2 / P3 join with F10 / Ctrl+F10, or by holding Start for 1 second.\nDefault: Auto\n"
        "* Takes effect after restarting the game."),
    # app f1.189 (review row #64): the split screen's own row - it shared the joining row's text above and showed its tooltip
    "Split screen when the game starts  *": ("Split screen at start  *",   # review row 64 (the author OK'd the short text)
        "How the split screen starts when the game launches.\nDefault: Battle only\n* Takes effect after restarting the game."),
}


def compact_notes(root):
    """f1.155: every long gray explanation under a box becomes one short line ("ⓘ" + its first sentence); the whole text
    shows when the mouse is over it. Labels marked .keep_note (their text changes while the app runs) are left alone."""
    stack = [root]
    while stack:
        w = stack.pop()
        stack.extend(w.winfo_children())
        if isinstance(w, (ttk.Label, ttk.Checkbutton)):
            try:
                cur = str(w.cget("text"))
            except tk.TclError:
                cur = ""
            if cur in CUSTOM_ROWS:   # a hand-built row: short text + tooltip
                w.configure(text=CUSTOM_ROWS[cur][0])
                Tooltip(w, CUSTOM_ROWS[cur][1])
                continue
        if not isinstance(w, ttk.Label) or getattr(w, "keep_note", False):
            continue
        try:
            fg = str(w.cget("foreground"))
            text = str(w.cget("text"))
        except tk.TclError:
            continue
        if fg != "#888" or (len(text) <= 70 and "\n" not in text):
            continue
        flat = " ".join(text.split())
        first = text.strip().split("\n")[0].strip()   # its first line / sentence on screen
        j = first.find(". ")
        if j > 0:
            first = first[:j + 1]
        if len(first) > 80:
            first = first[:77].rsplit(" ", 1)[0] + "..."
        w.configure(text="\u24d8  " + first + ("   (hover for more)" if first != flat else ""), cursor="question_arrow", wraplength=0)
        Tooltip(w, text)


class BigCheck(ttk.Frame):
    """f1.39o (the author: "too easy to miss"): a large check box - a drawn 26 px box and big bold text.
    Clicking the box or the text (or Space when it has focus) flips the "1" / "0" variable and calls command."""
    def __init__(self, master, text, var, command=None, size=26):
        super().__init__(master)
        self.var, self.command, self.size = var, command, size
        bg = ttk.Style().lookup("TFrame", "background") or "#f0f0f0"
        self.box = tk.Canvas(self, width=size, height=size, highlightthickness=0, bg=bg, cursor="hand2", takefocus=1)
        self.box.pack(side="left")
        self.text = ttk.Label(self, text=text, font=("Segoe UI", 13, "bold"), cursor="hand2")
        self.text.pack(side="left", padx=(8, 0))
        for w in (self, self.box, self.text):
            w.bind("<Button-1>", self.flip)
        self.box.bind("<space>", self.flip)
        self.box.bind("<FocusIn>", lambda e: self.draw())
        self.box.bind("<FocusOut>", lambda e: self.draw())
        var.trace_add("write", lambda *a: self.draw())
        self.draw()

    def flip(self, e=None):
        self.box.focus_set()
        self.var.set("0" if self.var.get() == "1" else "1")
        if self.command:
            self.command()

    def draw(self):
        c, n = self.box, self.size
        c.delete("all")
        on = self.var.get() == "1"
        try:
            focus = c.focus_get() is c
        except Exception:   # focus_get can fail while a drop-down list is open
            focus = False
        c.create_rectangle(2, 2, n - 2, n - 2, width=3 if focus else 2,
                           outline="#0a64c8" if (on or focus) else "#555", fill="#0a64c8" if on else "white")
        if on:
            c.create_line(n * .24, n * .52, n * .42, n * .72, n * .78, n * .30, fill="white", width=4, capstyle="round", joinstyle="round")


class App(tk.Tk):
    def __init__(self, ini_path):
        super().__init__()
        self.title(APP_TITLE)
        self.minsize(700, 500)
        self.geometry("860x760")   # f1.39ae: back to 860 (8 tabs again)
        self.xi = XInput()
        self.joy = Joysticks()   # v8.2
        self.ini = IniFile(ini_path)
        self.vars = {}          # (section, key) -> StringVar
        self.dirty = False
        self.player_ui = {}     # section -> dict of widgets
        self.pads = []          # f1.11: controllers plugged in (Joysticks.scan)
        self.loaded = {}

        top = ttk.Frame(self, padding=(10, 8))
        top.pack(fill="x")
        ttk.Label(top, text="Settings file:").pack(side="left")
        self.path_lbl = ttk.Label(top, text=ini_path, foreground="#555")
        self.path_lbl.pack(side="left", padx=6)
        ttk.Button(top, text="Choose...", command=self.choose_file).pack(side="right")
        prof = ttk.Frame(self, padding=(10, 0, 10, 4))   # f1.39: profiles
        prof.pack(fill="x")
        ttk.Label(prof, text="Profile:").pack(side="left")
        self.prof_choice = tk.StringVar()
        self.prof_combo = ttk.Combobox(prof, textvariable=self.prof_choice, state="readonly", width=34)
        self.prof_combo.pack(side="left", padx=6)
        self.prof_combo.bind("<<ComboboxSelected>>", lambda e: self.profile_show())
        ttk.Button(prof, text="Load", command=self.profile_load).pack(side="left")
        ttk.Button(prof, text="Save as profile...", command=self.profile_save).pack(side="left", padx=6)
        self.prof_del = ttk.Button(prof, text="Delete", command=self.profile_delete)
        self.prof_del.pack(side="left")
        self.prof_desc = ttk.Label(self, text="", foreground="#555", padding=(10, 0, 10, 4), justify="left")
        self.prof_desc.pack(fill="x")
        self.profiles = {}   # shown name -> path (None = the built-in mod defaults)
        ver = ttk.Frame(self, padding=(10, 0, 10, 2))
        ver.pack(fill="x")
        self.ver_lbl = ttk.Label(ver, text="", foreground="#333")
        self.ver_lbl.pack(side="left")
        self.show_version(ini_path)
        note = ttk.Frame(self, padding=(10, 0, 10, 6))
        note.pack(fill="x")
        ttk.Label(note, text="*  Requires game restart.   Everything else: Save, then press the reload key (F6) in game.",
                  foreground="#b00").pack(side="left")

        self.nb = ttk.Notebook(self)
        self.nb.pack(fill="both", expand=True, padx=10)
        self.build_general()
        self.build_player1()
        self.build_player("Player2", "Player 2")
        self.build_player("Player3", "Player 3")
        self.build_controllers()   # f1.11
        self.build_split()
        self.build_camera()
        self.build_balance()
        # f1.274 (the author: "get rid of the experimental tab as there is only one thing on it ... add that option to the split
        # screen tab, and put it in a collapsible dropdown that says experimental"): build_split makes the box
        self.build_troubleshooting()   # f1.104
        compact_notes(self.nb)   # f1.155: long gray notes -> one line + a tooltip

        bottom = ttk.Frame(self, padding=10)
        bottom.pack(fill="x")
        self.status = ttk.Label(bottom, text="")
        self.status.pack(side="left")
        ttk.Button(bottom, text="Close", command=self.on_close).pack(side="right")
        ttk.Button(bottom, text="Save", command=self.save).pack(side="right", padx=6)
        # (app f1.337, the author: "Get rid of it" - the Reload button re-read the file and silently dropped unsaved changes;
        #  Close asks before dropping them, the Profile row has its own Load, a difficulty preset re-applies when picked again)
        self.protocol("WM_DELETE_WINDOW", self.on_close)

        self.reload()
        self.profile_list()
        self.after(100, self.poll_pads)

    def show_version(self, ini_path):
        mv = installed_mod_version(ini_path)
        self.ver_lbl.config(text="Mod version installed: %s      Settings app version: %s" % (mv or "not found (xinput1_3.dll isn't next to the settings file)", APP_VERSION))

    # ---- helpers ----
    def var(self, section, key, default):
        v = self.vars.get((section, key))
        if v is None:
            v = tk.StringVar(value=default)
            v.default = default
            v.trace_add("write", lambda *a: self.mark_dirty())
            self.vars[(section, key)] = v
        return v

    def mark_dirty(self):
        if not getattr(self, "_loading", False):
            self.dirty = True
            self.status.config(text="Unsaved changes")
            self.check_conflicts()

    def scroll_tab(self, title):
        outer = ttk.Frame(self.nb)
        self.nb.add(outer, text=title)
        canvas = tk.Canvas(outer, highlightthickness=0)
        sb = ttk.Scrollbar(outer, orient="vertical", command=canvas.yview)
        inner = ttk.Frame(canvas, padding=10)
        inner.bind("<Configure>", lambda e: canvas.configure(scrollregion=canvas.bbox("all")))
        win = canvas.create_window((0, 0), window=inner, anchor="nw")
        canvas.bind("<Configure>", lambda e: canvas.itemconfigure(win, width=e.width))
        canvas.configure(yscrollcommand=sb.set)
        canvas.pack(side="left", fill="both", expand=True)
        sb.pack(side="right", fill="y")
        canvas.bind("<Enter>", lambda e: canvas.bind_all("<MouseWheel>", lambda ev: canvas.yview_scroll(int(-ev.delta / 120), "units")))
        canvas.bind("<Leave>", lambda e: canvas.unbind_all("<MouseWheel>"))
        return inner

    def field_rows(self, parent, section, fields, row0=0):
        r = row0
        for key, label, kind, default, extra in fields:
            v = self.var(section, key, default)
            text = short_label(section, key, label)
            tt = field_tip(section, key, label, kind, default, extra)   # f1.155: the full explanation in a tooltip
            if kind == "bool":
                cb = ttk.Checkbutton(parent, text=text, variable=v, onvalue="1", offvalue="0")
                cb.grid(row=r, column=0, columnspan=3, sticky="w", pady=1)
                tip(cb, tt)
            elif kind == "num":
                self.__dict__.setdefault("num_fields", {})[(section, key)] = (text, extra)   # app f1.333 (code review #4): every number box is checked
                tip(ttk.Label(parent, text=text), tt).grid(row=r, column=0, sticky="w", pady=1)
                lo, hi = extra
                inc = 0.05 if hi <= 5 else (1 if hi <= 1000 else 50)
                sp = ttk.Spinbox(parent, from_=lo, to=hi, increment=inc, textvariable=v, width=9)
                sp.grid(row=r, column=1, sticky="w", padx=6)
                tip(sp, tt)
                ttk.Label(parent, text="%s - %s" % (_rng(lo), _rng(hi)), foreground="#888").grid(row=r, column=2, sticky="w")   # the allowed range beside the box (the author, tooltip review)
            elif kind == "choice":
                tip(ttk.Label(parent, text=text), tt).grid(row=r, column=0, sticky="w", pady=1)
                tip(ttk.Combobox(parent, textvariable=v, values=extra, state="readonly", width=12), tt).grid(row=r, column=1, sticky="w", padx=6)
            elif kind in ("key", "key1"):   # f1.22: key1 = no Ctrl / Shift / Alt (the mod reads ReloadKey / HelpKey without them)
                tip(ttk.Label(parent, text=text), tt).grid(row=r, column=0, sticky="w", pady=1)
                tip(ttk.Label(parent, textvariable=v, width=14, relief="sunken", anchor="center"), tt).grid(row=r, column=1, sticky="w", padx=6)
                ttk.Button(parent, text="Set...", width=7,
                           command=lambda v=v, l=label, m=(kind == "key"): self.capture_key(v, l, m)).grid(row=r, column=2, sticky="w")
            r += 1
        parent.columnconfigure(0, weight=1)
        return r

    # ---- tabs made of setting groups ----
    def group(self, tab, title, section, fields, keys, note=None, first=False):
        """A labelled box with the listed settings (in this order) of one ini section."""
        by = {f[0]: f for f in fields}
        box = ttk.LabelFrame(tab, text=title, padding=8)
        box.pack(fill="x", pady=(0 if first else 10, 0))
        r = self.field_rows(box, section, [by[k] for k in keys], row0=1)   # row 0: room for a preset row
        if note:
            ttk.Label(box, text=note, foreground="#888", justify="left").grid(row=r, column=0, columnspan=3, sticky="w", pady=(4, 0))
        return box

    def preset_row(self, box, label, var, values, cmd, width=30):
        pr = ttk.Frame(box)
        pr.grid(row=0, column=0, columnspan=3, sticky="w", pady=(0, 6))
        ttk.Label(pr, text=label).pack(side="left")
        cb = ttk.Combobox(pr, textvariable=var, values=values, state="readonly", width=width)
        cb.pack(side="left", padx=6)
        cb.bind("<<ComboboxSelected>>", lambda e: cmd())

    def build_experimental_box(self, tab):
        # f1.69 (the author: an "experimental" option, off by default); f1.274: a closed + / - box on the Split screen tab (was
        # its own tab - the author: "get rid of the experimental tab as there is only one thing on it")
        body = self.collapsible(tab, "Experimental - not fully tested (off unless you switch it on)")
        by = {f[0]: f for f in PIP}
        r = self.field_rows(body, "PictureInPicture", [by[k] for k in ["ThirdsCommandNudge", "ThirdsPanelNudge"]])   # f1.84; app f1.327: ThirdsLayout -> Troubleshooting (the author: "Put that in troubleshooting instead of removing it altogether?")
        ttk.Label(body, text="With 'Multiple monitors / ultrawide' on, thirds (three columns) is used by itself on one wide screen with three players. The two \"move\" values push\n"
                             "each column's command HUD to the left and its health / ATB panel to the right (Thirds only).",
                  foreground="#888", justify="left").grid(row=r, column=0, columnspan=3, sticky="w", pady=(4, 0))
        # f1.311: multiple monitors (its own rows under Thirds)
        mm = ttk.Frame(body); mm.grid(row=r + 1, column=0, columnspan=3, sticky="w", pady=(12, 0))
        r2 = self.field_rows(mm, "PictureInPicture", [by[k] for k in ["MultiMonitor", "MonitorPlayer1", "MonitorShare", "MultiMonitorAsOneScreen"]])
        ttk.Label(mm, text="Multiple monitors: the game's own window mode should be Borderless; the mod widens it over every monitor.\n"
                           "Known issue: a short flash when a menu / the pause screen opens or closes. Takes effect after F6 in game or a restart.",
                  foreground="#888", justify="left").grid(row=r2, column=0, columnspan=3, sticky="w", pady=(4, 0))

    def build_troubleshooting(self):
        # f1.104 (the author: "For the purposes of testing only (because I don't always have multiple players with me) can the
        # windows be set to be on even when the characters are controlled by AI? ... put it in the troubleshooting settings
        # menu (default off)"). The planned Troubleshooting tab (Detailed log, fallback switches) starts with this.
        tab = self.scroll_tab("Troubleshooting")
        ttk.Label(tab, text="Troubleshooting and testing options. Leave them off for normal play.",
                  foreground="#b00").pack(anchor="w", pady=(0, 6))
        self.group(tab, "Testing", "PictureInPicture", PIP, ["WindowsForAI"], first=True)   # round 2: the note repeated the tooltip
        self.group(tab, "Split screen - thirds on a normal monitor", "PictureInPicture", PIP, ["ThirdsLayout"])   # app f1.327 (the author: thirds "is really only for widescreen ... Put that in troubleshooting")
        self.group(tab, "Testing - difficulty", "General", GENERAL, ["EnemyChangesApplyNow"])   # f1.274 (the author: "Yeah, put it in troubleshooting")
        self.group(tab, "Testing - bosses", "General", GENERAL, ["BossTargetsAITeammates", "BossSpreadNearest"])   # f1.224: BossSpreadNearest   # f1.213 (the author: "for the purpose of testing have the game treat those AI characters like humans")
        # f1.118 (the author: "make a clock active that I can see. I've been asked for time stamps ... It should just be a toggle in
        # the troubleshooting part of the settings.")
        # f1.148 (the author: "move the FPS counter toggle to troubleshooting tab in the settings app")
        self.group(tab, "Frame rate", "General", GENERAL, ["ShowFPS"])   # round 2: the note repeated the tooltip
        self.group(tab, "Clock", "General", GENERAL, ["ShowClock"])   # round 2: the note repeated the tooltip
        # f1.200 (the author, public beta): the detailed log is always on - the DetailedLog check box is gone
        lg = ttk.LabelFrame(tab, text="Log", padding=8)
        lg.pack(fill="x", pady=(10, 0))
        ttk.Label(lg, text="The mod writes FF7RCoopMod_Coop.log next to the game. To report a problem, send that file.\n"
                           "The log never records which keys you type, and the mod sends nothing over the internet.",
                  foreground="#555", justify="left").pack(anchor="w")

    def build_general(self):
        tab = self.scroll_tab("General")
        self.group(tab, "Keys", "General", GENERAL, ["HelpKey", "LeadKeyPlayer1", "LeadKeyPlayer2", "LeadKeyPlayer3", "KeyboardLeaderKeepsLead", "ReloadKey"], first=True,
                   note="Hot swap: The leader trades places with the corresponding player via button press. Example: Player 1 is leading\n"
                        "as Cloud. Ctrl+F9 is pressed on the keyboard. Player 1 swaps places with Player 2. Player 2 now controls Cloud,\n"
                        "and has control of field interactions with chests, doors, etc. Player 1 takes control of the character previously\n"
                        "controlled by Player 2 (ex: Barret).\n\n"
                        "The keyboard always drives the leader. \"Disallow hot swap when Player 1 is using the keyboard\" stops a\n"
                        "keyboard leader from handing over the lead (they would only watch the AI play their new character).")   # the author's wording (review row 16); round 2: the F6 line moved to the Reload settings key tooltip
                   # f1.39f: the author's wording; f1.80: KeyboardLeaderKeepsLead moved here; f1.90: F6 note (the author 2026-10-22)
        self.gen_warn = ttk.Label(tab, text="", foreground="#c00", justify="left")   # f1.22: keyboard keys used twice
        self.gen_warn.pack(anchor="w", pady=(4, 0))
        self.group(tab, "On screen", "General", GENERAL,
                   ["ShowTargetMarker", "ShowStatusCircles",
                    "StatusCirclesRight", "StatusCirclesTop", "StatusCirclesSize"])
        # (f1.125: the f1.124 "Knock-outs" box (KoKeepLeader) is gone - the author: "This should never be the case. It is game
        #  breaking without the new behavior." It is always on.)
        # f1.39ae (the author: "the whole command menus thing could just be on the General tab"; "all these options are not really
        # options"): only the style is a choice. MenusWithoutSplitScreen, Player1MenuInOwnView, OwnMenuPaging, OwnMenuAllyCommands,
        # OwnMenuTargetCursor, HideP1ReticleInWindows, ReserveQueuedItems and MenuSpacing are no longer in the app (keys stay in
        # the file as they are; fixing them in the DLL = removal checklist)
        # f1.59: the "Command menus" box (Individual / Shared menu) is gone - everyone always has their own menu

    # f1.39e: [PlayerN] ButtonIcons - the button icons the game shows that player (the author 2026-10-20: "a setting that
    # forces a particular style of button icons in the game for each controller"). f1.88: the DLL reads it for players 2/3
    # (their menus / palettes); player 1's icons are still the game's own choice. f1.89: Auto stays the default; Default (original game behavior) added.
    def player_color_box(self, tab, section):
        """f1.136: this player's color - their reticle, badge, off-screen pointer and window frame.
        f1.156 (the author: "P2 and P3 should be disallowed from selecting the same reticle color"): the other player's color
        (incl. their Default) is left out of this list, and Save refuses a clash (a hand-edited file)."""
        box = ttk.LabelFrame(tab, text="Color", padding=8)
        box.pack(fill="x", pady=(10, 0))
        dflt = "Orange" if section == "Player3" else "Green"
        other = "Player2" if section == "Player3" else "Player3"
        var = self.var(section, "Color", "Default")
        ovar = self.var(other, "Color", "Default")
        shown = tk.StringVar(value="")
        names = ["Default (%s)" % dflt.lower()] + [n for n, _ in PLAYER_COLORS]
        ttk.Label(box, text="This player's color").grid(row=0, column=0, sticky="w")
        cb = ttk.Combobox(box, textvariable=shown, values=names, state="readonly", width=20)
        cb.grid(row=0, column=1, sticky="w", padx=6)
        tip(cb, "Sets this player's color: their target reticle, joined status indicator, off-screen pointer and window frame.\n"
                "Note: Players 2 and 3 can't select the same color - the other player's color isn't in the list.\n"   # the author's wording (review row 18)
                "Default: %s" % dflt.lower())
        sw = tk.Label(box, text="      ", relief="solid", borderwidth=1)
        sw.grid(row=0, column=2, sticky="w")
        hexof = dict(PLAYER_COLORS)
        def picked(e=None):
            s_ = shown.get()
            var.set("Default" if s_.startswith("Default") else s_)
        def show(*a):
            v = var.get().strip()
            name = next((n for n, _ in PLAYER_COLORS if n.lower() == v.lower()), None)
            shown.set(name or names[0])
            sw.config(bg=hexof[name or dflt])
            taken = player_color(other, ovar.get())   # the other player's color is not offered here
            cb["values"] = [n for n in names if (dflt if n.startswith("Default") else n) != taken]
        cb.bind("<<ComboboxSelected>>", picked)
        var.trace_add("write", show)
        ovar.trace_add("write", show)
        show()
        # (round 2: the gray note under the color removed - the drop-down's tooltip says it)

    def button_icons_row(self, parent, section, row):
        ttk.Label(parent, text="Button icons in the game").grid(row=row, column=0, sticky="w", pady=(8, 0))
        var = self.var(section, "ButtonIcons", "Auto")
        shown = tk.StringVar(value="")
        cb = ttk.Combobox(parent, textvariable=shown, values=[t for t, _ in BUTTON_ICONS], state="readonly", width=32)
        cb.grid(row=row, column=1, sticky="w", padx=6, pady=(8, 0))
        def picked(e=None):   # f1.89: the shown text -> the value in the file
            for t, v in BUTTON_ICONS:
                if t == shown.get():
                    var.set(v)
        def show(*a):         # the value in the file -> the shown text (unknown = Auto, as the mod reads it)
            v = var.get().strip().lower()
            shown.set(next((t for t, fv in BUTTON_ICONS if v == fv.lower()), "Default (original game behavior)" if v.startswith("default") else "Auto"))
        cb.bind("<<ComboboxSelected>>", picked)
        var.trace_add("write", show)
        show()
        note = ("Auto: the mod applies PlayStation icons for a PlayStation controller, Xbox letters for any other. PlayStation / "   # the author's wording (review row 28), round 2: names the choice
                "Xbox: those icons, whatever the controller. Default: the game's own icons (the ones for player 1's controller or keyboard).")
        ttk.Label(parent, text=note, foreground="#888", justify="left", wraplength=560).grid(row=row + 1, column=0, columnspan=2, sticky="w")
        return cb

    def build_player1(self):
        tab = self.scroll_tab("Player 1")
        # f1.11: the controller is picked by its model ([Input2] Seat1); the keyboard always works for player 1
        self.p1_ui = {"device_text": tk.StringVar(), "map": {}, "uses": tk.StringVar(value="1"), "last": "Auto"}
        BigCheck(tab, "Player 1 uses a controller  *", self.p1_ui["uses"],
                 self.p1_uses_changed).pack(anchor="w", pady=(2, 8))   # f1.39n (the author): off = keyboard only ([Input2] Seat1=Keyboard); f1.39o: big, hard to miss
        d = ttk.LabelFrame(tab, text="Controller  *", padding=8)
        d.pack(fill="x")
        self.p1_ui["combo"] = ttk.Combobox(d, textvariable=self.p1_ui["device_text"], state="readonly", width=58)
        self.p1_ui["combo"].grid(row=0, column=0, sticky="w")
        self.p1_ui["combo"].bind("<<ComboboxSelected>>", lambda e: self.device_chosen("Player1"))
        ttk.Button(d, text="Refresh", command=self.refresh_devices).grid(row=0, column=1, padx=6)
        self.p1_ui["live"] = ttk.Label(d, text="", foreground="#2a7")
        self.p1_ui["live"].grid(row=1, column=0, columnspan=2, sticky="w", pady=(6, 0))
        self.p1_ui["note"] = ttk.Label(d, text=P1_NOTE_PAD, foreground="#888", justify="left")   # f1.39p: shorter when keyboard only
        self.p1_ui["note"].keep_note = True   # f1.155: its text changes with "Player 1 uses a controller" - not compacted
        self.p1_ui["note"].grid(row=2, column=0, columnspan=2, sticky="w")
        self.p1_ui["warn"] = ttk.Label(d, text="", foreground="#c00", justify="left")
        self.p1_ui["warn"].grid(row=3, column=0, columnspan=2, sticky="w")
        # round 2 (the author OK'd): no button-icons row on Player 1's tab - the game picks player 1's icons itself
        self.seat_var("Player1")
        # f1.39p (the author): the "Character" box (only "Name shown on screen") is gone - names aren't used
        bt = ttk.LabelFrame(tab, text="Buttons", padding=8)   # f1.39j
        bt.pack(fill="x", pady=(10, 0))
        ttk.Label(bt, text="Player 1's buttons and keys are the game's own - change them in the game's Options\n"
                           "(Controller Settings / Mouse & Keyboard Settings).", foreground="#555", justify="left").grid(row=0, column=0, sticky="w")

    def build_split(self):
        tab = self.scroll_tab("Split screen")
        ttk.Label(tab, text="Players 2 and 3 each get their own view of the game. This costs frame rate - lower the quality if your PC struggles.",
                  foreground="#555").pack(anchor="w", pady=(0, 8))
        lay = self.group(tab, "Default screen layout", "PictureInPicture", PIP, ["Side", "ToggleKey", "LayoutKey"], first=True,   # the author 2026-10-26 (was "Layout")
                         note="Thirds: by itself on one wide screen (with 'Multiple monitors / ultrawide' on); on a normal monitor it needs 'Allow Thirds on a normal monitor' (Troubleshooting tab), otherwise it acts as Right.\n"   # round 2: shorter; app f1.327
                              "Until player 3 joins, the screen is split in half (player 1 left, player 2 right).")   # f1.80; f1.83 known issue (the author)
        # v11.9: one choice instead of 'Enabled' + 'Show' (the same three states F7 cycles through)
        self.split_start = tk.StringVar(value="")
        ttk.Label(lay, text="Split screen when the game starts  *").grid(row=0, column=0, sticky="w", pady=1)   # app f1.189: own CUSTOM_ROWS key (it showed the joining tooltip)
        sc = ttk.Combobox(lay, textvariable=self.split_start, values=list(SPLIT_START), state="readonly", width=24)
        sc.grid(row=0, column=1, columnspan=2, sticky="w", padx=6)
        sc.bind("<<ComboboxSelected>>", lambda e: self.split_start_chosen())
        self.var("PictureInPicture", "Enabled", "1"); self.var("PictureInPicture", "Show", "Battle")
        self.build_experimental_box(tab)   # f1.274: was the Experimental tab
        self.build_party_panel(tab)   # f1.117
        self.build_cmd_scale(tab)   # f1.145
        self.build_message_log(tab)   # f1.161
        # f1.39u/v (the author): every window camera setting per player, in + / - boxes (closed at start)
        wc = ttk.LabelFrame(tab, text="Window cameras", padding=8)
        wc.pack(fill="x", pady=(10, 0))
        ttk.Label(wc, text="Each player's own window camera - open a box to change it.", foreground="#888").pack(anchor="w")
        # f1.39w (the author): "apply Player 2's settings to player 3" - player 3's box grayed out, player 2's settings drive both
        # windows ([Player3] WindowSameAsPlayer2=1; the app copies player 2's values into player 3's keys, so the DLL needs nothing)
        self.win_same = self.var("Player3", "WindowSameAsPlayer2", "1")
        BigCheck(wc, "Player 2 and Player 3's cameras use the same values", self.win_same,   # f1.274b: the author's wording (was "Apply player 2's settings to player 3")
                 self.window_same_changed).pack(anchor="w", pady=(8, 4))   # f1.39x (the author): big, like player 1's check box
        self.win_bodies = {}
        for sec in ("Player2", "Player3"):
            body = self.collapsible(wc, "Player %s's window camera" % sec[-1])
            self.win_bodies[sec] = body
            r = 0
            for fl in WINDOW_FIELDS:   # round 2 (the author OK'd): distance / height in meters (cm in the file), like the co-op camera
                if fl[0] in ("WindowDistance", "WindowHeight"):
                    self.metres_row(body, r, fl[1], self.var(sec, fl[0], fl[3]), fl[4][0], fl[4][1], section=sec, fld=fl, step=0.1)
                    r += 1
                else:
                    r = self.field_rows(body, sec, [fl], row0=r)
        for f in WINDOW_FIELDS:
            self.var("Player2", f[0], f[3]).trace_add("write", self.window_copy_p2)
        self.window_same_changed()
        # f1.39u (the author): the "HUD over the windows" box is gone - the game's own HUD only ([PictureInPicture] FullSizeHud /
        # HudCopy / PartyRowsAtTop / HudCopyFont and Resolution - f1.56: the mod no longer reads them, the HUD copy is gone)
        # f1.39u (the author: "Do we need a separate brightness setting only for the windows?"): the "Picture" box is gone too
        # (Brightness / TemporalAA / GammaFix stay in the file - 100 / on / on)
        q = ttk.LabelFrame(tab, text="Quality and speed", padding=8)
        q.pack(fill="x", pady=(10, 0))
        q.columnconfigure(0, weight=1)
        self.choice_map_row(q, 9, "Window sharpness", self.var("PictureInPicture", "FullSizeHudQuality", "100"), SHARP_CHOICES)   # f1.39z (the author)
        # f1.39y (the author): Detail and draw distance as plain-language choices (same numbers as before in the file)
        self.choice_map_row(q, 10, "Model detail in the windows", self.var("PictureInPicture", "Detail", "1"), DETAIL_CHOICES)
        self.choice_map_row(q, 11, "Draw distance in the windows", self.var("PictureInPicture", "ViewDistance", "0"), VIEWDIST_CHOICES)
        ttk.Label(q, text="Sharpness: how many pixels are drawn for each window - lower is softer but faster.\n"
                          "Model detail: further down the list, characters and objects in the windows switch to their simpler versions\n"
                          "closer to the camera - plainer but faster. Draw distance: things further away aren't drawn in the windows.",
                  foreground="#888", justify="left").grid(row=12, column=0, columnspan=3, sticky="w", pady=(4, 0))
        self.pip_q = tk.StringVar(value="")
        self.preset_row(q, "Quality setting:", self.pip_q, list(PIP_QUALITY), self.apply_pip_quality, width=24)
        for k, d in (("FullSizeHudQuality", "100"), ("Detail", "1"), ("ViewDistance", "0")):   # app f1.330: Ultra default   # f1.98 (E13): show Custom as soon as one changes
            self.var("PictureInPicture", k, d).trace_add("write", lambda *a: self.pip_quality_show())
        self.build_dynamic_quality(tab)   # f1.113

    def build_cmd_scale(self, tab):
        """f1.145 (the author: "we don't have any resolution size for the left side hud/menu/shortcuts"; defaults 90 / 90 / 75)."""
        b = ttk.LabelFrame(tab, text="Command HUD, menus and shortcuts", padding=8)
        b.pack(fill="x", pady=(10, 0))
        r = self.field_rows(b, "PictureInPicture", [("CommandMenuScale", "Size in a half-screen view (%)", "num", "90", (50, 100)),
                                                    ("CommandMenuScaleThird", "Size in a third-of-the-screen view (%)", "num", "85", (50, 100)),
                                                    ("CommandMenuScaleQuarter", "Size in a quarter-of-the-screen view (%)", "num", "85", (50, 100))])
        ttk.Label(b, text="Resize each player's command HUD, command menus and shortcut palette (bottom left of their part of the\n"   # the author's wording (review row 62)
                          "screen) during split screen. Size can be changed for half, third (Thirds layout) or quarter (players 2\n"
                          "and 3 sharing a half). Smaller keeps them clear of the health / ATB panel. Full screen is always 100%.", foreground="#888", justify="left").grid(row=r, column=0, columnspan=3, sticky="w", pady=(4, 0))

    def build_message_log(self, tab):
        """f1.161 (the author: "the text when an enemy is killed (tells you about their elimination, Gil and XP gained, etc).
        Can we add an option to move that the same way we move the menus? We need to move it to the right some by default.")"""
        b = ttk.LabelFrame(tab, text="Battle messages (enemy defeated, Gil, EXP, items)", padding=8)
        b.pack(fill="x", pady=(10, 0))
        self.field_rows(b, "PictureInPicture", [("MessageLogX", "Move messages right by designated amount of pixels (negative numbers move the messages left)", "num", "0", (-1500, 300)),
                                                    ("MessageLogY", "Move messages down by designated amount of pixels (negative numbers move the messages up)", "num", "0", (-600, 600))])
        # (review row 61: the note under the box removed - the author: redundant with the tooltips above)

    def build_party_panel(self, tab):
        """f1.117 (the author): the party health / ATB panel - the game's one panel (shared) or one per player (individual),
        and with individual: all party members or only that player's character."""
        b = ttk.LabelFrame(tab, text="Health / ATB panel", padding=8)
        b.pack(fill="x", pady=(10, 0))
        b.columnconfigure(0, weight=1)
        mode = self.var("PictureInPicture", "PartyPanel", "Individual")   # app f1.330: the default (the author)
        self.choice_map_row(b, 0, "Party health / ATB panel", mode, PARTY_PANEL_CHOICES, width=26)
        rows_lbl = ttk.Label(b, text="Each player sees")
        rows_lbl.grid(row=1, column=0, sticky="w", pady=1)
        rows = self.var("PictureInPicture", "PartyPanelRows", "OwnAndAI")   # app f1.331 (the author: AI teammates by default); app f1.330 Own
        shown = tk.StringVar(value="")
        rcb = ttk.Combobox(b, textvariable=shown, values=[t for t, _ in PARTY_ROWS_CHOICES], state="readonly", width=34)
        rcb.grid(row=1, column=1, columnspan=2, sticky="w", padx=6)
        def picked(e=None):
            for t, v in PARTY_ROWS_CHOICES:
                if t == shown.get():
                    rows.set(v)
        def show(*a):
            t = next((t for t, v in PARTY_ROWS_CHOICES if v.lower() == rows.get().strip().lower()), PARTY_ROWS_CHOICES[0][0])
            shown.set(t)
        def enable(*a):
            indiv = mode.get().strip().lower() == "individual"
            rcb.state(["!disabled"] if indiv else ["disabled"])
        rcb.bind("<<ComboboxSelected>>", picked)
        rows.trace_add("write", show); mode.trace_add("write", enable)
        show(); enable()
        self.field_rows(b, "PictureInPicture", [("PartyPanelScale", "Panel size in a half-screen view (%)", "num", "90", (50, 100)),   # f1.135 (the author)
                                                ("PartyPanelScaleThird", "Panel size in a third-of-the-screen view (%)", "num", "85", (50, 100)),   # f1.142 (the author:
                                                ("PartyPanelScaleQuarter", "Panel size in a quarter-of-the-screen view (%)", "num", "85", (50, 100))], row0=2)   #   "delineated between 1/2 window, 1/3 window, and 1/4 window")
        ttk.Label(b, text="Shared: The same as the default game - one panel at the bottom right of the monitor.\n"   # the author's wording (review row 63)
                          "Individual: During battles, each player gets the panel in their own part of the screen (bottom right).\n"
                          "This can be customized to show the whole party, their own character + the AI-played teammates, or only\n"
                          "their own character. Panel size: Resizable under the Individual option only, per layout (half, third\n"
                          "in the Thirds layout, or quarter when players 2 and 3 share a half).",
                  foreground="#888", justify="left").grid(row=5, column=0, columnspan=3, sticky="w", pady=(4, 0))
        # (f1.138b's orange known-issue note about the gap removed in f1.141 - the gap is closed now, always)

    def build_dynamic_quality(self, tab):
        """f1.113 (the author): the windows get plainer by themselves when the frame rate drops - on / off + target here, what it
        may change (in which order) and how far in an "advanced" + / - box."""
        d = ttk.LabelFrame(tab, text="Dynamic quality", padding=8)
        d.pack(fill="x", pady=(10, 0))
        by = {f[0]: f for f in PIP}
        top = ttk.Frame(d)   # (grid inside; the advanced box below is packed)
        top.pack(fill="x")
        r = self.field_rows(top, "PictureInPicture", [by["DynamicQuality"], by["DynamicTargetFps"]])
        ttk.Label(top, text="When the frame rate stays below the target for a few seconds, the windows get one step plainer;\n"
                          "when it holds the target again for a while they step back up - never above the quality set above.\n"
                          "Set the target typically 5 - 10 fps LOWER than the game's own frame rate setting (e.g. 50 - 55\n"
                          "with the game at 60), so it has room to step back up.", foreground="#888", justify="left").grid(row=r, column=0, columnspan=3, sticky="w", pady=(4, 0))
        # f1.144 (the author: "if the game FPS is set lower than the value in the settings app, then it is automatically
        # disabled. Make a note of that in the settings app.")
        ttk.Label(top, text="Guardrail: if the game's own frame rate limit is set LOWER than this target, dynamic quality switches\n"
                            "itself off (the target could never be reached) and the windows keep the quality set above. It switches\n"
                            "back on when the game's limit is raised to the target or above (or set to unlimited).",
                  foreground="#b05a00", justify="left").grid(row=r + 1, column=0, columnspan=3, sticky="w", pady=(4, 0))
        body = self.collapsible(d, "Advanced: what it changes, in which order, and how far")
        body.columnconfigure(1, weight=1)
        order = self.var("PictureInPicture", "DynamicOrder", "Detail, DrawDistance, Sharpness, HalfRate")
        names = [t for t, _ in DYN_LEVERS] + [DYN_NOT_USED]
        self.dyn_slots = []
        for i, lbl in enumerate(("Lower first", "Then", "Then", "Last")):
            ttk.Label(body, text=lbl).grid(row=i, column=0, sticky="w", pady=1)
            sv = tk.StringVar(value=DYN_NOT_USED)
            cb = ttk.Combobox(body, textvariable=sv, values=names, state="readonly", width=36)
            cb.grid(row=i, column=1, sticky="w", padx=6)
            cb.bind("<<ComboboxSelected>>", lambda e, i=i: self.dyn_slot_picked(i))
            self.dyn_slots.append(sv)
        def show(*a):
            if getattr(self, "_dyn_writing", False):
                return
            keys = [k.strip().lower() for k in order.get().replace(";", ",").split(",") if k.strip()]
            seen = []
            for k in keys:
                for t, v in DYN_LEVERS:
                    if v.lower() == k and t not in seen:
                        seen.append(t)
            for i, sv in enumerate(self.dyn_slots):
                sv.set(seen[i] if i < len(seen) else DYN_NOT_USED)
        order.trace_add("write", show)
        show()
        self.choice_map_row(body, 5, "Plainest model detail", self.var("PictureInPicture", "DynamicLowestDetail", "4"), DYN_DETAIL_FLOOR)
        self.choice_map_row(body, 6, "Shortest draw distance", self.var("PictureInPicture", "DynamicLowestDrawDistance", "4000"), DYN_DIST_FLOOR)
        self.choice_map_row(body, 7, "Softest sharpness", self.var("PictureInPicture", "DynamicLowestSharpness", "50"), DYN_SHARP_FLOOR)
        ttk.Label(body, text="Pick '(not used)' to keep something as it is. Sharpness rebuilds the window's picture, so each\n"
                             "of its steps shows as a short flicker. Half-rate windows: the windows draw about every other frame -\n"
                             "the biggest saving, a little choppier while it's on.", foreground="#888", justify="left").grid(row=8, column=0, columnspan=3, sticky="w", pady=(4, 0))

    def dyn_slot_picked(self, i):   # f1.113: one lever per slot - picking one that's in another slot moves it here
        picked = self.dyn_slots[i].get()
        for j, sv in enumerate(self.dyn_slots):
            if j != i and sv.get() == picked and picked != DYN_NOT_USED:
                sv.set(DYN_NOT_USED)
        vals = [sv.get() for sv in self.dyn_slots if sv.get() != DYN_NOT_USED]
        text = ", ".join(v for t in vals for tt, v in DYN_LEVERS if tt == t) or "None"   # f1.113 review: empty would mean "all four" to the mod
        self._dyn_writing = True
        try:
            self.var("PictureInPicture", "DynamicOrder", "").set(text)
        finally:
            self._dyn_writing = False

    def window_same_changed(self, *a):   # f1.39w
        if not hasattr(self, "win_bodies"):
            return
        same = self.win_same.get() == "1"
        if same:
            self.window_copy_p2()
        def walk(w):
            for ch in w.winfo_children():
                if isinstance(ch, (ttk.Spinbox, ttk.Combobox, ttk.Checkbutton, ttk.Label)) and not \
                        (isinstance(ch, ttk.Label) and str(ch.cget("foreground"))):   # gray notes / ranges keep their color
                    ch.state(["disabled"] if same else ["!disabled"])
                walk(ch)
        walk(self.win_bodies["Player3"])
        # f1.274b (the author: "the player 3 window camera dropdown would be closed and grayed out ... If it is not checked, then
        # they would each have the available separate drop down"): ticked = player 3's box closed with its + / - grayed out, and
        # player 2's box named for both; unticked = two separate boxes again (player 3 keeps the values just copied)
        b2, b3 = self.win_bodies["Player2"], self.win_bodies["Player3"]
        if same:
            if b3.winfo_manager():
                b3.coll_toggle()
            b3.coll_tog.state(["disabled"]); b3.coll_title.state(["disabled"])
            b2.coll_title.config(text="Player 2 and Player 3's window camera")
        else:
            b3.coll_tog.state(["!disabled"]); b3.coll_title.state(["!disabled"])
            b2.coll_title.config(text="Player 2's window camera")

    def window_copy_p2(self, *a):   # f1.39w: player 2's window camera -> player 3's (while "Apply player 2's settings" is on)
        if getattr(self, "_loading", False) or not hasattr(self, "win_same") or self.win_same.get() != "1":
            return
        for f in WINDOW_FIELDS:
            v2 = self.var("Player2", f[0], f[3]).get()
            v3 = self.var("Player3", f[0], f[3])
            if v3.get() != v2:
                v3.set(v2)

    def choice_map_row(self, parent, row, label, var, choices, width=22):
        """f1.39y: a drop-down of (text, value) pairs for a number setting; a value not in the list shows as 'Custom (x)' and stays."""
        shown = tk.StringVar(value="")
        ttk.Label(parent, text=label).grid(row=row, column=0, sticky="w", pady=1)
        cb = ttk.Combobox(parent, textvariable=shown, values=[t for t, _ in choices], state="readonly", width=width)
        cb.grid(row=row, column=1, columnspan=2, sticky="w", padx=6)
        def picked(e=None):
            for t, v in choices:
                if t == shown.get():
                    var.set(v)
        def show(*a):
            t = next((t for t, v in choices if _num_eq(var.get().strip(), v)), None)
            shown.set(t or "Custom (%s)" % var.get().strip())
        cb.bind("<<ComboboxSelected>>", picked)
        var.trace_add("write", show)
        show()

    def collapsible(self, parent, title, open_=False):
        """f1.39u: a box with a + / - button in its title that shows / hides its body. Returns the body frame (grid inside it)."""
        head = ttk.Frame(parent)
        tog = ttk.Button(head, text="+", width=3)
        tog.pack(side="left")
        lab = ttk.Label(head, text=title)
        lab.pack(side="left", padx=(6, 0))
        box = ttk.LabelFrame(parent, labelwidget=head, padding=(8, 0, 8, 4))
        box.pack(fill="x", pady=(4, 0))
        ttk.Frame(box, height=2).pack(fill="x")   # keeps the closed box its title's height (an empty box shrinks to nothing)
        body = ttk.Frame(box)
        def toggle():
            if body.winfo_manager():
                body.pack_forget(); tog.config(text="+")
            else:
                body.pack(fill="x", pady=(4, 0)); tog.config(text="-")
        tog.config(command=toggle)
        if open_:
            toggle()
        body.coll_tog, body.coll_title, body.coll_toggle = tog, lab, toggle   # f1.274b: the window cameras box closes / renames it
        return body

    def split_start_chosen(self):
        en, show = SPLIT_START[self.split_start.get()]
        self.var("PictureInPicture", "Enabled", "1").set(en)
        self.var("PictureInPicture", "Show", "Battle").set(show)

    def split_start_show(self):
        en = self.var("PictureInPicture", "Enabled", "1").get().strip()
        show = self.var("PictureInPicture", "Show", "Battle").get().strip().lower()
        self.split_start.set("Battle only" if show == "battle" else ("Always on" if en == "1" else "Off"))

    def build_camera(self):
        tab = self.scroll_tab("Co-op camera")
        ttk.Label(tab, text="One shared camera that keeps everyone in view (used when the split screen is off, or in the field with 'Battle only').",
                  foreground="#555").pack(anchor="w", pady=(0, 8))
        c = self.group(tab, "On / off", "Camera", CAMERA, ["Enabled", "ToggleKey"], first=True)
        self.field_rows(c, "General", [f for f in GENERAL if f[0] == "ShowOffscreenArrow"], row0=20)   # f1.39i: [General] key, shown here (only with one shared camera - the author)
        self.field_rows(c, "Camera", [f for f in CAMERA if f[0] == "TalkScenes"], row0=21)   # f1.120 (the author): not a framing number - kept out of the presets
        # f1.39aa (the author's review): the preset shows what the numbers match; the numbers are in a + / - "Fine-tune" box,
        # distances in metres (the file keeps cm)
        self.preset = tk.StringVar(value="")
        f = ttk.LabelFrame(tab, text="Framing", padding=8)
        f.pack(fill="x", pady=(10, 0))
        f.columnconfigure(0, weight=1)
        self.preset_row(f, "Preset:", self.preset, list(CAMERA_PRESETS) + ["Custom"], self.apply_preset)
        ttk.Label(f, text="\n".join("%s: %s." % (n.split(" (")[0], t) for n, t in CAMERA_PRESET_NOTES.items()) +
                  "\nCustom: your own numbers in Fine-tune below.", foreground="#888", justify="left").grid(row=1, column=0, columnspan=3, sticky="w")
        hold = ttk.Frame(f)
        hold.grid(row=2, column=0, columnspan=3, sticky="we", pady=(6, 0))
        body = self.collapsible(hold, "Fine-tune")
        by = {x[0]: x for x in CAMERA}
        r = 0
        for k in CAMERA_FRAMING:
            if k in CAMERA_METRES:
                key, label, kind, d, (lo, hi) = by[k]
                self.metres_row(body, r, label, self.var("Camera", key, d), lo, hi)
                r += 1
            else:
                r = self.field_rows(body, "Camera", [by[k]], row0=r)
        for k in CAMERA_FRAMING:
            self.var("Camera", k, by[k][3]).trace_add("write", lambda *a: self.camera_preset_show())

    def build_balance(self):
        tab = self.scroll_tab("Balance and AI settings")
        # f1.151b (the author: "add a note at the top of enemy AI settings that the changes cannot take effect during a battle")
        ttk.Label(tab, text="Enemy settings never go down during a battle (they go up when a player's character joins). Saved and reloaded\n"
                            "(F6) in the middle of a fight, the enemy settings on this tab wait until the battle is over. Everything else\n"
                            "F6 reloads applies at once.",
                  foreground="#B35C00", justify="left").pack(anchor="w", pady=(0, 8))
        # f1.39ad (the author): difficulty presets on top (built-in + the player's own: Save as preset / Delete)
        d = ttk.LabelFrame(tab, text="Difficulty preset", padding=8)
        d.pack(fill="x")
        row = ttk.Frame(d)
        row.pack(fill="x")
        ttk.Label(row, text="Preset:").pack(side="left")
        self.diff_choice = tk.StringVar(value="")
        self.diff_combo = ttk.Combobox(row, textvariable=self.diff_choice, state="readonly", width=30)
        self.diff_combo.pack(side="left", padx=6)
        self.diff_combo.bind("<<ComboboxSelected>>", lambda e: self.diff_apply())
        ttk.Button(row, text="Save as preset...", command=self.diff_save).pack(side="left", padx=(6, 0))
        self.diff_del = ttk.Button(row, text="Delete", command=self.diff_delete)
        self.diff_del.pack(side="left", padx=6)
        self.diff_desc = ttk.Label(d, text="", foreground="#555", justify="left")
        self.diff_desc.pack(anchor="w", pady=(6, 0))
        ttk.Label(d, text="Change any value below to make it your own (the preset then shows Custom), then Save as preset.\n"
                          "The four built-in presets can't be changed or deleted.", foreground="#888", justify="left").pack(anchor="w", pady=(4, 0))
        self.build_party_sets(tab)   # f1.151: the enemy values, one set per party size
        self.group(tab, "Who enemies go after (every party size)", "Balance", BALANCE, ["EnemiesTreatHumansLikePlayer1"])   # (f1.214: bosses follow it too - BossTargetSpread gone)
        # (f1.275: "Players' ATB speed" moved into each party-size set - build_party_sets)
        # (f1.272: the "Command menus: slowdown and darkening" box moved into each party-size set - build_party_sets)
        self.group(tab, "Screen effects", "General", GENERAL, ["DangerPerScreen", "DangerHealth"])   # f1.105 / f1.106 (the author); f1.208: MenuDarken moved to the command menus box
        # (f1.178: the "Enemy info" box (EnemyHPNumbers / EnemyNameSize) is gone with the drawn target bar - players 2 / 3 get the game's own)
        self.group(tab, "AI teammates", "General", GENERAL, ["AIGuardCopy"])
        for sec, k in DIFFICULTY_KEYS:
            dflt = SET_DEFAULTS.get((sec, k)) or next((f[3] for f in BALANCE if f[0] == k), "1")   # (DynamicDifficulty: not a BALANCE field, default 1)
            self.var(sec, k, dflt).trace_add("write", lambda *a: self.diff_show())
        self.diff_list()

    def build_party_sets(self, tab):
        """f1.151: enemy settings per party size. f1.271 app: three boxes with + / - buttons (the author: "easy to overlook").
        f1.272 (the author): a choice on top - "Same difficulty no matter how many humans are playing" shows ONE Global box
        ([BalanceGlobal], [Balance] DynamicDifficulty=0); "Dynamic difficulty based on human player count" (=1) shows the three.
        Each box also holds the command menus' slowdown and darkening (per set since f1.272)."""
        box = ttk.LabelFrame(tab, text="Enemy and command menu settings by number of players", padding=8)
        box.pack(fill="x", pady=(10, 0))
        mode = self.var("Balance", "DynamicDifficulty", "1")
        # f1.274c (the author: "Should be a dropdown box (more consistent with the rest of the app) and condenses a long screen"):
        # a drop-down instead of the two radio buttons; same key ([Balance] DynamicDifficulty 0 / 1)
        modes = [("0", "Same difficulty no matter how many humans are playing"),
                 ("1", "Dynamic difficulty based on human player count (recommended)")]
        shown = tk.StringVar(value="")
        row = ttk.Frame(box)
        row.pack(fill="x")
        ttk.Label(row, text="Difficulty").pack(side="left")
        cb = ttk.Combobox(row, textvariable=shown, values=[t for _, t in modes], state="readonly", width=58)
        cb.pack(side="left", padx=6)
        tip(cb, "Same difficulty: one set of enemy and command menu settings (Global), used whether 1, 2 or 3 people play.\n"
                "Dynamic difficulty: a set for 1, 2 and 3 people playing. AI teammates don't count: 3 characters with 2 people "
                "playing uses the two character party's settings. Default: Dynamic.")
        cb.bind("<<ComboboxSelected>>", lambda e: mode.set(dict((t, v) for v, t in modes)[shown.get()]))
        def mode_shown(*a):
            shown.set(dict(modes).get("0" if mode.get().strip() == "0" else "1"))
        mode.trace_add("write", mode_shown)
        mode_shown()
        ttk.Label(box, text="In a battle the settings only go UP (a player's character joins the party); going down (a controller\n"
                            "dropping out, a player leaving, F6) waits until the battle is over, so enemies don't get weaker mid-fight.\n"
                            "100 / 0 / Off = the game's normal.",
                  foreground="#888", justify="left").pack(anchor="w", pady=(4, 4))
        dyn = ttk.Frame(box)
        glob = ttk.Frame(box)
        gen = {f_[0]: f_ for f_ in GENERAL}
        self.party_set_frames = {}

        def fill(parent, sec, title):
            by = {f_[0]: f_ for f_ in set_fields(sec)}   # f1.157: per-set defaults
            f = self.collapsible(parent, title, open_=True)
            self.party_set_frames[title] = f
            keys = set_keys(sec)
            self.group(f, "Enemy toughness - %s" % title.lower(), sec, set_fields(sec), SET_TOUGH)
            self.group(f, "Who enemies go after - %s" % title.lower(), sec, set_fields(sec), [k for k in keys if k in SET_TARGET])
            # f1.275 / f1.276: ATB speed per set - the one character party: player 1's only (player 1 alone)
            # f1.280b (the author: "for two character party, why is there a 3rd character option? Is that for two humans and one
            # AI character?" - Claude: the rows are per player, any two of the three can be the two people playing): a note
            per_player = {"Balance2": "One row per player - only the two people playing count.\n"
                                      "For example players 1 and 3 when player 2 isn't in. An AI teammate always uses the game's normal rules.",
                          "Balance3": "One row per player.\nAn AI teammate always uses the game's normal rules.",
                          "BalanceGlobal": "One row per player - only the people playing count.\n"
                                           "An AI teammate always uses the game's normal rules."}.get(sec)
            self.group(f, "Players' ATB speed - %s" % title.lower(), sec, set_fields(sec), ["Player1Rates"] if sec == "Balance1" else SET_ATB,
                       note=per_player)
            m = ttk.LabelFrame(f, text="Command menus: slowdown and darkening - %s" % title.lower(), padding=8)
            m.pack(fill="x", pady=(10, 0))
            people = 1 if sec == "Balance1" else 3   # (one character: nobody else has a menu or a split-screen view)
            r = 1
            for p_ in range(people):
                r = self.field_rows(m, sec, [by[SET_MENU_SLOW[p_]]], row0=r)
                if people > 1:
                    r = self.field_rows(m, sec, [gen[SET_MENU_DARK[p_]]], row0=r)
            r = self.field_rows(m, sec, [gen["MenuDarken"]], row0=r)
            if per_player:
                ttk.Label(m, text=per_player, foreground="#888", justify="left").grid(row=r, column=0, columnspan=3, sticky="w", pady=(4, 0))

        for sec, title in PARTY_SETS:
            fill(dyn, sec, title)
        fill(glob, GLOBAL_SET[0], GLOBAL_SET[1])

        def show(*a):
            same = mode.get().strip() == "0"
            (glob if same else dyn).pack(fill="x")
            (dyn if same else glob).pack_forget()
        mode.trace_add("write", show)
        show()

    # ---- f1.39ad: difficulty presets ----
    def diff_dir(self):
        return os.path.join(os.path.dirname(os.path.abspath(self.ini.path)), DIFF_DIR)

    def diff_list(self, select=None):
        self.diff_presets = {n: {"description": dsc, "values": dict(v), "path": None} for n, (dsc, v) in DIFFICULTY_BUILTIN.items()}
        try:
            files = sorted(f for f in os.listdir(self.diff_dir()) if f.lower().endswith(".ini"))
        except OSError:
            files = []
        for fn in files:
            path = os.path.join(self.diff_dir(), fn)
            try:
                vals, _ = read_profile(path)
            except OSError:
                continue
            name = vals.get(("difficulty", "name")) or fn[:-4]
            if name in DIFFICULTY_BUILTIN:
                continue
            v = {(sec, k): vals[(sec.lower(), k.lower())] for sec, k in DIFFICULTY_KEYS if (sec.lower(), k.lower()) in vals}   # f1.151: per section
            self.diff_presets[name] = {"description": vals.get(("difficulty", "description"), ""), "values": v, "path": path}
        self.diff_show(select)

    def diff_match(self):
        for name, p in self.diff_presets.items():
            if p["values"] and all(_num_eq(self.var(sec, k, "").get().strip(), v) for (sec, k), v in p["values"].items()):
                return name
        return None

    def diff_show(self, select=None):
        if not hasattr(self, "diff_presets"):
            return
        name = select if select in self.diff_presets else self.diff_match()
        names = list(self.diff_presets)
        self.diff_combo["values"] = names + ([] if name else ["Custom"])
        self.diff_choice.set(name or "Custom")
        p = self.diff_presets.get(name)
        self.diff_desc.config(text=(p["description"] or "(no description)") if p else
                              "Your own values - Save as preset to keep them under a name.")
        self.diff_del.state(["!disabled"] if p and p["path"] else ["disabled"])

    def diff_apply(self):
        name = self.diff_choice.get()
        p = self.diff_presets.get(name)
        if not p:
            self.diff_show()
            return
        for (sec, k), v in p["values"].items():   # f1.151: every party-size set too
            self.var(sec, k, v).set(v)
        self.diff_show(name)
        self.status.config(text="Difficulty '%s' applied - press Save to use it." % name)

    def diff_save(self):
        bad = self.validate()
        if bad:
            messagebox.showerror(APP_TITLE, "Please fix these first:\n\n" + "\n".join(bad))
            return
        cur = self.diff_choice.get()
        suggest = cur if cur in self.diff_presets and self.diff_presets[cur]["path"] else ""
        name = simpledialog.askstring(APP_TITLE, "Name for this difficulty preset:", initialvalue=suggest, parent=self)
        if not name or not name.strip():
            return
        name = " ".join(name.replace(";", ",").split())[:60]
        if name.lower() in [n.lower() for n in DIFFICULTY_BUILTIN] or name.lower() == "custom":
            messagebox.showerror(APP_TITLE, "'%s' is a built-in preset name. Please pick another name." % name)
            return
        fn = profile_file_name(name)
        if not fn:
            messagebox.showerror(APP_TITLE, "Please use letters or numbers in the name.")
            return
        path = os.path.join(self.diff_dir(), fn + ".ini")
        old = next((p for p in self.diff_presets.values() if p["path"] and os.path.normcase(p["path"]) == os.path.normcase(path)), None)
        if os.path.isfile(path) and not messagebox.askyesno(APP_TITLE, "A preset called '%s' already exists. Replace it?" % name):
            return
        desc = simpledialog.askstring(APP_TITLE, "A short description (optional):", initialvalue=(old or {}).get("description", ""), parent=self)
        if desc is None:
            return
        desc = " ".join(desc.replace(";", ",").split())[:200]
        lines = ["; FF7R Co-op Mod difficulty preset - pick it in the settings app (Balance and AI settings > Difficulty preset), then Save.",
                 "[Difficulty]", "Name=%s" % name, "Description=%s" % desc]
        for sec in ["Balance"] + [p for p, _ in ALL_SETS]:   # f1.151: [Balance] + one section per party size (f1.272: + the global set)
            lines += ["", "[%s]" % sec] + ["%s=%s" % (k, self.var(sec, k, "").get().strip()) for s_, k in DIFFICULTY_KEYS if s_ == sec]
        try:
            os.makedirs(self.diff_dir(), exist_ok=True)
            write_file_safely(path, ("\r\n".join(lines) + "\r\n").encode("utf-8"))   # app f1.333: UTF-8 (names / descriptions keep their letters)
        except OSError as e:
            messagebox.showerror(APP_TITLE, "Couldn't save the preset:\n%s" % e)
            return
        self.diff_list(select=name)
        self.status.config(text="Difficulty preset '%s' saved." % name)
        messagebox.showinfo(APP_TITLE, "Difficulty preset '%s' saved.\n\n%s" % (name, path))

    def diff_delete(self):
        name = self.diff_choice.get()
        p = self.diff_presets.get(name)
        if not p or not p["path"]:
            return
        if not messagebox.askyesno(APP_TITLE, "Are you sure you want to delete the difficulty preset '%s'?\n\nThis can't be undone.\n(%s)" % (name, p["path"]),
                                   icon="warning", default="no"):
            return
        try:
            os.remove(p["path"])
        except OSError as e:
            messagebox.showerror(APP_TITLE, "Couldn't delete the preset:\n%s" % e)
            return
        self.diff_list()
        self.status.config(text="Difficulty preset '%s' deleted." % name)
        messagebox.showinfo(APP_TITLE, "Difficulty preset '%s' deleted." % name)

    def pip_quality_show(self):
        """Which quality setting the window values match, else Custom (f1.98: also called live, E13)."""
        if not hasattr(self, "pip_q"):
            return
        cur = {k: self.var("PictureInPicture", k, "").get().strip() for k in ("FullSizeHudQuality", "Detail", "ViewDistance")}
        match = [n for n, vals in PIP_QUALITY.items() if all(_num_eq(cur[k], v) for k, v in vals.items())]
        self.pip_q.set(match[0] if match else "Custom")

    def apply_pip_quality(self):
        if self.pip_q.get() not in PIP_QUALITY:
            return
        for k, v in PIP_QUALITY.get(self.pip_q.get(), {}).items():
            self.var("PictureInPicture", k, v).set(v)
        self.status.config(text="Quality '%s' applied - Save to keep it" % self.pip_q.get())

    def apply_preset(self):
        name = self.preset.get()
        if name not in CAMERA_PRESETS:   # "Custom" = keep the numbers
            self.camera_preset_show()
            return
        vals = {k: d for k, _, _, d, _ in CAMERA if k in CAMERA_FRAMING}   # f1.39aa: only the framing numbers (was every [Camera] key)
        vals.update(CAMERA_PRESETS.get(name, {}))
        for k, v in vals.items():
            self.var("Camera", k, v).set(v)
        self.camera_preset_show()
        self.status.config(text="Preset '%s' applied - Save to keep it" % name)

    def camera_preset_show(self):   # f1.39aa: which preset the framing numbers match, else Custom
        if not hasattr(self, "preset"):
            return
        base = {k: d for k, _, _, d, _ in CAMERA if k in CAMERA_FRAMING}
        for name, ch in CAMERA_PRESETS.items():
            want = dict(base); want.update(ch)
            if all(_num_eq(self.var("Camera", k, d).get().strip(), v) for k, v in want.items() for d in [base[k]]):
                self.preset.set(name)
                return
        self.preset.set("Custom")

    def metres_row(self, parent, row, label, var, lo_cm, hi_cm, section="Camera", fld=None, step=0.5):
        """f1.39aa: a distance kept in cm in the file, shown and typed in metres."""
        shown = tk.StringVar(value="")
        busy = [False]
        def from_file(*a):
            if busy[0]:
                return
            try:
                shown.set("%g" % (float(var.get()) / 100.0))
            except ValueError:
                shown.set(var.get())
        def to_file(*a):
            try:   # app f1.333 (code review #5): "2,6" and "2.6 m" are read too; anything else is reported by validate()
                cm = "%g" % round(float(shown.get().strip().lower().rstrip("m").strip().replace(",", ".")) * 100.0, 3)
            except ValueError:
                return
            if not _num_eq(cm, var.get()):
                busy[0] = True
                try:
                    var.set(cm)
                finally:
                    busy[0] = False
        f = fld or next((f for f in CAMERA if f[1] == label), None)   # f1.155: short label + tooltip; round 2: any section's field (window cameras)
        key = f[0] if f else ""
        tt = field_tip(section, key, label, "num", f[3], f[4]) if f else ""
        tip(ttk.Label(parent, text=short_label(section, key, label)), tt).grid(row=row, column=0, sticky="w", pady=1)
        self.__dict__.setdefault("metre_fields", []).append((shown, "%s (%s)" % (short_label(section, key, label), section), lo_cm / 100.0, hi_cm / 100.0))
        tip(ttk.Spinbox(parent, from_=lo_cm / 100.0, to=hi_cm / 100.0, increment=step, textvariable=shown, width=9), tt).grid(row=row, column=1, sticky="w", padx=6)
        ttk.Label(parent, text="%s - %s m" % (_rng(lo_cm / 100.0), _rng(hi_cm / 100.0)), foreground="#888").grid(row=row, column=2, sticky="w")   # the allowed range beside the box
        var.trace_add("write", from_file)
        shown.trace_add("write", to_file)
        from_file()

    # ---- Player tabs ----
    def build_player(self, section, title):
        tab = self.scroll_tab(title)
        ui = {"map": {}}
        self.player_ui[section] = ui
        if section == "Player3":
            ttk.Label(tab, text="Pick \"Off\" under Controller to switch player 3 off (the AI then plays the 3rd party member).  *",
                      foreground="#555").pack(anchor="w", pady=(0, 2))

        d = ttk.LabelFrame(tab, text="Controller  *", padding=8)
        d.pack(fill="x")
        ui["device_text"] = tk.StringVar()
        ui["combo"] = ttk.Combobox(d, textvariable=ui["device_text"], state="readonly", width=58)
        ui["combo"].grid(row=0, column=0, sticky="w")
        ui["combo"].bind("<<ComboboxSelected>>", lambda e, s=section: self.device_chosen(s))
        ttk.Button(d, text="Refresh", command=self.refresh_devices).grid(row=0, column=1, padx=6)
        ui["live"] = ttk.Label(d, text="", foreground="#2a7")
        ui["live"].grid(row=1, column=0, columnspan=2, sticky="w", pady=(6, 0))
        ttk.Label(d, text="Press buttons on a controller - if they show up here, that's the controller this player will use.\n"
                          "If the buttons show up wrong (e.g. A shows as X), set up a controller map for it (Controller maps..., below).",
                  foreground="#888", justify="left").grid(row=2, column=0, columnspan=2, sticky="w")
        ttk.Button(d, text="Controller maps...", command=self.show_button_maps).grid(row=3, column=0, sticky="w", pady=(4, 0))   # f1.39j
        self.button_icons_row(d, section, 4)   # f1.39e
        self.seat_var(section)
        self.player_color_box(tab, section)   # f1.136

        o = ttk.LabelFrame(tab, text="Stick and rumble", padding=8)   # f1.39r: was "Character"
        o.pack(fill="x", pady=(10, 0))
        # f1.39r (the author): "Plays party member number" removed - player 2 plays party member 2, player 3 party member 3
        # ([PlayerN] PartySlot lines already in a settings file are left alone; the DLL still reads them - removal checklist)
        # f1.39p (the author): "Name shown on screen" removed - names aren't used
        ttk.Label(o, text="Left stick dead zone (0.0 - 0.9)").grid(row=1, column=0, sticky="w")
        ttk.Spinbox(o, from_=0, to=0.9, increment=0.02, width=8,
                    textvariable=self.var(section, "MoveDeadzone", PLAYER_DEFAULTS[section]["MoveDeadzone"])).grid(row=1, column=1, sticky="w", padx=6, pady=2)
        ttk.Label(o, text="0.0 - 0.9", foreground="#888").grid(row=1, column=2, sticky="w")
        ttk.Label(o, text="Rumble when this player's character is hit (%, 0 = off)").grid(row=2, column=0, sticky="w")
        digits = (self.register(lambda t: t == "" or (t.isdigit() and len(t) <= 3)), "%P")   # f1.98 (the author): letters can't be typed
        ttk.Spinbox(o, from_=0, to=200, increment=10, width=8, validate="key", validatecommand=digits,
                    textvariable=self.var(section, "Rumble", "100")).grid(row=2, column=1, sticky="w", padx=6, pady=2)
        ttk.Label(o, text="0 - 200", foreground="#888").grid(row=2, column=2, sticky="w")
        o.columnconfigure(0, weight=1)


        # joining: player 2's key and start setting live in [General], player 3's in [Player3]
        n = "2" if section == "Player2" else "3"
        jsec = "General" if section == "Player2" else section
        j = ttk.LabelFrame(tab, text="Joining", padding=8)
        j.pack(fill="x", pady=(10, 0))
        jk = self.var(jsec, "JoinKey", "F10" if section == "Player2" else "Ctrl+F10")   # f1.90: was F11 (the game's windowed / fullscreen key)
        ttk.Label(j, text="Keyboard key to join / leave").grid(row=0, column=0, sticky="w")
        ttk.Label(j, textvariable=jk, width=14, relief="sunken", anchor="center").grid(row=0, column=1, sticky="w", padx=6, pady=1)
        ttk.Button(j, text="Set...", width=7, command=lambda v=jk: self.capture_key(v, "Player %s join / leave" % n, True)).grid(row=0, column=2)
        # JoinAtStart = Auto / 0 (the author 2026-10-26: "change 'on' to 'Auto'"; an old 1 shows as Auto)
        ui["join_var"] = self.var(jsec, "JoinAtStart", "Auto")
        ui["join_on"] = tk.StringVar(value="Auto")
        ttk.Label(j, text="Join when the game starts  *").grid(row=1, column=0, sticky="w", pady=(4, 0))
        jc = ttk.Combobox(j, textvariable=ui["join_on"], values=["Auto", "Off"], state="readonly", width=8)
        jc.grid(row=1, column=1, sticky="w", padx=6, pady=(4, 0))
        jc.bind("<<ComboboxSelected>>", lambda e, s=section: self.join_chosen(s))
        ttk.Checkbutton(j, text="Join / leave by holding a button on this player's controller (the button: Advanced > Joining, below)",
                        variable=self.var(section, "JoinWithController", "1"), onvalue="1", offvalue="0").grid(row=2, column=0, columnspan=3, sticky="w")
        ttk.Checkbutton(j, text="If the controller disconnects, the AI takes over until it's back",
                        variable=self.var(section, "AutoLeaveOnDisconnect", "1"), onvalue="1", offvalue="0").grid(row=3, column=0, columnspan=3, sticky="w")
        j.columnconfigure(0, weight=1)

        # f1.39v: the "Window camera (split screen)" box moved to the Split screen tab (Player N's window camera); the old
        # turn speed % ([PlayerN] CameraSpeed) is folded into that box's degrees-per-second value
        ttk.Label(tab, text="This player's window camera (split screen) is on the Split screen tab.", foreground="#888").pack(anchor="w", pady=(10, 0))

        ui["warn"] = ttk.Label(tab, text="", foreground="#c00", justify="left")
        ui["warn"].pack(anchor="w", pady=(6, 0))
        # f1.39q (the author): the box opens / closes with a + / - button (closed when the app starts)
        head = ttk.Frame(tab)
        tog = ttk.Button(head, text="+", width=3)
        tog.pack(side="left")
        ttk.Label(head, text="Advanced: change buttons", font=("Segoe UI", 10, "bold")).pack(side="left", padx=(6, 0))
        custom = ttk.Label(head, text="", foreground="#b00")
        custom.pack(side="left", padx=(10, 0))
        box = ttk.LabelFrame(tab, labelwidget=head, padding=8)
        box.pack(fill="x", pady=(10, 0))
        b = ttk.Frame(box)
        ttk.Label(box, text="Change which buttons player %s uses (Set = Replace current selection, Add = adds an additional button for the same command, Clear = none)." % section[-1],   # the author's wording (review row 19)
                  foreground="#888").pack(anchor="w")
        def toggle(b=b, tog=tog):
            if b.winfo_manager():
                b.pack_forget(); tog.config(text="+")
            else:
                b.pack(fill="x", pady=(6, 0)); tog.config(text="-")
        tog.config(command=toggle)
        def show_custom(*a, section=section, custom=custom):
            changed = any(self.var(section, k, d).get().strip() != d for _, keys in PAD_GROUPS for k, _, d in keys)
            custom.config(text="(custom buttons in use)" if changed else "")
        ttk.Label(b, text="Only use this if this player wants a different button layout - the defaults match the game's own (PS4) layout.",   # the author's wording (review row 20)
                  foreground="#888", justify="left").grid(row=0, column=0, columnspan=5, sticky="w")
        ttk.Label(b, text="Warning: with a custom layout the button icons in the game may not show correctly - the game\n"
                          "always shows its standard buttons, not the ones changed here.",
                  foreground="#b00", justify="left").grid(row=1, column=0, columnspan=5, sticky="w", pady=(2, 4))   # f1.39k (the author)
        r = 2
        for group, keys in PAD_GROUPS:
            ttk.Label(b, text=group, font=("Segoe UI", 10, "bold")).grid(row=r, column=0, columnspan=5, sticky="w", pady=(8 if r > 2 else 0, 2))   # f1.39: spans the button columns (long group names were cut off)
            r += 1
            for key, label, default in keys:
                v = self.var(section, key, default)
                ttk.Label(b, text="    " + label).grid(row=r, column=0, sticky="w")
                ttk.Label(b, textvariable=v, width=16, relief="sunken", anchor="center").grid(row=r, column=1, padx=6, pady=1)
                ttk.Button(b, text="Set", width=6, command=lambda v=v, l=label, s=section: self.capture_pad(s, v, l, False)).grid(row=r, column=2)
                ttk.Button(b, text="Add", width=6, command=lambda v=v, l=label, s=section: self.capture_pad(s, v, l, True)).grid(row=r, column=3, padx=3)
                ttk.Button(b, text="Clear", width=6, command=lambda v=v: v.set("None")).grid(row=r, column=4)
                r += 1
        ttk.Button(b, text="Restore defaults", command=lambda s=section: self.default_buttons(s)).grid(row=r, column=0, sticky="w", pady=(8, 0))   # f1.39l: inside the box (was "Default buttons" below it)
        b.columnconfigure(0, weight=1)
        for _, keys in PAD_GROUPS:
            for k, _, d in keys:
                self.var(section, k, d).trace_add("write", show_custom)
        show_custom()

    # ---- f1.11: controllers by model ([Input2] Seat1-3) ----
    def seat_var(self, section):
        return self.var("Input2", SEAT_KEYS[section], "")

    def capture_joy(self, v, label, jid):
        if not self.joy.dll:
            messagebox.showwarning(APP_TITLE, "Can't read controllers this way on this PC.")
            return
        d = JoyCapture(self, self.joy, label, jid)
        self.wait_window(d)
        if d.result:
            v.set(d.result)

    def default_buttons(self, section):
        who = "player 3" if section == "Player3" else "player 2"
        if not messagebox.askyesno(APP_TITLE, "Put all of %s's buttons back to the defaults (the game's own PS4 layout)?" % who):
            return
        for _, keys in PAD_GROUPS:
            for key, _, default in keys:
                self.var(section, key, default).set(default)
        self.status.config(text="%s's buttons are back to the defaults - press Save to use them." % who.capitalize())

    def seat_value(self, section):
        """f1.14: [Input2] SeatN without a note; blank = Auto (the mod reads nothing else any more)."""
        return self.seat_var(section).get().split(";")[0].strip() or "Auto"

    def p3_on(self):
        return self.seat_value("Player3").lower() not in ("none", "keyboard")

    @staticmethod
    def parse_value(v):
        """'054C:0CE6' / '054C:0CE6#2' -> (vid, pid, nth); anything else -> None."""
        v = v.split(";")[0].strip()
        try:
            vp, _, n = v.partition("#")
            a, b = vp.split(":")
            if len(a) > 4 or len(b) > 4:
                return None
            return (int(a, 16), int(b, 16), int(n) if n else 1)
        except ValueError:
            return None

    def pad_for_value(self, v):
        p = self.parse_value(v)
        if not p:
            return None
        for d in self.pads:
            if (d["vid"], d["pid"], d["nth"]) == p:
                return d
        return None

    def xslot_of(self, pad):
        """The XInput slot of an Xbox-type pad (the nth slot with its model), or None."""
        if not pad or not self.xi.available:
            return None
        slots = [i for i in range(4) if self.xi.read(i)[0] and self.xi.vidpid(i) == (pad["vid"], pad["pid"])]
        return slots[pad["nth"] - 1] if len(slots) >= pad["nth"] else None

    def seat_pads(self):
        """Which plugged-in controller each player gets right now (the mod's rules: fixed picks first, then Auto seats in
        order, gamepads only). section -> pad dict or None."""
        out, taken, autos = {}, set(), []
        for sec in ("Player1", "Player2", "Player3"):
            out[sec] = None
            if sec == "Player3" and not self.p3_on():
                continue
            v = self.seat_value(sec)
            low = v.lower()
            if low == "auto":
                autos.append(sec)
                continue
            if low in ("keyboard", "none") or low.startswith("path:"):
                continue
            pad = self.pad_for_value(v)
            if pad:
                out[sec] = pad
                taken.add(pad["jid"])
        for sec in autos:
            pad = next((p for p in self.pads if p["gamepad"] and p["jid"] not in taken), None)
            if pad:
                out[sec] = pad
                taken.add(pad["jid"])
        return out

    def ui_of(self, section):
        return self.p1_ui if section == "Player1" else self.player_ui[section]

    def refresh_devices(self):
        self.pads = self.joy.scan(self.xi)
        for section in ("Player1", "Player2", "Player3"):
            if section != "Player1" and section not in self.player_ui:
                continue
            ui = self.ui_of(section)
            items, m = [], {}
            def add(text, value):
                items.append(text)
                m[text] = value
            p1kb = False
            if section == "Player1":   # f1.39n: "Player 1 uses a controller" off = keyboard only (the list is grayed out)
                low1 = self.seat_value("Player1").split(";")[0].strip().lower()
                p1kb = low1 in ("keyboard", "none")
                ui["uses"].set("0" if p1kb else "1")
                if p1kb:
                    add("Keyboard only", self.seat_value("Player1"))
            if section == "Player3":
                add("Off - the AI plays the 3rd party member", "None")
            if not p1kb:
                add(AUTO_TEXT, "Auto")
                for p in sorted(self.pads, key=lambda p: not p["gamepad"]):
                    add("%s  [%s]%s" % (p["name"], p["value"], "" if p["gamepad"] else "  (not a gamepad)"), p["value"])
            if section == "Player1":
                ui["combo"].state(["disabled"] if p1kb else ["!disabled", "readonly"])
                ui["note"].config(text=P1_NOTE_KB if p1kb else P1_NOTE_PAD)
            cur = self.seat_value(section)
            pc = self.parse_value(cur)
            low = cur.split(";")[0].strip().lower()
            if section == "Player3" and low == "keyboard":
                low = "none"   # only player 1 uses the keyboard: the mod treats it as off
            if section == "Player1" and low == "none":
                low = "keyboard"   # player 1 always has the keyboard
            show = "Keyboard only" if p1kb else next((t for t, v in m.items() if v is not None and
                         (v.lower() == low or (pc is not None and self.parse_value(v) == pc))), None)
            if show is None:
                show = ("Nobody (no controller)" if cur.lower() == "none" else
                        "Not plugged in right now: %s" % cur)
                items.insert(0, show); m[show] = cur
            ui["map"] = m
            ui["combo"].config(values=items)
            ui["device_text"].set(show)
        if hasattr(self, "ctl_ui"):
            names = ["%s  [%s]  - %s" % (p["name"], p["value"], KIND_TEXT[p["kind"]]) for p in self.pads]
            self.ctl_ui["combo"].config(values=names)
            if names and self.ctl_ui["choice"].get() not in names:
                self.ctl_ui["choice"].set(names[0])
            if not names:
                self.ctl_ui["choice"].set("")
            self.controller_chosen()
        if not self.joy.dll and not os.environ.get("FF7R_FAKE_LIST"):
            self.status.config(text="Can't list controllers on this PC (Windows' joystick functions not found)")

    def p1_uses_changed(self):   # f1.39n: the "Player 1 uses a controller" box
        sv = self.seat_var("Player1")
        if self.p1_ui["uses"].get() == "1":
            sv.set(self.p1_ui.get("last") or "Auto")
        else:
            cur = self.seat_value("Player1")
            if cur.lower() not in ("keyboard", "none"):
                self.p1_ui["last"] = cur
            sv.set("Keyboard")
            # (the author 2026-10-26: "No hot swap for keyboard should be on by default when Player 1 selects the keyboard")
            self.var("General", "KeyboardLeaderKeepsLead", "1").set("1")
        self.refresh_devices()
        self.check_conflicts()

    def device_chosen(self, section):
        ui = self.ui_of(section)
        v = ui["map"].get(ui["device_text"].get())
        if v is None:
            return
        self.seat_var(section).set(v)
        self.check_conflicts()

    def join_chosen(self, section):
        ui = self.player_ui[section]
        ui["join_var"].set("0" if ui["join_on"].get() == "Off" else "Auto")

    def join_show(self):
        for section, ui in self.player_ui.items():
            v = ui["join_var"].get().split(";")[0].strip().lower()
            try:
                on = v in ("", "auto") or int(v) != 0   # like the mod (a number; anything else = 0); an old "Auto" counts as on
            except ValueError:
                on = False
            ui["join_on"].set("Auto" if on else "Off")

    def live_text(self, section, pad):
        if pad is None:
            if section == "Player3" and not self.p3_on():
                return "Off - the AI plays", "#2a7"
            low = self.seat_value(section).lower()
            if section == "Player1" and low == "none":
                low = "keyboard"   # player 1 always has the keyboard
            if low in ("keyboard",):
                return "Keyboard and mouse", "#2a7"
            if low == "none":
                return "Nobody - no controller", "#888"
            if low == "auto":
                return "Auto: no free gamepad plugged in right now" + (" - player 1 uses the keyboard" if section == "Player1" else ""), "#c00"
            if low.startswith("path:"):
                return "Picked by device path (written by hand) - can't show it here", "#888"
            return "Not plugged in right now", "#c00"
        auto = self.seat_value(section).lower() == "auto"
        head = ("Auto -> " if auto else "") + "%s [%s]" % (pad["name"], pad["value"])
        xs = self.xslot_of(pad)
        if xs is not None:
            ok, mask = self.xi.read(xs)
            names = mask_to_names(mask) if ok else []
            return "%s   held: %s" % (head, ", ".join(names) if names else "-"), "#2a7"
        held = sorted(self.joy.held(pad["jid"])) if self.joy.dll else []
        dp = sorted(self.joy.pov(pad["jid"])) if self.joy.dll else []
        return "%s   held: %s" % (head, ", ".join(["button %d" % b for b in held] + dp) or "-"), "#2a7"

    def poll_pads(self):
        try:
            sp = self.seat_pads()
            for section in ("Player1", "Player2", "Player3"):
                if section != "Player1" and section not in self.player_ui:
                    continue
                t, col = self.live_text(section, sp[section])
                self.ui_of(section)["live"].config(text=t, foreground=col)
            if hasattr(self, "ctl_ui"):
                pad = self.ctl_pad()
                if pad is None:
                    self.ctl_ui["live"].config(text="")
                else:
                    held = sorted(self.joy.held(pad["jid"])) if self.joy.dll else []
                    dp = sorted(self.joy.pov(pad["jid"])) if self.joy.dll else []
                    self.ctl_ui["live"].config(text="Buttons held right now: %s" %   # f1.39t: plainer
                                               (", ".join([str(b) for b in held] + dp) or "-"))
        except tk.TclError:
            return
        self.after(60, self.poll_pads)

    def map_numbers(self, pad):
        """Button numbers of a non-XInput controller in MAP_KEYS order (its [Pad.VID:PID] map, as the mod reads it)."""
        if pad["kind"] == "PlayStation":
            return list(PS_NUMBERS)   # read directly by the mod, always the PlayStation layout
        sec = self.ensure_map_vars(pad)
        return [self.var(sec, k, PS_NUMBERS[i]).get().strip() for i, (k, _) in enumerate(MAP_KEYS)]

    def capture_pad(self, section, v, label, add):
        pad = self.seat_pads().get(section)
        if pad is None:
            messagebox.showinfo(APP_TITLE, "This player has no controller plugged in right now (Controller, at the top of this tab).")
            return
        xs = self.xslot_of(pad)
        if xs is not None:
            d = PadCapture(self, self.xi, xs, label)
        elif self.joy.dll:
            d = JoyPadCapture(self, self.joy, pad["jid"], self.map_numbers(pad), label)
        else:
            messagebox.showwarning(APP_TITLE, "Can't read this controller on this PC.")
            return
        self.wait_window(d)
        if d.result:
            cur = [] if not add or v.get() in ("", "None") else [x.strip() for x in v.get().split(",")]
            if d.result not in cur:
                cur.append(d.result)
            v.set(", ".join(cur))

    # ---- f1.11: Controllers tab (button maps per controller model) ----
    def show_button_maps(self):   # f1.39j: the Player tabs' "Button maps..." button
        for t in self.nb.tabs():
            if self.nb.tab(t, "text") == "Controller maps":
                self.nb.select(t)

    def build_controllers(self):
        tab = self.scroll_tab("Controller maps")   # f1.39j: was "Controllers", then "Button maps"; f1.39s: "Controller maps" (the author's layout review)
        self.ctl_ui = {"choice": tk.StringVar()}
        ttk.Label(tab, text="Only for controllers whose buttons come out wrong. Xbox-style and PlayStation controllers don't need a controller map.\n"
                            "A map belongs to the controller model, so it works for whichever player uses that model.",
                  foreground="#555", justify="left").pack(anchor="w", pady=(0, 6))   # f1.39j
        top = ttk.LabelFrame(tab, text="Controllers plugged in", padding=8)
        top.pack(fill="x")
        self.ctl_ui["combo"] = ttk.Combobox(top, textvariable=self.ctl_ui["choice"], state="readonly", width=64)
        self.ctl_ui["combo"].grid(row=0, column=0, sticky="w")
        self.ctl_ui["combo"].bind("<<ComboboxSelected>>", lambda e: self.controller_chosen())
        ttk.Button(top, text="Refresh", command=self.refresh_devices).grid(row=0, column=1, padx=6)
        self.ctl_ui["info"] = ttk.Label(top, text="", foreground="#555", justify="left")
        self.ctl_ui["info"].grid(row=1, column=0, columnspan=2, sticky="w", pady=(6, 0))
        self.ctl_ui["live"] = ttk.Label(top, text="", foreground="#2a7")
        self.ctl_ui["live"].grid(row=2, column=0, columnspan=2, sticky="w", pady=(4, 0))
        top.columnconfigure(0, weight=1)
        self.ctl_ui["mapbox"] = ttk.LabelFrame(tab, text="Controller map", padding=8)
        self.ctl_ui["mapbox"].pack(fill="x", pady=(10, 0))

    def ctl_pad(self):
        if not hasattr(self, "ctl_ui"):
            return None
        t = self.ctl_ui["choice"].get()
        for p in self.pads:
            if t.startswith("%s  [%s]" % (p["name"], p["value"])):
                return p
        return None

    def ensure_map_vars(self, pad):
        sec = "Pad.%04X:%04X" % (pad["vid"], pad["pid"])
        if (sec, "A") in self.vars:
            return sec
        keys = [(k, PS_NUMBERS[i]) for i, (k, _) in enumerate(MAP_KEYS)] + \
               [(k, d) for k, _, d in MAP_AXES] + [("Name", pad["name"])]
        was = getattr(self, "_loading", False)
        self._loading = True
        try:
            for k, d in keys:
                v = self.var(sec, k, d)
                v.set(self.ini.get(sec, k, d))
                self.loaded[(sec, k)] = v.get()   # as the file has it (or what the mod uses without it) - unchanged = not written
        finally:
            self._loading = was
        return sec

    def controller_chosen(self):
        box = self.ctl_ui["mapbox"]
        for w in box.winfo_children():
            w.destroy()
        pad = self.ctl_pad()
        if pad is None:
            self.ctl_ui["info"].config(text="No controllers found." if not self.pads else "")
            return
        sec = self.ensure_map_vars(pad)
        has = any(self.ini.get(sec, k, None) is not None for k, _ in MAP_KEYS)
        if pad["kind"] != "other":   # f1.39t: plainer wording (the author's layout review)
            self.ctl_ui["info"].config(text="%s - %s controller, no map needed." % (
                pad["name"], "an Xbox-style" if pad["kind"] == "Xbox" else "a PlayStation"))
            ttk.Label(box, text="Not needed for this controller: the mod already knows its buttons.", foreground="#555").grid(row=0, column=0, sticky="w")
            return
        self.ctl_ui["info"].config(text="%s - %s" % (pad["name"], "this controller has its own map." if has else
                                                     "no map yet, so the standard PlayStation-style numbers below are used."))
        r = 0
        hb = ttk.Frame(box)
        hb.grid(row=r, column=0, columnspan=3, sticky="w")
        ttk.Label(hb, text="Buttons", font=("Segoe UI", 10, "bold")).pack(side="left")
        ttk.Button(hb, text="Learn all", command=lambda s=sec, j=pad["jid"]: self.learn_all(s, j)).pack(side="left", padx=(12, 0))   # f1.39t
        r += 1
        ttk.Label(box, text="Press Learn, then that button on this controller - or Learn all to go through them in order. 0 = no button.",
                  foreground="#555").grid(row=r, column=0, columnspan=3, sticky="w", pady=(0, 4))
        r += 1
        for key, label in MAP_KEYS:
            v = self.var(sec, key, "0")
            ttk.Label(box, text="    " + label).grid(row=r, column=0, sticky="w")
            ttk.Spinbox(box, from_=0, to=32, increment=1, width=6, textvariable=v).grid(row=r, column=1, sticky="w", padx=6, pady=1)
            ttk.Button(box, text="Learn", width=7, command=lambda v=v, l=label, j=pad["jid"]: self.capture_joy(v, l, j)).grid(row=r, column=2, sticky="w")
            r += 1
        ttk.Label(box, text="Control sticks", font=("Segoe UI", 10, "bold")).grid(row=r, column=0, sticky="w", pady=(10, 0))   # f1.39t (the author)
        r += 1
        ttk.Label(box, text="Only change these if a stick moves the wrong way in the game.", foreground="#555").grid(
            row=r, column=0, columnspan=3, sticky="w", pady=(0, 4))
        r += 1
        for key, label, default in MAP_AXES:
            ttk.Label(box, text="    " + label).grid(row=r, column=0, sticky="w")
            ttk.Combobox(box, textvariable=self.var(sec, key, default), values=DI_AXIS_NAMES, state="readonly", width=8).grid(row=r, column=1, sticky="w", padx=6, pady=1)
            r += 1
        # f1.39t (the author): the "L2 / R2 share the Z axis" check box is gone; f1.79: TriggersOnZ no longer read by the DLL
        rd = ttk.Frame(box)
        rd.grid(row=r, column=0, columnspan=3, sticky="w", pady=(10, 0))
        ttk.Button(rd, text="Restore defaults", command=lambda s=sec: self.map_defaults(s)).pack(side="left")
        ttk.Label(rd, text="Puts back the standard PlayStation-style button numbers and sticks.", foreground="#888").pack(side="left", padx=(8, 0))   # f1.39t
        box.columnconfigure(0, weight=1)

    def learn_all(self, sec, jid):   # f1.39t (the author): learn the 12 buttons one after another
        if not self.joy.dll:
            messagebox.showwarning(APP_TITLE, "Can't read controllers this way on this PC.")
            return
        done = 0
        for i, (key, label) in enumerate(MAP_KEYS):
            d = JoyCapture(self, self.joy, "%s   (%d of %d)" % (label, i + 1, len(MAP_KEYS)), jid)
            self.wait_window(d)
            if not d.result:
                break
            self.var(sec, key, "0").set(d.result)
            done += 1
        self.status.config(text=("All %d buttons learned - press Save to use them." % done) if done == len(MAP_KEYS) else
                                ("Stopped after %d of %d buttons - the rest are unchanged." % (done, len(MAP_KEYS))))

    def map_defaults(self, sec):   # f1.39m: "Restore defaults" (was "PlayStation numbering"), asks first like the Player tabs' one
        if not messagebox.askyesno(APP_TITLE, "Put this controller's map back to the defaults (the standard PlayStation numbering)?"):
            return
        for i, (k, _) in enumerate(MAP_KEYS):
            self.var(sec, k, PS_NUMBERS[i]).set(PS_NUMBERS[i])
        for k, _, d in MAP_AXES:
            self.var(sec, k, d).set(d)
        self.status.config(text="The controller map is back to the defaults - press Save to use it.")

    def capture_key(self, v, label, mods):
        d = KeyCapture(self, label, mods)
        self.wait_window(d)
        if d.result:
            v.set(d.result)

    # ---- checks ----
    HOTKEYS = [("General", "JoinKey", "Player 2 join / leave"), ("Player3", "JoinKey", "Player 3 join / leave"),
               ("General", "LeadKeyPlayer1", "Hot swap player 1"), ("General", "LeadKeyPlayer2", "Hot swap player 2"),
               ("General", "LeadKeyPlayer3", "Hot swap player 3"), ("General", "ReloadKey", "Reload settings"),
               ("General", "HelpKey", "Help list"), ("Camera", "ToggleKey", "Co-op camera on / off"),
               ("PictureInPicture", "ToggleKey", "Split screen on / off"), ("PictureInPicture", "LayoutKey", "Split screen layout")]

    def key_clashes(self):
        """f1.22: keyboard keys set for two mod actions (the mod matches Ctrl / Shift / Alt exactly, so F9 and Ctrl+F9 differ)."""
        used = {}
        for sec, key, label in self.HOTKEYS:
            if sec == "Player3" and not self.p3_on():
                continue
            v = self.vars.get((sec, key))
            if v is None:
                continue
            parts = [x.strip().lower() for x in v.get().split(";")[0].split("+") if x.strip()]
            if not parts or parts[-1] in ("none", "off"):
                continue
            mods = sorted(m.replace("control", "ctrl") for m in parts[:-1])
            used.setdefault("+".join(mods + [parts[-1]]), []).append(label)
        return ["%s is set for more than one key action: %s" % (k.upper(), ", ".join(w)) for k, w in used.items() if len(w) > 1]

    def check_conflicts(self):
        if hasattr(self, "gen_warn"):
            self.gen_warn.config(text="\n".join("! " + m for m in self.key_clashes()))
        seats = {}
        for sec in ("Player1", "Player2", "Player3"):
            v = self.seat_var(sec).get().strip()
            if sec == "Player3" and not self.p3_on():
                continue
            if v and v.lower() not in ("auto", "keyboard", "none"):
                p = self.parse_value(v)
                key = ("%04X:%04X#%d" % p) if p else v.lower()
                seats.setdefault(key, []).append(sec.replace("Player", "Player "))
        same = ["%s are set to the same controller (%s)." % (" and ".join(w), k.split("#")[0] + ("" if k.endswith("#1") else " #" + k.split("#")[-1]))
                for k, w in seats.items() if len(w) > 1]
        if hasattr(self, "p1_ui"):
            self.p1_ui["warn"].config(text="\n".join("! " + m for m in same if "Player 1" in m))
        for section, ui in self.player_ui.items():
            msgs = []
            battle = PAD_GROUPS[0][1]
            used = {}
            for key, label, _ in battle + [PAD_GROUPS[3][1][0], PAD_GROUPS[2][1][0]] + PAD_GROUPS[4][1][:2]:
                for n in [x.strip() for x in self.var(section, key, "").get().split(",") if x.strip() and x.strip() != "None"]:
                    used.setdefault(n, []).append(label)
            for n, labels in used.items():
                if len(labels) > 1:
                    msgs.append("%s is used for more than one battle action: %s" % (n, ", ".join(labels)))
            me = section.replace("Player", "Player ")
            msgs += [m for m in same if me in m]
            ui["warn"].config(text="\n".join("! " + m for m in msgs))

    # ---- load / save ----
    def reload(self):
        self.ini.load()
        self._loading = True
        try:
            for (section, key), v in self.vars.items():
                d = v.default
                if key in MENU_SHARED_SEC and section.startswith("Balance") and section != "Balance" and \
                        not (section == "Balance1" and key in SET_ATB):   # f1.276: the one character party's ATB = 100 when missing (as the mod)
                    # f1.272: as the mod - a set without
                    d = self.ini.get(MENU_SHARED_SEC[key], key, d)                                           # its own value uses the shared one
                # (f1.54: a missing [PlayerN] Window* key / MenuSlowdownPlayer2 / 3 = the default, like the mod)
                v.set(self.norm_value(section, key, self.ini.get(section, key, d), d))
            if hasattr(self, "win_same") and self.ini.get("Player3", "WindowSameAsPlayer2", None) is None:
                # f1.39w: not in the file yet - on only if player 3's window camera already matches player 2's
                self.win_same.set("1" if all(self.var("Player2", f[0], f[3]).get() == self.var("Player3", f[0], f[3]).get()
                                             for f in WINDOW_FIELDS) else "0")
        finally:
            self._loading = False
        self.loaded = {k: v.get() for k, v in self.vars.items()}   # v19.5: what the file said when the app read it
        self.dirty = False
        self.values_changed()
        self.status.config(text="Loaded" if self.ini.exists else
                           "No settings file yet - start the game once to create it, or Save to make one here")

    def norm_value(self, section, key, val, default):
        """A value read from a file, as the app shows it (shared by reload and profile loading)."""
        if key in ("JoinKey", "ToggleKey", "LayoutKey", "LeadKeyPlayer1", "LeadKeyPlayer2", "LeadKeyPlayer3", "ReloadKey", "HelpKey") or section == "Player1":
            pass
        elif section in ("Player2", "Player3") and key not in ("PartySlot", "MoveDeadzone", "JoinHoldSeconds", "AutoLeaveOnDisconnect", "JoinWithController", "JoinAtStart", "Name", "Rumble", "InvertY", "CameraSpeed", "ButtonIcons", "Color") \
                and not key.startswith("DirectInput") and not key.startswith("Window"):
            val = norm_pad(val) or default
        return val

    def values_changed(self):
        """Refresh the parts of the window that are worked out from several values."""
        self.join_show()   # f1.11
        if hasattr(self, "split_start"):
            self.split_start_show()
        self.camera_preset_show()   # f1.39aa
        if hasattr(self, "diff_presets"):   # f1.39ad
            self.diff_list() if getattr(self, "_diff_dir_seen", None) != self.diff_dir() else self.diff_show()
            self._diff_dir_seen = self.diff_dir()
        if hasattr(self, "win_same"):   # f1.39w: gray out / copy player 2's window camera to player 3 (a hand-edited file may differ)
            self.window_same_changed()
        self.pip_quality_show()   # show which quality setting the current values match
        self.refresh_devices()
        self.check_conflicts()

    # ---- f1.39: profiles ----
    def profile_dir(self):
        return os.path.join(os.path.dirname(os.path.abspath(self.ini.path)), PROFILE_DIR)

    def profile_list(self, select=None):
        self.profiles = {PROFILE_DEFAULTS: None}
        self.prof_info = {PROFILE_DEFAULTS: {"description": "Every setting back to how the mod comes (your controller choices stay).", "readonly": "1"}}
        d = self.profile_dir()
        if os.path.isdir(d):
            for fn in sorted(os.listdir(d), key=str.lower):
                if not fn.lower().endswith(".ini"):
                    continue
                path = os.path.join(d, fn)
                try:
                    _, info = read_profile(path)
                except OSError:
                    continue
                name = info.get("name") or fn[:-4]
                while name in self.profiles:
                    name += " (2)"
                self.profiles[name] = path
                self.prof_info[name] = info
        names = list(self.profiles)
        self.prof_combo["values"] = names
        cur = select if select in self.profiles else (self.prof_choice.get() if self.prof_choice.get() in self.profiles else names[0])
        self.prof_choice.set(cur)
        self.profile_show()

    def profile_show(self):
        name = self.prof_choice.get()
        info = self.prof_info.get(name, {})
        ro = info.get("readonly", "0") == "1"
        desc = info.get("description", "")
        self.prof_desc.config(text=(desc + ("   (comes with the mod - can't be changed or deleted)" if ro and self.profiles.get(name) else "")).strip())
        self.prof_del.state(["disabled"] if ro else ["!disabled"])

    def profile_load(self):
        name = self.prof_choice.get()
        if name not in self.profiles:
            return
        path = self.profiles[name]
        if path is None:
            vals = None
        else:
            try:
                vals, _ = read_profile(path)
            except OSError as e:
                messagebox.showerror(APP_TITLE, "Couldn't read the profile:\n%s" % e)
                return
        if not messagebox.askokcancel(APP_TITLE, "Load the profile '%s' into the app?\n\nIts settings replace what the app shows now "
                                      "(your controller choices stay as they are). Press Save afterwards to use it in the game." % name):
            return
        n = 0
        for (section, key), v in self.vars.items():
            if not profile_key_ok(section, key):
                continue
            if vals is None:
                new = v.default
            elif (section.lower(), key.lower()) in vals:
                new = self.norm_value(section, key, vals[(section.lower(), key.lower())], v.default)
            elif key in WINDOW_SHARED and section in ("Player2", "Player3") and ("pictureinpicture", WINDOW_SHARED[key].lower()) in vals:
                new = window_default(lambda sc, k, d: vals.get((sc.lower(), k.lower()), d), section, key, v.default)   # f1.39u/v: an older profile
            else:
                continue   # a profile may hold only some settings
            if v.get() != new:
                v.set(new)
                n += 1
        self.values_changed()
        msg = ("Profile '%s' loaded - %d setting(s) changed. Press Save to use it." % (name, n) if n else
               "Profile '%s' loaded - it matches what the app already shows." % name)
        self.status.config(text=msg)
        messagebox.showinfo(APP_TITLE, ("Profile '%s' loaded.\n\n%d setting(s) changed in the app. Press Save to use them in the game." % (name, n)) if n else
                            ("Profile '%s' loaded.\n\nIt matches what the app already shows - nothing changed." % name))   # f1.39c: acknowledged

    def profile_save(self):
        bad = self.validate()
        if bad:
            messagebox.showerror(APP_TITLE, "Please fix these first:\n\n" + "\n".join(bad))
            return
        cur = self.prof_choice.get()
        suggest = cur if self.profiles.get(cur) and self.prof_info.get(cur, {}).get("readonly", "0") != "1" else ""
        name = simpledialog.askstring(APP_TITLE, "Name for this profile:", initialvalue=suggest, parent=self)
        if not name or not name.strip():
            return
        name = name.strip()
        fn = profile_file_name(name)
        if not fn:
            messagebox.showerror(APP_TITLE, "Please use letters or numbers in the name.")
            return
        path = os.path.join(self.profile_dir(), fn + ".ini")
        old_info = {}
        if os.path.isfile(path):
            try:
                _, old_info = read_profile(path)
            except OSError:
                old_info = {}
            if old_info.get("readonly", "0") == "1":
                messagebox.showerror(APP_TITLE, "'%s' comes with the mod and can't be changed. Please pick another name." % name)
                return
            if not messagebox.askyesno(APP_TITLE, "A profile called '%s' already exists. Replace it?" % name):
                return
        desc = simpledialog.askstring(APP_TITLE, "A short description (optional):", initialvalue=old_info.get("description", ""), parent=self)
        if desc is None:
            return
        desc = " ".join(desc.replace(";", ",").split())[:200]
        lines = ["; FF7R Co-op Mod settings profile - load it in the settings app (Profile: ... Load), then Save.",
                 "; Controller choices and controller maps are not part of a profile.",
                 "[Profile]", "Name=%s" % name.replace(";", ","), "Description=%s" % desc]
        by_sec = {}
        for (section, key), v in self.vars.items():
            if profile_key_ok(section, key):
                by_sec.setdefault(section, []).append((key, v.get()))
        for section, items in by_sec.items():
            lines.append("")
            lines.append("[%s]" % section)
            lines += ["%s=%s" % kv for kv in items]
        try:
            os.makedirs(self.profile_dir(), exist_ok=True)
            write_file_safely(path, ("\r\n".join(lines) + "\r\n").encode("utf-8"))   # app f1.333: UTF-8 (names / descriptions keep their letters)
        except OSError as e:
            messagebox.showerror(APP_TITLE, "Couldn't save the profile:\n%s" % e)
            return
        self.profile_list(select=name)
        self.status.config(text="Profile '%s' saved (%s)." % (name, os.path.join(PROFILE_DIR, fn + ".ini")))
        messagebox.showinfo(APP_TITLE, "Profile '%s' saved.\n\n%s\n\n(Your controller choices are not part of a profile.)" % (name, path))   # f1.39c

    def profile_delete(self):
        name = self.prof_choice.get()
        path = self.profiles.get(name)
        if not path or self.prof_info.get(name, {}).get("readonly", "0") == "1":
            return
        if not messagebox.askyesno(APP_TITLE, "Are you sure you want to delete the profile '%s'?\n\nThis can't be undone.\n(%s)" % (name, path),
                                   icon="warning", default="no"):   # f1.39c
            return
        try:
            os.remove(path)
        except OSError as e:
            messagebox.showerror(APP_TITLE, "Couldn't delete it:\n%s" % e)
            return
        self.profile_list()
        self.status.config(text="Profile '%s' deleted." % name)

    def validate(self):
        bad = []
        for (section, key), v in self.vars.items():
            for flds in (GENERAL, CAMERA, PIP, BALANCE, WINDOW_FIELDS):
                for k, label, kind, d, extra in flds:
                    # f1.273 (code review #64): every Balance section, incl. [BalanceGlobal] (f1.272 - it wasn't checked)
                    if k == key and kind == "num" and (section in ("General", "Camera", "PictureInPicture", "Player2", "Player3") or section.startswith("Balance")):
                        where = dict(ALL_SETS).get(section, "")
                        lab = "%s (%s)" % (label, where) if where else label
                        try:
                            f = float(v.get())
                            if not (extra[0] <= f <= extra[1]):
                                bad.append("%s: %s is outside %g - %g" % (lab, v.get(), extra[0], extra[1]))
                        except ValueError:
                            bad.append("%s: '%s' isn't a number" % (lab, v.get()))
        checked = {(sec_, k_) for (sec_, k_) in self.vars if any(k_ == f[0] and f[2] == "num" for flds in (GENERAL, CAMERA, PIP, BALANCE, WINDOW_FIELDS) for f in flds)}
        for (section, key), (label, extra) in getattr(self, "num_fields", {}).items():   # app f1.333 (code review #4): boxes made outside the tables
            if (section, key) in checked or (section, key) not in self.vars:
                continue
            val = self.vars[(section, key)].get()
            try:
                f = float(val)
                if not (extra[0] <= f <= extra[1]):
                    bad.append("%s: %s is outside %g - %g" % (label, val, extra[0], extra[1]))
            except ValueError:
                bad.append("%s: '%s' isn't a number" % (label, val))
        for (section, key), v in self.vars.items():   # app f1.333: whole numbers where the mod reads whole numbers
            if key in INT_KEYS:
                try:
                    f = float(v.get())
                    if f != int(f):
                        bad.append("%s (%s): %s - use a whole number" % (key, section, v.get()))
                except ValueError:
                    pass   # reported above
        for shown, label, lo, hi in getattr(self, "metre_fields", []):   # app f1.333 (code review #5)
            try:
                f = float(shown.get().strip().lower().rstrip("m").strip().replace(",", "."))
                if not (lo <= f <= hi):
                    bad.append("%s: %s m is outside %g - %g m" % (label, shown.get(), lo, hi))
            except ValueError:
                bad.append("%s: '%s' isn't a distance in metres" % (label, shown.get()))
        # f1.39p: the player name checks went with the name fields
        if player_color("Player2", self.var("Player2", "Color", "Default").get()) == player_color("Player3", self.var("Player3", "Color", "Default").get()):
            bad.append("Players 2 and 3 have the same color (%s) - pick another one on a Player tab" %
                       player_color("Player2", self.var("Player2", "Color", "Default").get()))   # f1.156
        for (section, key), v in self.vars.items():   # f1.11: button maps per controller model
            if section.startswith("Pad.") and key in [k for k, _ in MAP_KEYS]:
                val = v.get().strip()
                if not (val.isdigit() and 0 <= int(val) <= 32):
                    bad.append("Controller maps tab, %s %s: button number 0 - 32" % (section[4:], key))
        for section in ("Player2", "Player3"):
            try:
                dz = float(self.var(section, "MoveDeadzone", "0.24").get())
                if not 0 <= dz <= 0.9:
                    raise ValueError
            except ValueError:
                bad.append("%s dead zone must be 0.0 - 0.9" % section)
            rv = self.var(section, "Rumble", "100").get().strip()   # f1.98 (E7): the mod reads a whole number 0 - 200 ("abc" = 0 = no rumble)
            if not (rv.isdigit() and 0 <= int(rv) <= 200):
                bad.append("%s rumble must be a whole number 0 - 200 (0 = off)" % section)
        return bad

    def save(self):
        bad = self.validate()
        if bad:
            messagebox.showerror(APP_TITLE, "Please fix these first:\n\n" + "\n".join(bad))
            return False
        # pick up any edits made outside the app since loading, then apply ONLY what was changed in the app (v19.5:
        # before, every value the app held was written back, so an app left open undid changes made in the file meanwhile)
        self.ini.load()
        loaded = getattr(self, "loaded", {})
        changed = []
        for (section, key), v in self.vars.items():
            if (section, key) in loaded and v.get() == loaded[(section, key)]:
                continue
            changed.append((section, key))
            if not section.startswith("Pad."):
                self.ini.set(section, key, v.get())
        for psec in sorted({s_ for s_, _ in changed if s_.startswith("Pad.")}):   # f1.11: a button map is written whole, in
            for (section, key), v in self.vars.items():                           # order (missing keys would mean the
                if section == psec:                                                # PlayStation numbering to the mod)
                    self.ini.set(section, key, v.get())
        if not self.ini.exists and not changed:   # app f1.333 (code review #3): no file yet and nothing changed - the mod writes its own
            self.status.config(text="Nothing to save yet - the mod writes its settings file (with notes) the first time the game starts.")
            messagebox.showinfo(APP_TITLE, "Nothing to save yet.\n\nThe mod writes its settings file (with notes on every setting) the first time the game starts with it installed.")
            return True
        if not os.path.isfile(os.path.join(os.path.dirname(os.path.abspath(self.ini.path)), "xinput1_3.dll")) and not getattr(self, "_nodll_ok", False):
            if not messagebox.askyesno(APP_TITLE, "The mod (xinput1_3.dll) isn't in this folder:\n%s\n\nThe game only reads the settings file next to the mod "
                                       "(End\\Binaries\\Win64). Save here anyway?\n\n(Choose... at the top picks another settings file.)" % os.path.dirname(os.path.abspath(self.ini.path))):
                return False
            self._nodll_ok = True
        try:
            self.ini.save()
        except OSError as e:
            messagebox.showerror(APP_TITLE, "Couldn't save:\n%s" % e)
            return False
        self.dirty = False
        pads = [k for s_, k in changed if s_ == "Input2" and k.startswith("Seat")]
        self.reload()   # show the file as it is now
        if pads:
            self.status.config(text="Saved. Controller changes (who uses which controller, player 3 on/off) take effect when the game is restarted.")
            messagebox.showinfo(APP_TITLE, "Saved.\n\nController changes (which controller each player uses, player 3 on / off) only take effect after restarting the game.")
        else:
            self.status.config(text="Saved. Press the reload key (F6) in game to use it - settings marked * need a game restart.")
            messagebox.showinfo(APP_TITLE, "Saved.\n\nPress the reload key (F6) in the game to use the changes - settings marked * need a game restart.")   # f1.39c: acknowledged
        return True

    def choose_file(self):
        p = filedialog.askopenfilename(title="Find FF7RCoopMod.ini", filetypes=[("Settings", "*.ini")],
                                       initialdir=os.path.dirname(self.ini.path))
        if p:
            self.ini = IniFile(p)
            self.path_lbl.config(text=p)
            self.show_version(p)
            self.reload()
            self.profile_list()

    def on_close(self):
        if self.dirty:
            r = messagebox.askyesnocancel(APP_TITLE, "Save your changes before closing?")
            if r is None:
                return
            if r and not self.save():
                return
        self.destroy()


def steam_game_dirs():
    """app f1.333 (code review #8): the game's Win64 folder in every Steam library (registry + libraryfolders.vdf)."""
    out = []
    try:
        import winreg
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r"Software\Valve\Steam") as k:
            steam = winreg.QueryValueEx(k, "SteamPath")[0]
    except Exception:
        return out
    libs = [steam]
    try:
        text = read_text_file(os.path.join(steam, "steamapps", "libraryfolders.vdf"))[0]
        libs += [m.replace("\\\\", "\\") for m in re.findall(r'"path"\s+"([^"]+)"', text)]
    except OSError:
        pass
    for lib in libs:
        d = os.path.join(lib, "steamapps", "common", "FINAL FANTASY VII REMAKE", "End", "Binaries", "Win64")
        if d not in out:
            out.append(d)
    return out


def find_ini():
    here = os.path.dirname(os.path.abspath(sys.argv[0]))
    dirs = [here]
    if not os.path.isfile(os.path.join(here, INI_NAME)):
        dirs += steam_game_dirs()
    for d in dirs:
        p = os.path.join(d, INI_NAME)
        if os.path.isfile(p):
            return p
    return os.path.join(here, INI_NAME)


if __name__ == "__main__":
    path = sys.argv[1] if len(sys.argv) > 1 else find_ini()
    if sys.platform == "win32":
        try:
            ctypes.windll.shcore.SetProcessDpiAwareness(1)
        except Exception:
            pass
    App(path).mainloop()
