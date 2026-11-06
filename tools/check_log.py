#!/usr/bin/env python3
"""FF7R co-op mod - log checker (f1.98, 2026-10-22).

Reads FF7RCoopMod_Coop.log (appended across game sessions), picks one session and prints a short report:
problems first (crashes, failed hooks, warnings), then settings / controllers / battles, the two-command-menus
checks (players 2/3's own menus and focus contexts), shared items, the R1 regression check, enemy targeting
(ATTENTION / TARGETLOG), button icons (ICONS) and the reticle (RETANIM), then the temporary diagnostics found.
It never replaces reading the log - it points at what to read.

Usage:
  python3 check_log.py FF7RCoopMod_Coop.log            # the last session
  python3 check_log.py FF7RCoopMod_Coop.log -s -2      # the one before it
  python3 check_log.py FF7RCoopMod_Coop.log -s f1.20   # the last session of that version
  python3 check_log.py FF7RCoopMod_Coop.log --list     # list the sessions
  add -v to print every matching line instead of the first few
"""
import argparse, re, sys
from collections import Counter, defaultdict

SESSION = re.compile(r"^\[(\d\d:\d\d:\d\d\.\d+)\] ==== FF7RCoopMod v(.+?) - installing co-op hooks")
TIME = re.compile(r"^\[(\d\d:\d\d:\d\d\.\d+)\] ")


def secs(ts):
    h, m, s = ts.split(":")
    return int(h) * 3600 + int(m) * 60 + float(s)


def sessions(lines):
    """(first line, version, start time) per session. The settings are logged just BEFORE the ==== line, so a
    session starts at the first of the lines directly above it that are at most 2 s older."""
    out = []
    for i, l in enumerate(lines):
        m = SESSION.match(l)
        if not m:
            continue
        t0 = secs(m.group(1)); j = i
        while j > 0 and (not out or j - 1 > out[-1][0]):
            mt = TIME.match(lines[j - 1])
            if not mt or not (0 <= t0 - secs(mt.group(1)) <= 2.0):
                break
            j -= 1
        out.append((j, m.group(2), m.group(1)))
    # f1.207: past 40 MB in a session the log is renamed to FF7RCoopMod_Coop_previous.log and a new one starts with a
    # "LOG: the log passed 40 MB" line - no ==== header. Those lines are the rest of that session ("continued").
    if lines and "LOG: the log passed 40 MB" in lines[0] and (not out or out[0][0] > 0):
        mt = TIME.match(lines[0])
        out.insert(0, (0, "(continued from _previous.log)", mt.group(1) if mt else "?"))
    return out


def pick(sess, want):
    if not sess:
        return None
    if want is None:
        return len(sess) - 1
    if re.fullmatch(r"-?\d+", want):
        k = int(want)
        return k if k >= 0 else len(sess) + k
    hits = [k for k, s in enumerate(sess) if s[1] == want.lstrip("v")]
    return hits[-1] if hits else None


class Report:
    def __init__(self, lines, verbose):
        self.lines, self.verbose = lines, verbose
        self.flags = []   # (level, text)

    def grep(self, pat, flags=0):
        r = re.compile(pat, flags)
        return [l for l in self.lines if r.search(l)]

    def show(self, title, hits, n=6):
        print("  %-46s %d" % (title, len(hits)))
        for l in (hits if self.verbose else hits[:n]):
            print("      " + l[:240])
        if not self.verbose and len(hits) > n:
            print("      ... %d more (-v shows all)" % (len(hits) - n))

    def flag(self, level, text):
        self.flags.append((level, text))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("log")
    ap.add_argument("-s", "--session", help="index (0 = first, -1 = last) or version, e.g. f1.21")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("-v", "--verbose", action="store_true")
    a = ap.parse_args()
    with open(a.log, "r", encoding="utf-8", errors="replace") as f:
        lines = [l.rstrip("\r\n") for l in f]
    sess = sessions(lines)
    if a.list:
        for k, (i, v, t) in enumerate(sess):
            end = sess[k + 1][0] if k + 1 < len(sess) else len(lines)
            print("%3d  %-6s started %s  lines %d" % (k - len(sess), v, t, end - i))
        return
    k = pick(sess, a.session)
    if k is None or k < 0 or k >= len(sess):
        sys.exit("no such session (use --list)")
    i0, ver, t0 = sess[k]
    i1 = sess[k + 1][0] if k + 1 < len(sess) else len(lines)
    body = lines[i0:i1]
    last_t = next((TIME.match(l).group(1) for l in reversed(body) if TIME.match(l)), t0)
    R = Report(body, a.verbose)
    print("=" * 100)
    print("Session %d of %d: version %s, %s - %s, %d lines" % (k + 1, len(sess), ver, t0, last_t, len(body)))
    print("=" * 100)

    # ---------------- problems ----------------
    print("\n[1] Problems")
    crash = R.grep(r"\bCRASH\b")
    hookf = R.grep(r"HOOK FAILED|NOT installed|hook failed")
    warn = R.grep(r"WARNING|ERROR|contexts OFF|not understood|can't be read")
    R.show("CRASH lines", crash)
    R.show("failed hooks", hookf)
    hookn = R.grep(r"HOOK NOTE")   # f1.205 (finding #34): a hook written while a game thread was inside it / not every thread paused
    R.show("hook notes (f1.205: threads while a hook was written)", hookn)
    paused = [int(m.group(1)) for l in R.grep(r"^.*Hooked .*\((\d+) other threads paused") for m in [re.search(r"\((\d+) other threads paused", l)] if m]
    if paused: print("  hooks written with the other threads paused: %d (%d - %d threads each)" % (len(paused), min(paused), max(paused)))
    R.show("warnings / errors", warn)
    logset = R.grep(r"\] LOG: ")   # f1.207: the log set aside (renamed to _previous) at launch (> 20 MB) or in a session (> 40 MB)
    R.show("log set aside (f1.207 - the earlier part is in FF7RCoopMod_Coop_previous.log)", logset, 3)
    if any("could not be renamed" in l for l in logset): R.flag("CHECK", "the log could not be renamed to _previous (open in another program?) - part of it may be gone")
    if crash: R.flag("FAIL", "CRASH in the log")
    if hookf: R.flag("FAIL", "a hook failed to install")
    if hookn: R.flag("CHECK", "a hook note (f1.205 thread pause) - see above")
    # f1.98: the DLL writes no "game closed" line, so a normal close and a hard stop look the same here - the old
    # "closed normally?" check never fired (review E1). A crash the DLL catches shows as CRASH lines above.
    print("  last line at %s (the log has no 'game closed' line - a log that just stops is normal)" % last_t)

    # ---------------- settings / controllers ----------------
    print("\n[2] Settings and controllers")
    dl = R.grep(r"SETTINGS: detailed log ")   # f1.152: DetailedLog=0 = a short log - most rows below stay 0
    R.show("DETAILED LOG (f1.152; always on since f1.200) - off = most event rows below are 0 by design (a build before f1.200 with DetailedLog=0)", dl, 3)
    R.show("SETTINGS", R.grep(r"\] SETTINGS: "), 10)
    R.show("seat picks", R.grep(r"INPUT2 SEATS: seat \d picks|INPUT2 SEATS: seat \d \(player"), 6)
    R.show("players in / out", R.grep(r"PLAYER \d is now (IN|OUT)|CONNECTED|now nobody"), 8)
    R.show("hotkeys ignored outside normal play (HOTKEY)", R.grep(r"\] HOTKEY: "), 4)
    # players 2/3 always have their own command menus (since f1.59); the focus contexts are what can fail
    ctx_on = R.grep(r"CTX: per-player focus contexts (ON|OFF)")
    ctx_state = "not logged" if not ctx_on else ("OFF" if "contexts OFF" in ctx_on[0] else "ON")
    items = bool(R.grep(r"ITEMS: queued item commands count as used"))
    print("  per-player focus contexts: %s; ReserveQueuedItems on: %s" % (ctx_state, "yes" if items else "no"))

    # ---------------- battles ----------------
    print("\n[3] Battles")
    on = R.grep(r"own command system ON \(battle\)")
    ending = R.grep(r"battle is ending")
    R.show("own command systems ON (battle starts)", on, 4)
    R.show("battle endings (held buttons released)", ending, 4)
    R.show("cutscenes / scripted shots", R.grep(r"CUTSCENE: (begins|ends|the game)"), 6)
    # f1.235: a cutscene the game never ended switched the co-op parts off for 11 minutes (f1.234 boss rush 21:48 - 21:59)
    R.show("cutscenes ended by the mod (the game sent no end, f1.235)", R.grep(r"CUTSCENE: ends - .*the game sent no end"), 4)
    cbeg = None
    for l in R.grep(r"CUTSCENE: (begins|ends)"):
        m = TIME.match(l)
        if not m: continue
        if "begins" in l: cbeg = (secs(m.group(1)), l)
        elif cbeg:
            if secs(m.group(1)) - cbeg[0] > 180:
                R.flag("CHECK", "a cutscene lasted %.0f s - from %s (the co-op parts are off during one)" % (secs(m.group(1)) - cbeg[0], cbeg[1][:60]))
            cbeg = None
    if cbeg and R.lines:
        lm = TIME.match(R.lines[-1])
        if lm and secs(lm.group(1)) - cbeg[0] > 180:
            R.flag("CHECK", "a cutscene was still on at the end of the session, %.0f s after it began - %s (no end from the game)" % (secs(lm.group(1)) - cbeg[0], cbeg[1][:60]))
    # f1.277 (long play 2026-11-02): the main menu's 90 deg view was learned as the "normal" field of view - every walking
    # view then counted as a cinematic shot (co-op camera off, split screen hidden, the party-size battle flag stuck)
    for l in R.grep(r"CINEMATIC: the game's normal field of view is"):
        m = re.search(r"is (\d+) deg", l)
        if m and int(m.group(1)) > 75:
            R.flag("CHECK", "the cinematic-shot check learned a normal field of view of %s deg (expected ~60) - walking may count as a cinematic shot: %s" % (m.group(1), l[:80]))
    # f1.281: the fixed 60 deg normal - a narrowed camera for 60 s of walking = a story walk or an area that walks narrower
    for l in R.grep(r"CINEMATIC: LONG"):
        R.flag("CHECK", "a narrowed camera lasted 60 s while player 1 walked - a forced story walk, or an area that walks under 56 deg (co-op camera / split screen off there): %s" % l[:100])
    pb = None
    for l in R.grep(r"PARTYBAL: battle"):
        m = TIME.match(l)
        if not m: continue
        if "battle over" in l:
            if pb and secs(m.group(1)) - pb[0] > 1800:
                R.flag("CHECK", "the party-size battle flag stayed on %.0f s - from %s" % (secs(m.group(1)) - pb[0], pb[1][:60]))
            pb = None
        elif not pb: pb = (secs(m.group(1)), l)
    if pb and R.lines:
        lm = TIME.match(R.lines[-1])
        if lm and secs(lm.group(1)) - pb[0] > 1800:
            R.flag("CHECK", "the party-size battle flag was still on at the end of the session, %.0f s after %s (no 'PARTYBAL: battle over')" % (secs(lm.group(1)) - pb[0], pb[1][:60]))
    R.show("camera left alone - paused / main menu (f1.278)", R.grep(r"CAMERA: (game paused / main menu|play resumed)"), 4)
    R.show("battle with no enemy left - story-shot checks (f1.279)", R.grep(r"CAMERA: (battle with no enemy|battle with enemies again|no enemy left)"), 6)
    R.show("pause / resume", R.grep(r"PAUSE: game"), 4)
    if not on:
        R.flag("CHECK", "no battle in this session")

    # ---------------- test 16: two menus at once ----------------
    print("\n[4] Two command menus at once (players 2/3's own menus, per-player focus contexts)")
    R.show("focus contexts ON/OFF", ctx_on, 2)
    R.show("focus manager check", R.grep(r"FOCUS: focus manager"), 2)
    gate = R.grep(r"CTX: player \d's own command menu (now takes|no longer takes)")
    R.show("own menu takes / releases the pad", gate, 8)
    keys = R.grep(r"CTX: player \d key ")
    per = Counter()
    for l in keys:
        m = re.search(r"CTX: player (\d) key (\S+) (down|repeat|up)", l)
        if m:
            per[(m.group(1), m.group(2).replace("Gamepad_", ""), m.group(3))] += 1
    print("  %-46s %d" % ("made key events", len(keys)))
    for (p, kname, kind), c in sorted(per.items()):
        print("      P%s %-18s %-6s x%d" % (p, kname, kind, c))
    # f1.64: "CTX: player 1 key ..." = TurnKeys=Pad keys made INTO player 1's menu during a KO hand-off turn (f1.60) -
    # moving player 1's path is their job. Only player 2/3's own made keys must leave player 1's path alone.
    moved = [l for l in keys if "player 1's path CHANGED" in l and not re.search(r"CTX: player 1 key ", l)]
    R.show("player 1's path CHANGED by a player 2/3 made key (must be 0)", moved)
    R.show("made keys into player 1's menu (TurnKeys turns, expected to move it)", [l for l in keys if re.search(r"CTX: player 1 key ", l)], 2)
    leafs = Counter(re.search(r"leaf (\S+)", l).group(1) for l in keys if re.search(r"leaf (\S+)", l))
    if leafs: print("      their path's leaf after the key: %s" % dict(leafs))
    fwd = R.grep(r"CMDKEY: a key event for player \d's Command_C goes to player 1's")
    R.show("Command_C key forwarding to player 1 (expect 0)", fwd, 4)
    R.show("own command menu opened / closed", R.grep(r"CMD2 MENU: player \d's own command menu (opened|closed)|CMD2 MENU: on player"), 8)
    R.show("their command system asks for a key", R.grep(r"CMD2 MENU: player \d's command system asks for key"), 4)
    focus = R.grep(r"\] FOCUS: ")
    sw = Counter(re.search(r"in P(\d)'s context \(switched\)", l).group(1) for l in focus if "(switched)" in l)
    calls = Counter(re.search(r"FOCUS: \s*(\w+)\(", l).group(1) for l in focus if re.search(r"FOCUS: \s*(\w+)\(", l))
    print("  %-46s %d  %s" % ("FOCUS lines", len(focus), dict(calls)))
    if sw: print("      calls switched into a player's context: %s" % {"P" + k: v for k, v in sw.items()})
    R.show("pushes of players 2/3's Command_C", [l for l in focus if re.search(r"Push(R)?\(.*owner P[23]", l)], 4)
    R.show("FOCUS on P2/P3 widgets left in P1's context", [l for l in focus if re.search(r"owner P[23].*in P1's context", l)], 4)
    # f1.98: the FAIL used to need "focus contexts ON" to even run, so it could never catch them being OFF (review E1)
    if ctx_state == "OFF":
        R.flag("FAIL", "the per-player focus contexts are OFF - players 2/3's own menus won't get their keys (CTX line)")
    elif ctx_state == "not logged" and on:
        R.flag("CHECK", "battle but no 'CTX: per-player focus contexts' line - did the focus hooks install?")
    if moved:
        R.flag("FAIL", "a made key moved player 1's focus path")
    if fwd:
        R.flag("CHECK", "Command_C key forwarding to player 1 happened")
    if on and ctx_state == "ON" and not gate:
        R.flag("CHECK", "battle but player 2/3's own menu never took their pad (see FOCUS / CMD2 MENU lines)")
    # f1.209: players 2/3's command HUD hidden during cutscenes / scripted shots
    R.show("command HUD hidden / shown for cutscenes (f1.209)", R.grep(r"CMD2: player \d's command HUD (hidden|shown again)"), 6)
    # f1.208: command menu darkening - the game's whole-screen filter (shared screen) or each player's own view (split screen)
    md = R.grep(r"\] MENUDIM: ")
    R.show("MENUDIM: the game's filter / own views darkened (f1.208: MenuDarkenSplitPlayerN, MenuDarken)", md, 8)
    if any("WaitMode" in l and l.endswith("filter shown") for l in md) and any("view darkened" in l for l in md):
        R.flag("CHECK", "the game's whole-screen menu darkening AND a per-view one in the same session (split screen toggled with a menu open?)")

    # ---------------- items ----------------
    print("\n[5] Shared items (ReserveQueuedItems)")
    R.show("ITEMS", R.grep(r"\] ITEMS: "), 8)

    # ---------------- R1 regression ----------------
    print("\n[6] R1 regression check")
    # (f1.191: the "KO hand-off" row is gone - its LEADER lines went with the old hand-off code; KOLEAD (below) covers KOs)
    R.show("switch onto a human blocked / redirected", R.grep(r"LEADER: switch to a human.*(blocked|redirected|refused)"), 4)
    R.show("game's leader change let through on leaving the pause menu (f1.196)", R.grep(r"LEADER: switch to a human.*allowed - the game is leaving"), 4)
    # f1.196: the f1.195 hang - the log stopped right after a blocked switch while leaving the pause menu
    tail = R.lines[-80:] if hasattr(R, "lines") else []
    if any("LEADER: switch to a human" in l and "blocked" in l for l in tail):
        R.flag("CHECK", "the log ends soon after a blocked leader switch - the f1.195 quit-to-title hang looked like this")
    R.show("hot swap (SEATS)", R.grep(r"\] SEATS: "), 6)
    R.show("D-pad character switch (SWITCH)", R.grep(r"\] SWITCH: "), 6)
    # (2026-10-26: the shared-menu turn rows (P2/P3 MENU, P1 PAD) are gone - that fallback was removed; a line like that now = old code)
    if R.grep(r"P[23] MENU: (pad pressed|player \d owns|released)|P1 PAD: |TURNKEYS: |KEYBOARD: key message"):
        R.flag("PROBLEM", "a shared-menu turn line appeared - that code was removed 2026-10-26 (an old DLL installed?)")
    R.show("palettes (SHORTCUT)", R.grep(r"P[23] SHORTCUT: palette (opened|released)|palette issued a command"), 6)
    R.show("action passes (attack / dodge / guard / ability)", R.grep(r"Action pass \d"), 4)
    R.show("field attacks", R.grep(r"FIELD: player \d attack"), 3)
    R.show("join / leave (keys, buttons, controller lost)", R.grep(r"is now (IN|OUT) \("), 4)

    print("\n[6b] Target cursor (TGTPICK) / player 1's reticle (RETICLE)")
    R.show("player 1's reticle hidden / shown over windows", R.grep(r"\] RETICLE: "), 6)
    R.show("TGTPICK (f1.43): the target players 2/3 pick", R.grep(r"TGTPICK: "), 40)
    R.show("LOCKON / split-screen lock-on aim / leading player's marker (f1.52)", R.grep(r"LOCKON: |CAMERA: player 1 (locked|not locked)|MARKER: player \d leads"), 40)
    R.show("DAMAGE / ATTACKRATE balance hooks (f1.58 / f1.62)", R.grep(r"DAMAGE: |ATTACKRATE: "), 30)
    R.show("ATTACKRATE DIAG counters (f1.64)", R.grep(r"ATTACKRATE DIAG: "), 20)
    R.show("Enemy settings per party size (f1.151) - which set, waits for a battle to end", R.grep(r"PARTYBAL: |SETTINGS: [123]-character party|SETTINGS: active enemy settings|SETTINGS: reloaded during a battle"), 40)
    R.show("PAGES own menu pages (f1.67)", R.grep(r"PAGES: "), 30)
    # (f1.184: KOMENU / KORESTORE / KOTRACK rows dropped - the KO hand-off is gone since f1.126; KOLEAD replaces it)
    R.show("KOLEAD player 1 down - the lead stays with player 1 (f1.126)", R.grep(r"\] KOLEAD: "), 8)
    # f1.282: the Failed Experiment gate hand-off starts only on the stairs' line
    R.show("QUIETLEAD gate hand-off starts (f1.282: on the stairs only)", R.grep(r"QUIETLEAD: player \d.s .*the hand-off starts"), 8)
    R.show("QUIETLEAD at the gate but off the stairs (no hand-off, f1.282)", R.grep(r"QUIETLEAD: .*not on the stairs' line"), 8)
    R.show("AUTOSTEP gate steering ends", R.grep(r"\] AUTOSTEP: over"), 8)
    if len(R.grep(r"\] AUTOSTEP: the leader is steered")) > len(R.grep(r"\] AUTOSTEP: over")): R.flag("CHECK", "an AUTOSTEP started but never ended (f1.282 bug: the steering never switched on)")
    if R.grep(r"QUIETLEAD: over after .*no jump cutscene within"): R.flag("CHECK", "a gate hand-off ended without the jump (QUIETLEAD ... no jump cutscene within 5 s)")
    if R.grep(r"KOLEAD: .*(NOT|didn't|failed)"): R.flag("CHECK", "KOLEAD reported a problem keeping the lead with player 1")
    perf = R.grep(r"\] PERF: ")
    R.show("PERF frame rate every 30 s (f1.103)", perf, 40)
    lows = [l for l in perf if re.search(r"- (\d+) fps", l) and int(re.search(r"- (\d+) fps", l).group(1)) < 45]
    if lows: R.flag("CHECK", "%d PERF line(s) below 45 fps on average - see PERF" % len(lows))
    R.show("NAMECHK enemy names from the game (f1.61)", R.grep(r"NAMECHK: "), 30)
    R.show("SMOOTH badges / pointers (f1.53)", R.grep(r"SMOOTH: |HUD: P2 / P3 badges|HUD: small font"), 20)

    # ---------------- enemy targeting (f1.92+) ----------------
    print("\n[6c] Who enemies go after (ATTENTION rules / TARGETLOG)")
    R.show("SETTINGS for the targeting rules", R.grep(r"SETTINGS: enemies (stay on|go after|prefer)|enemies treat humans"), 4)
    stays = R.grep(r"ATTENTION: .* - stays on them \(StayOnHitChance\)")
    turns = R.grep(r"ATTENTION: .* turns on .*TurnOnHeavyDamage\)")
    weak = R.grep(r"ATTENTION: .* goes after the weakened ")
    R.show("stays on the player it hit (StayOnHitChance)", stays, 3)
    R.show("turns on a heavy hitter (TurnOnHeavyDamage)", turns, 3)
    R.show("goes after a weakened player (PreferWeakTargets)", weak, 3)
    R.show("30 s summaries", R.grep(r"ATTENTION: last 30 s"), 4)
    params = R.grep(r"ATTENTION: On(Give|Take)Damage params")
    R.show("damage events seen (params lines, only when StayOnHit / TurnOnHeavy is on)", params, 2)
    # a rule switched on in the settings but never seen in a battle is worth a look (it may just not have come up)
    m = next((re.search(r"stay on a player they hit (\d+)% of the time, turn on a player dealing (\d+)%", l)
              for l in R.grep(r"SETTINGS: enemies stay on")), None)
    stay_on, heavy_on = (int(m.group(1)) > 0, int(m.group(2)) > 0) if m else (False, False)
    weak_on = bool(R.grep(r"SETTINGS: enemies go after a weakened player"))
    if on:
        if stay_on and not stays and not R.grep(r"enemies landed [1-9]"):
            R.flag("CHECK", "StayOnHitChance is on but no enemy landed a hit on its target (ATTENTION) - did a fight happen?")
        if heavy_on and not turns:
            R.flag("CHECK", "TurnOnHeavyDamage is on but no enemy turned on a heavy hitter - hit harder, or check the % (ATTENTION)")
        if (stay_on or heavy_on) and not params:
            R.flag("CHECK", "StayOnHitChance / TurnOnHeavyDamage is on but the enemies' damage events never arrived (no ATTENTION params lines)")
        if weak_on and not weak and not R.grep(r"ATTENTION: last 30 s - [1-9]\d* new target picks while"):
            R.flag("CHECK", "PreferWeakTargets is on but no player got below WeakTargetHealth while enemies picked targets")
    tl = R.grep(r"TARGETLOG: \S.* -> \S.*, was ")
    who = Counter(re.search(r" -> [^(]*\((P\d|AI|-)", l).group(1) for l in tl if re.search(r" -> [^(]*\((P\d|AI|-)", l))
    print("  %-46s %d  %s" % ("TARGETLOG target changes (temporary)", len(tl), dict(who) if who else ""))
    R.show("TARGETLOG shares (30 s / battle over / session, with moving %)", R.grep(r"TARGETLOG: (last 30 s|battle over|session so far)"), 6)
    R.show("TARGETLOG hate rates", R.grep(r"TARGETLOG: hate rates"), 2)
    if R.grep(r"TARGETLOG: hate rates can't be read"):
        R.flag("CHECK", "TARGETLOG couldn't read the hate rates - this game version's code doesn't match")

    # ---------------- button icons (f1.88+) ----------------

    # ---------------- bosses (2026-10-30: one summary per boss - targets, commands, moves, phases) ----------------
    print("\n[6g] Bosses (BOSSTARGET / BOSSRANGE / BOSSCMD / BOSSACT / BOSSHP)")
    bosses = []
    for l in R.grep(r"BOSSTARGET: (\S+) is a boss"):
        nm = re.search(r"BOSSTARGET: (\S+) is a boss", l).group(1)
        if nm not in bosses: bosses.append(nm)
    print("  %-46s %d  %s" % ("bosses seen", len(bosses), ", ".join(b.split("_")[0] + " " + (b.split("_")[2] if len(b.split("_")) > 2 else "") for b in bosses)))
    for b in bosses:
        short = b.split("_")[0] + " " + (b.split("_")[2] if len(b.split("_")) > 2 else b)
        mine = [l for l in R.lines if b in l]
        print("  --- %s (%d lines)" % (short, len(mine)))
        picks = Counter()
        for l in mine:
            m = re.search(r"BOSSTARGET: \S+ -> (\S+) \((\w+)\) instead of", l)
            if m: picks["%s (%s)" % (m.group(1), m.group(2))] += 1
        if picks: print("      mod's picks: %s" % ", ".join("%s %d" % kv for kv in picks.most_common()))
        held = [l for l in mine if "(its own choice)" in l]
        if held: print("      left to the game (phase / cutscene / special move): %d" % len(held))
        for l in [l for l in mine if "BOSSRANGE: battle " in l]:
            print("      " + l[l.find("BOSSRANGE:"):][:230])
            for m in re.finditer(r"(\w+) (\d+) s \((\d+)%\)", l):
                if int(m.group(3)) >= 80 and int(m.group(2)) >= 30:
                    R.flag("CHECK", "%s spent %s%% of its target time on %s (%s s) - attention not spread?" % (short, m.group(3), m.group(1), m.group(2)))
        for l in [l for l in mine if "BOSSCMD:" in l]:
            print("      " + l[l.find("BOSSCMD:"):][:300])
        unk = [l for l in mine if "a mode its script doesn't give as a fixed value" in l]
        if unk: R.flag("CHECK", "%s: SetTargetByType mode could not be read (f1.225) - %s" % (short, unk[0][-60:]))
        tab = [l for l in mine if "BOSSACT: battle summary" in l]
        if tab:
            i = R.lines.index(tab[-1]); rows = []
            for l2 in R.lines[i + 1:i + 40]:
                if "BOSSACT:   " not in l2: break
                rows.append(l2)
            print("      moves (BOSSACT table, %d):" % len(rows))
            for l2 in rows: print("        " + l2[l2.find("BOSSACT:   ") + 11:][:220])
        acts = [l for l in mine if re.search(r"BOSSACT: \S+ - .* \| aimed at", l) and "it never started" not in l]
        if acts:
            noc = [l for l in acts if "NO CONTACT" in l]
            far = [l for l in acts if (lambda m: m and float(m.group(1)) > 8.0)(re.search(r"m when picked, ([\d.]+) m at its start", l))]
            farnoc = [l for l in far if "NO CONTACT" in l]
            print("      moves ended: %d, NO CONTACT %d, started over 8 m away %d (of them NO CONTACT %d)" % (len(acts), len(noc), len(far), len(farnoc)))
            if len(far) >= 4 and len(farnoc) * 2 > len(far):
                R.flag("CHECK", "%s: most moves started over 8 m from their target reached nobody (%d of %d) - far targets wasted?" % (short, len(farnoc), len(far)))
        sus = [l for l in mine if "SUSPENDED" in l]
        lost = [l for l in sus if "TARGET LOST" in l]
        if sus: print("      moves given up: %d (target lost %d)" % (len(sus), len(lost)))
        if len(lost) >= 3: R.flag("CHECK", "%s gave up %d moves with TARGET LOST - see BOSSACT SUSPENDED" % (short, len(lost)))
        phases = [l for l in mine if re.search(r"HP floor (set to|CLEARED)|PHASE \d+ -> \d+|on the game's HP floor|phase-start line|off the HP floor|back$", l)]
        R.show("      phase lines / guard", phases, 12)
        stuck = [l for l in mine if "NOT going down" in l]
        if stuck: print("      'NOT going down' lines: %d" % len(stuck))
        # a phase that never came: on the floor / line, then no floor change and no cutscene for 60 s
        locks = [(secs(TIME.match(l).group(1)), l) for l in mine if TIME.match(l) and ("on the game's HP floor -" in l or "now on the game's HP floor too" in l)]
        ev = [secs(TIME.match(l).group(1)) for l in R.lines if TIME.match(l) and (("CUTSCENE: begins" in l) or (b in l and re.search(r"HP floor (set to|CLEARED)|battle over", l)))]
        for t, l in locks:
            if not any(t < e <= t + 60 for e in ev):
                R.flag("CHECK", "%s sat on its HP floor at %s and no cutscene / floor change / battle end followed within 60 s - stuck phase?" % (short, l[1:13]))
        for l in [l for l in mine if "on its phase-start line without reaching the floor" in l]:
            t = secs(TIME.match(l).group(1)) if TIME.match(l) else 0
            if any(t - 60 <= e <= t for e in ev): continue   # (f1.233 S16: the phase started AT the line - a cutscene came during the hold)
            R.flag("INFO", "%s stayed 30 s on its phase-start line without reaching its floor (%s) - its phase starts some other way, or nobody's hits could push it past (f1.229)" % (short, l[1:13]))
        idle = [l for l in mine if "BOSSIDLE" in l]
        if idle: print("      BOSSIDLE (script quiet 5 s+): %d" % len(idle))
        dm = [l for l in R.lines if "a boss takes" in l]
        if dm and b == bosses[0]: print("      'a boss takes N instead of M' (f1.227, all bosses): %d" % len(dm))
        cut = [l for l in R.lines if "a hit on a boss by" in l and "cut from" in l]
        if cut and b == bosses[0]:
            R.show("      hits cut by a boss's HP lines (f1.229, all bosses)", cut, 6)
    if bosses and not R.grep(r"BOSSCMD: "):
        R.flag("INFO", "boss fight(s) but no BOSSCMD line - the battle-end line comes with f1.225+ (or the battle never ended)")
    p23 = R.grep(r"hit counts as the player-controlled character's")
    if p23: print("  players 2/3's hits counted as player-controlled (f1.229): %d line(s) (first 5 only are logged)" % len(p23))
    print("\n[6d] Players 2/3's button icons (ICONS)")
    R.show("ICONS choices / style", R.grep(r"ICONS: (button icons - |players 2/3's own button icons|the game set its icon style|not available)"), 6)
    rebuilt = R.grep(r"ICONS: text with button icons rebuilt - ")
    own = Counter(re.search(r"owner (\d)", l).group(1) for l in rebuilt if re.search(r"owner (\d)", l))
    print("  %-46s %d  %s" % ("texts rebuilt (owner: count)", len(rebuilt), {"owner " + k: v for k, v in sorted(own.items())}))
    R.show("full rebuilds (battle start / F6 / controller change)", R.grep(r"ICONS: (texts with button icons rebuilt|every text with button icons)"), 3)
    if R.grep(r"ICONS: not available"):
        R.flag("CHECK", "ICONS not available - everyone sees player 1's button icons")

    # ---------------- reticle / temporary diagnostics ----------------
    R.show("TALKSCENE talking scenes (field controls; f1.120 - detailed log)", R.grep(r"\] TALKSCENE: "), 12)
    R.show("MSGLOG battle messages moved (f1.161 - MessageLogX / MessageLogY)", R.grep(r"\] MSGLOG: |MessageLogX"), 12)
    R.show("GAMEPOPUP players 2/3's copies of the game's numbers + player 1's hidden over windows (f1.173)", R.grep(r"\] GAMEPOPUP: "), 30)
    R.show("GAMEBARS players 2/3's game enemy bars + player 1's hidden over windows (f1.175)", R.grep(r"\] GAMEBARS: "), 30)
    R.show("GAMERETICLE players 2/3's game reticle (f1.176)", R.grep(r"\] GAMERETICLE: "), 30)
    R.show("TGTFILTER enemies left out of players 2/3's targets (f1.110)", R.grep(r"\] TGTFILTER: "), 12)
    R.show("DYNQ dynamic window quality steps (f1.113)", R.grep(r"\] DYNQ: |dynamic window quality"), 20)
    R.show("PRESSURE players 2/3's 'Pressured' from player 1's enemy panel (f1.114)", R.grep(r"\] PRESSURE: "), 20)
    R.show("PARTYHUD individual party panels (f1.117)", R.grep(r"\] PARTYHUD: |party health / ATB panel"), 12)
    if R.grep(r"DYNQ: .*NOT available"): R.flag("CHECK", "DYNQ: half-rate windows not available (SetComponentTickInterval missing)")
    R.show("CINEMATIC cinematic shots (game camera)", R.grep(r"\] CINEMATIC: "), 8)
    R.show("OWNHUD players 2/3's own command HUD", R.grep(r"\] OWNHUD: "), 8)
    R.show("CAMERA window cameras / walls / lens", R.grep(r"\] CAMERA: |\] LENS: "), 8)

    # ---------------- f1.178 - f1.184: the game's own HUD for players 2/3, targets, stagger ----------------
    print("\n[6f] The game's own HUD for players 2/3, targets from the game's list, stagger (f1.178 - f1.190)")
    if R.grep(r"HOOK FAILED stagger start|stagger start leader check.*bytes differ"):
        R.flag("PROBLEM", "the stagger-start patch (0x967374) was NOT installed - players 2/3 can't start a stagger")
    R.show("stagger-start patch (f1.182)", R.grep(r"Patched stagger start leader check"), 2)
    tl = R.grep(r"TGTFILTER: the game's target list")
    R.show("TGTFILTER the game's target list (f1.181)", tl, 8)
    if R.grep(r"TGTFILTER: the game's target list \(0x9C9F50\) NOT"):
        R.flag("CHECK", "the game's target list didn't match - players 2/3 fell back to the enemy list")
    short = [l for l in tl if re.search(r"(\d+) enemy\(ies\) targetable now \(of (\d+)", l)
             and int(re.search(r"(\d+) enemy\(ies\) targetable now \(of (\d+)", l).group(1)) < int(re.search(r"targetable now \(of (\d+)", l).group(1))]
    if short: R.flag("INFO", "%d time(s) fewer enemies were targetable than were in the battle (down / hidden / untargetable ones) - see TGTFILTER" % len(short))
    heal = R.grep(r"GAMERETICLE: player \d's reticle (was hidden by something else|.* - made again)")
    R.show("reticle self-checks fired (f1.181)", heal, 6)
    if heal: R.flag("INFO", "the reticle self-check brought a reticle back %d time(s)" % len(heal))
    R.show("GAMERETICLE STATE per player (f1.181, detailed log)", R.grep(r"GAMERETICLE STATE: "), 10)
    # enemy bar gaps: a STATE line says the bar isn't shown while the point IS in the window - how long until the next line
    bs = [(secs(TIME.match(l).group(1)), l) for l in R.grep(r"GAMEBARS STATE: ") if TIME.match(l)]
    R.show("GAMEBARS STATE per player (f1.183, detailed log)", [l for _, l in bs], 10)
    # (f1.195: not while the game is paused - nothing is placed then; the f1.195 test's pause menu was flagged)
    pz, pstart = [], None
    for l in R.grep(r"PAUSE: game (PAUSED|running again)"):
        m = TIME.match(l)
        if not m: continue
        if "PAUSED" in l: pstart = secs(m.group(1))
        elif pstart is not None: pz.append((pstart, secs(m.group(1)))); pstart = None
    if pstart is not None: pz.append((pstart, 1e12))
    # (f1.209: not during a cutscene either - the f1.208 Airbuster end-of-fight cutscene 19:30:49 - 19:31:28 was flagged)
    cstart = None
    for l in R.grep(r"CUTSCENE: (begins|ends)"):
        m = TIME.match(l)
        if not m: continue
        if "begins" in l: cstart = secs(m.group(1))
        elif cstart is not None: pz.append((cstart, secs(m.group(1)))); cstart = None
    if cstart is not None: pz.append((cstart, 1e12))
    def paused_at(t): return any(a - 0.5 <= t <= b + 0.5 for a, b in pz)
    battle_over = [secs(TIME.match(l).group(1)) for l in R.grep(r"PARTYBAL: battle over|PIP: battle over") if TIME.match(l)]
    for pn in ("2", "3"):
        mine = [(t, l) for t, l in bs if "player %s on" % pn in l]
        worst = (0.0, None)
        for k, (t, l) in enumerate(mine):
            m = re.search(r"visibility (-?\d+)", l)
            vis = int(m.group(1)) if m else -1
            bad = ("NOT in their window" not in l) and ("placing call NO" in l or vis not in (0, 3, 4)) and not paused_at(t)
            if bad:
                end = mine[k + 1][0] if k + 1 < len(mine) else t
                # (f1.230: the gap ends when the battle does - the f1.21x logs flagged gaps that ran across battles)
                over = [bt for bt in battle_over if t < bt < end]
                if over: end = over[0]
                if end - t > worst[0]: worst = (end - t, l)
        if worst[0] >= 5.0:
            R.flag("CHECK", "player %s's enemy bar wasn't shown for %.0f s while their target was in their window: %s" % (pn, worst[0], worst[1][:160]))
    # f1.189: GAMEBARS SEE - what the bar looks like on screen (the f1.188 test: P2's bar "shown" by the log but not seen)
    see = R.grep(r"GAMEBARS SEE: ")
    # (f1.191: "no geometry" / "attached NO" are normal for these bars - a bar not drawn yet has no geometry, and they sit in
    #  a canvas, not straight on the viewport - so they are not flagged)
    R.show("GAMEBARS SEE per player (f1.189, detailed log)", see, 12)
    for pn in ("2", "3"):
        mine = [l for l in see if "player %s's bar" % pn in l]
        odd = []
        for l in mine:
            m = re.search(r"color alpha (-?[\d.]+), root visibility (-?\d+), attached (\S+)", l)
            why = []
            if "NOT inside their window" in l: why.append("off their window")
            if m and float(m.group(1)) < 0.1: why.append("color alpha %s" % m.group(1))
            if m and m.group(2) in ("1", "2"): why.append("root hidden")
            if "ALSO one of player 1's" in l: why.append("shared with player 1's bars")
            if why: odd.append("%s (%s)" % (l[:15], ", ".join(why)))
        if odd:
            R.flag("CHECK", "player %s's bar looked wrong on screen %d time(s), first: %s" % (pn, len(odd), odd[0]))
    kept = R.grep(r"GAMEBARS: player \d's enemy bar kept above their window's bottom")
    R.show("GAMEBARS bars kept above the window's bottom (f1.190)", kept, 4)
    # stagger: a person filled an enemy's gauge - did the stagger start within 2 s?
    st = [(secs(TIME.match(l).group(1)), l) for l in R.grep(r"STAGGERLOG: ") if TIME.match(l)]
    R.show("STAGGERLOG hits by people + 30 s sums (f1.181, detailed log)", [l for _, l in st if "last 30 s" in l], 8)
    R.show("STAGGERLOG staggers started", [l for _, l in st if "STAGGERED now" in l], 8)
    for k, (t, l) in enumerate(st):
        m = re.search(r"STAGGERLOG: (\S+) hit by \S+ \(player (\d)\).* gauge ([\d.]+) / ([\d.]+)", l)
        if not m or "staggered" in l.split(" gauge ", 1)[-1].lower():
            continue   # no parse, or the enemy was already / just staggered
        en, pl, now, mx = m.group(1), m.group(2), float(m.group(3)), float(m.group(4))
        if mx <= 0 or now < mx - 0.05 or pl == "1":
            continue
        later = [l2 for t2, l2 in st[k + 1:] if t2 - t <= 2.0 and en in l2 and "STAGGERED" in l2]
        if not later:
            R.flag("CHECK", "player %s filled %s's stagger gauge (%.1f / %.1f) at %s but no stagger started within 2 s"
                   % (pl, en, now, mx, TIME.match(l).group(1)))
    print("\n[6e] Removed diagnostics (should all be 0)")
    diag = [("EDGEDIAG", "white-line check, f1.88", r"\] EDGEDIAG: "),
            ("RETANIM", "game reticle animation, f1.95", r"\] RETANIM[ :]"),
            ("ATTENTION DIAG", "gone since f1.92", r"ATTENTION DIAG: "),
            ("CAMWALL", "gone since f1.153", r"\] CAMWALL: "),
            ("NEWFN", "gone since f1.153", r"\] NEWFN: "),
            ("TGTDIAG", "gone since f1.153", r"\] TGTDIAG: "),
            ("TALKDIAG", "gone since f1.153", r"\] TALKDIAG: "),
            ("STAGGER RE", "gone since f1.159", r"\] STAGGER RE: "),
            ("STAGGER DIAG", "gone since f1.159", r"\] STAGGER DIAG: "),
            ("DIAG KEYS", "gone since f1.167", r"\] DIAG KEYS: "),
            ("DMGNUM", "drawn numbers, gone since f1.178", r"\] DMGNUM: "),
            ("POPUP", "popup research, gone since f1.185", r"\] POPUP: "),
            ("ESTATUS", "enemy bar research, gone since f1.185", r"\] ESTATUS: ")]
    found = []
    for name, what, pat in diag:
        h = R.grep(pat)
        print("  %-46s %d" % ("%s (%s)" % (name, what), len(h)))
        if h: found.append(name)

    # ---------------- verdict ----------------
    print("\n[7] Summary")
    if found:
        print("  INFO  temporary diagnostics in this session: %s" % ", ".join(found))
    if not R.flags:
        print("  nothing automatic to flag - now read the whole session (rule: read the WHOLE log)")
    for lvl, t in R.flags:
        print("  %-5s %s" % (lvl, t))


if __name__ == "__main__":
    main()
