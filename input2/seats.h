// input2/seats.h - the new seats module: which controller sits in which seat, by controller IDENTITY.
//
// Seat 0 = player 1's (the leader seat), 1 = player 2's, 2 = player 3's. A seat holds the keyboard, nothing,
// or one physical controller from the devices module (in2.h). A seat keeps "its" controller by identity (device
// path), so unplugging and re-plugging - or Windows shuffling XInput slot numbers - never moves a controller to
// another player; when it comes back the seat takes it again. Two seats never share a controller (earlier seat wins).
//
// [Input2] Seat1/Seat2/Seat3 (written by the settings app) pick each seat's controller; the glue
// (input/40b_input2_glue.inc) hands the seats' readings to the game and to players 2/3.
#pragma once
#include "in2.h"
#include <cstdio>
#include <cstring>
#include <cctype>

namespace in2 {
namespace seats {

enum Kind : uint8_t { SEAT_NONE = 0, SEAT_KEYBOARD = 1, SEAT_DEVICE = 2, SEAT_AUTO = 3 };

// What a seat asks for.
struct Want
{
    Kind     kind;          // SEAT_DEVICE: by path if path[0], else by VID:PID (+ nth); SEAT_AUTO = first free controller
                            // (VID:PID and Auto turn into that controller's path at the first pick: the seat keeps it)
    uint16_t vid, pid;
    int      nth;           // 1 = the first present controller with that VID:PID (by device number), 2 = the second ...
    char     path[200];     // lower case device path (identity)
};

// What a seat has right now.
struct Seat
{
    Kind kind;              // SEAT_DEVICE only when the controller is present; a missing one = SEAT_NONE + waiting
    bool waiting;           // wants a controller that isn't here (unplugged / not found yet)
    bool autoWaiting;       // waiting on Auto that never picked one (no controller of its own yet - not "unplugged")
    int  deviceId;          // in2 Device::id (0 = none)
    char desc[180];         // "keyboard", "nobody", "#3 DualSense 054C:0CE6 (...)", "waiting for 054C:0CE6"
};

void SetLogger(LogFn f);                    // where its log lines go (the mod's log)
// Auto picks only gamepads (Device::gamepad), seats with a fixed pick are resolved first (so an Auto seat never
// takes another seat's named controller), and the host can veto a controller for an Auto seat (one another seat's
// old-style settings will take - the glue's untied [PlayerN] picks). nullptr = no veto.
// "Keyboard", "None", "Auto", "054C:0CE6", "054C:0CE6#2", "path:\\?\hid#vid_054c&pid_0ce6#..." (case-insensitive).
// (review #30): inline here so the settings reader (settings/03 SeatWant) uses this same parser - THE one reading of SeatN
inline bool ParseWant(const char* text, Want* out)
{
    if (!text || !out) return false;
    Want w; memset(&w, 0, sizeof(w));
    char t[220]; strncpy(t, text, sizeof(t) - 1); t[sizeof(t) - 1] = 0;
    { char* sc = strchr(t, ';'); if (sc) *sc = 0; }   // "Seat2=0D22:0C34   ; msi GC30" - a note after ; is ignored
    char* s = t; while (*s == ' ' || *s == '\t') ++s;
    size_t l = strlen(s); while (l && (s[l - 1] == ' ' || s[l - 1] == '\t' || s[l - 1] == '\r' || s[l - 1] == '\n')) s[--l] = 0;
    char low[220]; strcpy(low, s); for (char* p = low; *p; ++p) *p = (char)tolower((unsigned char)*p);
    if (!low[0] || !strcmp(low, "none")) w.kind = SEAT_NONE;
    else if (!strcmp(low, "keyboard")) w.kind = SEAT_KEYBOARD;
    else if (!strcmp(low, "auto")) w.kind = SEAT_AUTO;
    else if (!strncmp(low, "path:", 5)) { w.kind = SEAT_DEVICE; snprintf(w.path, sizeof(w.path), "%s", low + 5); if (!w.path[0]) return false; }
    else
    {
        unsigned v = 0, p = 0; int nth = 1; char extra = 0;
        int got = sscanf(low, "%4x:%4x#%d%c", &v, &p, &nth, &extra);
        if (got < 2 || got == 4) return false;
        w.kind = SEAT_DEVICE; w.vid = (uint16_t)v; w.pid = (uint16_t)p; w.nth = got >= 3 ? nth : 1;
        if (w.nth < 1) return false;
    }
    *out = w;
    return true;
}
void SetWant(int seat, const Want& w);
bool TieToDevice(int seat, int deviceId);   // the seat keeps exactly this controller (by its path) from now on
void Trade(int a, int b);                   // hot swap: two seats exchange what they hold
void Update();                              // resolve every seat against the present controllers; logs changes
bool Get(int seat, Seat* out);
// (finding #32) only what a controller read needs - not the 180-byte description (reads run ~1000-2400 times a second)
struct SeatBrief { Kind kind; bool waiting, autoWaiting; int deviceId; };
bool GetBrief(int seat, SeatBrief* out);

}   // namespace seats
}   // namespace in2
