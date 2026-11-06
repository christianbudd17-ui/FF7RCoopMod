// input2/seats.cpp - the new seats module. See seats.h. Changes on the game thread (the glue's per-frame tick); Get() is
// also called from the game's input thread, so the resolved seats are behind a lock.
#include "seats.h"
#include <cstdio>
#include <cstring>
#include <cctype>
#include <cstdlib>

namespace in2 {
namespace seats {
namespace {

Want g_want[3] = {{SEAT_NONE, 0, 0, 0, ""}, {SEAT_NONE, 0, 0, 0, ""}, {SEAT_NONE, 0, 0, 0, ""}};
Seat g_seat[3] = {{SEAT_NONE, false, false, 0, "nobody"}, {SEAT_NONE, false, false, 0, "nobody"}, {SEAT_NONE, false, false, 0, "nobody"}};
LogFn g_log = nullptr;
SRWLOCK g_seatLock = SRWLOCK_INIT;   // Get() is called from the game's input thread too (XInputGetState / PadGet)   // taken from the devices module's host (set by the glue through SetLogger below)


void WantText(const Want& w, char* out, size_t n)
{
    switch (w.kind)
    {
    case SEAT_NONE: snprintf(out, n, "nobody"); break;
    case SEAT_KEYBOARD: snprintf(out, n, "keyboard"); break;
    case SEAT_AUTO: snprintf(out, n, "any free controller"); break;
    case SEAT_DEVICE:
        if (w.path[0]) snprintf(out, n, "the controller at %s", w.path);
        else snprintf(out, n, "%04X:%04X #%d", w.vid, w.pid, w.nth > 0 ? w.nth : 1);
        break;
    }
}

bool Matches(const Want& w, const Device& d, int nthSeen)
{
    if (!d.present) return false;
    if (w.path[0]) return !strcmp(w.path, d.path);
    return d.vid == w.vid && d.pid == w.pid && nthSeen == (w.nth > 0 ? w.nth : 1);
}

}   // namespace

void SetLogger(LogFn f) { g_log = f; }

// ParseWant: inline in seats.h (review #30 - the settings reader uses it too)

void SetWant(int seat, const Want& w)
{
    if (seat < 0 || seat > 2) return;
    g_want[seat] = w;
}

bool TieToDevice(int seat, int deviceId)
{
    if (seat < 0 || seat > 2 || deviceId <= 0) return false;
    for (int i = 0, n = Count(); i < n; ++i)
    {
        Device d; if (!in2::Get(i, &d) || d.id != deviceId) continue;
        Want w; memset(&w, 0, sizeof(w)); w.kind = SEAT_DEVICE; snprintf(w.path, sizeof(w.path), "%s", d.path);
        w.vid = d.vid; w.pid = d.pid;   // the model too (re-plug into another port, Update phase B)
        g_want[seat] = w;
        return true;
    }
    return false;
}

void Trade(int a, int b)
{
    if (a < 0 || a > 2 || b < 0 || b > 2 || a == b) return;
    Want t = g_want[a]; g_want[a] = g_want[b]; g_want[b] = t;
    if (g_log) g_log("INPUT2 SEATS: seats %d and %d traded controllers", a + 1, b + 1);
}

void Update()
{
    int n = Count();
    static Device devs[64];
    if (n > 64) n = 64;
    for (int i = 0; i < n; ++i) if (!in2::Get(i, &devs[i])) devs[i].present = false;
    bool used[64] = {};
    int pick[3] = {-1, -1, -1};
    // seats with a fixed pick first, then the Auto seats (in seat order), so Auto never takes a controller a later
    // seat names. f1.28 (code review B1): four phases -
    //  A. seats that keep a controller by its path take it if it's here;
    //  B. a seat whose controller isn't at its path any more takes an unclaimed controller of the SAME MODEL (VID:PID) that
    //     no other seat keeps - the same pad plugged into another USB port gets another path (pads without a serial
    //     number), and before this the seat waited forever while a waiting Auto seat could grab it (player 1's pad
    //     driving player 3). The seat keeps the new path from then on;
    //  C. seats by VID:PID (+ nth) that haven't picked yet;
    //  D. Auto seats.
    for (int s = 0; s < 3; ++s)   // A
    {
        const Want& w = g_want[s];
        if ((w.kind != SEAT_DEVICE) || !w.path[0]) continue;
        for (int i = 0; i < n; ++i) if (!used[i] && devs[i].present && !strcmp(w.path, devs[i].path)) { pick[s] = i; used[i] = true; break; }
    }
    for (int s = 0; s < 3; ++s)   // B
    {
        Want& w = g_want[s];
        if (w.kind != SEAT_DEVICE || !w.path[0] || pick[s] >= 0 || (!w.vid && !w.pid)) continue;
        for (int i = 0; i < n; ++i)
        {
            if (used[i] || !devs[i].present || devs[i].vid != w.vid || devs[i].pid != w.pid) continue;
            bool kept = false;   // another seat keeps exactly this controller (its path) - never take that one
            for (int o = 0; o < 3; ++o) if (o != s && g_want[o].kind == SEAT_DEVICE && g_want[o].path[0] && !strcmp(g_want[o].path, devs[i].path)) kept = true;
            if (kept) continue;
            if (g_log) g_log("INPUT2 SEATS: seat %d's controller (%04X:%04X) isn't at its old USB port - taking the same model at %s", s + 1, w.vid, w.pid, devs[i].path);
            snprintf(w.path, sizeof(w.path), "%s", devs[i].path);
            pick[s] = i; used[i] = true; break;
        }
    }
    for (int s = 0; s < 3; ++s)   // C
    {
        const Want& w = g_want[s];
        if (w.kind != SEAT_DEVICE || w.path[0]) continue;
        int nthSeen = 0;
        for (int i = 0; i < n; ++i)
        {
            if (!devs[i].present) continue;
            if (devs[i].vid == w.vid && devs[i].pid == w.pid) ++nthSeen;   // counts taken ones too
            if (used[i]) continue;
            if (Matches(w, devs[i], nthSeen)) { pick[s] = i; used[i] = true; break; }
        }
    }
    for (int s = 0; s < 3; ++s)   // D
    {
        if (g_want[s].kind != SEAT_AUTO) continue;
        for (int i = 0; i < n; ++i)
        {
            if (used[i] || !devs[i].present) continue;
            if (!devs[i].gamepad) continue;   // gamepads only (the unused host veto SetAutoFilter removed)
            pick[s] = i; used[i] = true; break;
        }
    }
    for (int s = 0; s < 3; ++s)
    {
        const Want w = g_want[s];
        Seat now; memset(&now, 0, sizeof(now));
        if (w.kind == SEAT_NONE) { now.kind = SEAT_NONE; strcpy(now.desc, "nobody"); }
        else if (w.kind == SEAT_KEYBOARD) { now.kind = SEAT_KEYBOARD; strcpy(now.desc, "keyboard"); }
        else if (pick[s] >= 0)
        {
            const Device& d = devs[pick[s]];
            if (!g_want[s].path[0] || g_want[s].kind == SEAT_AUTO)
            {   // first pick by VID:PID / Auto: from now on the seat keeps exactly this controller (by its path), so
                // unplugging another one with the same VID:PID never moves controllers between seats. f1.28: its model
                // is kept too (phase B).
                g_want[s].kind = SEAT_DEVICE; snprintf(g_want[s].path, sizeof(g_want[s].path), "%s", d.path);
            }
            g_want[s].vid = d.vid; g_want[s].pid = d.pid;
            now.kind = SEAT_DEVICE; now.deviceId = d.id;
            Describe(d, now.desc, sizeof(now.desc));
        }
        else
        {
            now.kind = SEAT_NONE; now.waiting = true; now.autoWaiting = w.kind == SEAT_AUTO;
            char wt[200]; WantText(w, wt, sizeof(wt));
            snprintf(now.desc, sizeof(now.desc), "waiting for %s", wt);
        }
        bool logIt = now.kind != g_seat[s].kind || now.deviceId != g_seat[s].deviceId || now.waiting != g_seat[s].waiting;
        if (logIt || now.autoWaiting != g_seat[s].autoWaiting || strcmp(now.desc, g_seat[s].desc))   // a hot swap of two waiting seats
        {
            if (g_log && logIt) g_log("INPUT2 SEATS: seat %d (player %d): %s", s + 1, s + 1, now.desc);
            AcquireSRWLockExclusive(&g_seatLock); g_seat[s] = now; ReleaseSRWLockExclusive(&g_seatLock);
        }
    }
}

bool Get(int seat, Seat* out)
{
    if (seat < 0 || seat > 2 || !out) return false;
    AcquireSRWLockShared(&g_seatLock); *out = g_seat[seat]; ReleaseSRWLockShared(&g_seatLock);
    return true;
}

bool GetBrief(int seat, SeatBrief* out)
{
    if (seat < 0 || seat > 2 || !out) return false;
    AcquireSRWLockShared(&g_seatLock);
    const Seat& s = g_seat[seat];
    out->kind = s.kind; out->waiting = s.waiting; out->autoWaiting = s.autoWaiting; out->deviceId = s.deviceId;
    ReleaseSRWLockShared(&g_seatLock);
    return true;
}

}   // namespace seats
}   // namespace in2
