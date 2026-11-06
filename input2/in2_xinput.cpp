// input2/in2_xinput.cpp - the controller module's XInput channel: slots 0-3 (through the real System32 xinput1_3)
// and tying each slot to its controller by matching button presses. Split from in2.cpp (review #19).
#include "in2_internal.h"

namespace in2 {
namespace detail {

namespace {

// ---------------------------------------------------------------------------------------------------------
// XInput slots and their controllers
// ---------------------------------------------------------------------------------------------------------
void Bind(int slot, int idx, const char* why)
{
    for (int s = 0; s < 4; ++s) if (g_xBoundTo[s] == idx) { g_xBoundTo[s] = -1; }
    if (g_xBoundTo[slot] >= 0) g_d[g_xBoundTo[slot]].pub.xinputSlot = -1;
    g_xBoundTo[slot] = idx;
    Dev& d = g_d[idx];
    d.pub.xinputSlot = slot; d.matchSlot = -1; d.matchCount = 0;
    char ds[160]; DescribeTo(d.pub, ds, sizeof(ds));
    LOG("INPUT2: XInput slot %d is %s (%s)", slot, ds, why);
}

}   // namespace


void Unbind(int slot, const char* why)
{
    int idx = g_xBoundTo[slot]; if (idx < 0) return;
    g_xBoundTo[slot] = -1; g_d[idx].pub.xinputSlot = -1;
    char ds[160]; DescribeTo(g_d[idx].pub, ds, sizeof(ds));
    LOG("INPUT2: XInput slot %d no longer tied to %s (%s)", slot, ds, why);
}

void PollXInput(bool scan)
{
    if (!g_h.xGetState) return;
    DWORD now = GetTickCount();
    for (int s = 0; s < 4; ++s)
    {
        if (!g_xConn[s] && !scan) continue;   // asking an empty slot is slow - only while scanning
        XState x; memset(&x, 0, sizeof(x));
        bool ok = g_h.xGetState((DWORD)s, &x) == ERROR_SUCCESS;
        if (ok != g_xConn[s])
        {
            g_xConn[s] = ok;
            LOG("INPUT2: XInput slot %d %s", s, ok ? "connected" : "disconnected");
            if (!ok) { Unbind(s, "slot disconnected"); g_xPrev[s] = 0; }
        }
        if (!ok) continue;
        g_x[s] = x;
        uint16_t down = (uint16_t)(x.g.b & ~g_xPrev[s]); g_xPrev[s] = x.g.b;
        if (down) { g_xEdgeBtn[s] = down; g_xEdgeT[s] = now; }
    }
}

// An XInput slot belongs to the XInput-family controller whose DirectInput presses match it. Shortcut: exactly one
// unmatched XInput-family controller and exactly one unmatched connected slot.
void MatchXInput()
{
    int freeSlots = 0, lastSlot = -1, freeDevs = 0, lastDev = -1;
    for (int s = 0; s < 4; ++s) if (g_xConn[s] && g_xBoundTo[s] < 0) { ++freeSlots; lastSlot = s; }
    for (int i = 0; i < g_n; ++i) if (g_d[i].pub.present && g_d[i].pub.xinputFamily && g_d[i].pub.xinputSlot < 0) { ++freeDevs; lastDev = i; }
    bool single = freeSlots == 1 && freeDevs == 1;   // one candidate pair: one matching press is enough (else two)
    DWORD now = GetTickCount();
    for (int i = 0; i < g_n; ++i)
    {
        Dev& d = g_d[i];
        if (!d.pub.present || !d.pub.xinputFamily || d.pub.xinputSlot >= 0 || !d.edgeT || now - d.edgeT > 200) continue;
        {   // (code review B5): a press that fits more than one slot, or a slot press that another untied pad also
            // fits (two pads pressed together), says nothing about which is which - it doesn't count, for anyone
            auto Fits = [&](const Dev& x, int s) {
                if (!g_xConn[s] || g_xBoundTo[s] >= 0 || !g_xEdgeT[s] || !x.edgeT || now - x.edgeT > 200) return false;
                DWORD dt = x.edgeT > g_xEdgeT[s] ? x.edgeT - g_xEdgeT[s] : g_xEdgeT[s] - x.edgeT;
                return dt <= MATCH_WINDOW_MS && (x.edgeBtn & g_xEdgeBtn[s]) != 0; };
            int nFit = 0, fitS = -1;
            for (int s = 0; s < 4; ++s) if (Fits(d, s)) { ++nFit; fitS = s; }
            bool amb = nFit > 1;
            for (int j = 0; j < g_n && !amb && nFit == 1; ++j)
                if (j != i && g_d[j].pub.present && g_d[j].pub.xinputFamily && g_d[j].pub.xinputSlot < 0 && Fits(g_d[j], fitS)) amb = true;
            if (amb)
            {
                static int ambLogs = 0;
                if (ambLogs++ < 5) LOG("INPUT2: #%d's press can't tell which XInput slot is which (pressed together with another controller) - not counted", d.pub.id);
                for (int s = 0; s < 4; ++s) if (Fits(d, s)) g_xEdgeT[s] = 0;   // that slot press counts for nobody
                d.edgeT = 0;
                continue;
            }
        }
        for (int s = 0; s < 4; ++s)
        {
            if (!g_xConn[s] || g_xBoundTo[s] >= 0 || !g_xEdgeT[s]) continue;
            DWORD dt = d.edgeT > g_xEdgeT[s] ? d.edgeT - g_xEdgeT[s] : g_xEdgeT[s] - d.edgeT;
            if (dt > MATCH_WINDOW_MS || !(d.edgeBtn & g_xEdgeBtn[s])) continue;
            if (d.matchSlot == s) ++d.matchCount; else { d.matchSlot = s; d.matchCount = 1; }
            d.edgeT = 0;   // each press counts once
            if (d.matchCount >= (single && s == lastSlot && i == lastDev ? 1 : 2)) Bind(s, i, single ? "the only unmatched XInput controller, press matches" : "button presses match");
            break;
        }
    }
}

}   // namespace detail
}   // namespace in2
