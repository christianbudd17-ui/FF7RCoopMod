// (the author: "the text when an enemy is killed (tells you about their elimination, Gil and XP gained, etc). Can we add
// an option to move that the same way we move the menus? We need to move it to the right some by default." - "we should be
// able to move them in every configuration").
// (the author: "They need to be all the way to the right side of the monitor." - when: "Always"; "They are basically like
// the mini map they should not really move once we have the position we want"): the messages' right edge goes to the right edge
// of the screen (a 20 px margin at 1080p), split screen or not, every layout; then [PictureInPicture] MessageLogX / MessageLogY
// px (at 1080p; + = right / down, default 0 / 0).
// The game's message log is a UEndMessageLogs widget (exe: EndMessageLogs.cpp / SEndMessageLogs.cpp, the HUD's
// "Pnl_MessageLogs"); its lines are MessageLogs_Cell_C user widgets. f1.162 moved the log widget itself
// (SetRenderTranslation) - but in the f1.162 test (20:05 - 20:08 screenshots) nothing moved: the class was found, it is a UMG
// widget, yet none of the found log widgets ever had a size on screen (no "MSGLOG: the game's message log ... at" line).
// (1) every log widget found is described once (name, outer, flags, visibility, size) - MSGLOG lines; (2) the lines
// themselves are moved instead: every shown MessageLogs_Cell_C (a root widget - pip_hud/31's shown list) gets the same shift,
// re-applied every frame (their own slide-in may set the same translation). Where the game puts the column is measured once
// (the first line, after it has been up for 20 frames and before anything was moved); the column's width is the widest line
// seen so far, so the column's right edge stays at the screen's edge and it only ever moves left when a wider line shows up.
struct MlMoved { int idx = -1, ser = 0; float px = 0.f, py = 0.f, sc = 1.f, ox = 0.f, oy = 0.f, ow = 0.f, oh = 0.f;
                 bool known = false, logged = false, described = false; int tries = 0; unsigned frameSet = 0; ULONGLONG t = 0; };
static MlMoved g_MlMv[32];
static unsigned g_MlFrame = 0;
// the lines (cells)
struct MlCell { int idx = -1, ser = 0; unsigned seen = 0; bool logged = false; };
static MlCell g_MlCells[16];
static bool g_MlColKnown = false; static float g_MlColX = 0.f, g_MlColY = 0.f, g_MlColW = 0.f, g_MlColSc = 1.f;
static void MlDescribe(void* w, const char* what, float x, float y, float rw, float rh, bool ok)
{
    static int logs = 0; if (logs >= 40) return; ++logs;
    char nm[96] = "?", cn[96] = "?", o1[96] = "-", o2[96] = "-", sl[96] = "-", sp[96] = "-";
    NameOf(w, nm, sizeof(nm)); ObjClassName(w, cn, sizeof(cn));
    void* out = *(void**)((unsigned char*)w + 0x20);
    if (out) { NameOf(out, o1, sizeof(o1)); void* out2 = *(void**)((unsigned char*)out + 0x20); if (out2) NameOf(out2, o2, sizeof(o2)); }
    void* slot = Readable((unsigned char*)w + 0x28, 8) ? *(void**)((unsigned char*)w + 0x28) : nullptr;   // UWidget Slot
    if (slot && Readable(slot, 0x38))
    {
        ObjClassName(slot, sl, sizeof(sl));
        void* par = *(void**)((unsigned char*)slot + 0x28);   // UPanelSlot Parent
        if (par && Readable(par, 0x30)) { char pn[64] = "?", pc[64] = "?"; NameOf(par, pn, sizeof(pn)); ObjClassName(par, pc, sizeof(pc)); snprintf(sp, sizeof(sp), "%s (%s)", pn, pc); }
    }
    CoopLog("MSGLOG: %s %s (%s) - outer %s / %s, flags 0x%X, visibility %d, slot %s in %s, on screen %s %.0f, %.0f (%.0f x %.0f)",
        what, nm, cn, o1, o2, (unsigned)*(DWORD*)((unsigned char*)w + 8), (int)((unsigned char*)w)[0xB7], sl, sp, ok ? "at" : "NO SIZE -", x, y, rw, rh);
}
static void MessageLogCells(float clipX, float clipY)
{
    float k = clipY / 1080.f;
    // this frame's shown lines
    for (int i = 0; i < g_RwN; ++i)
    {
        void* w = ResolveWeak(g_RwIdx[i], g_RwSer[i]);
        if (!w || !ClassIs(*(void**)((unsigned char*)w + 0x10), "MessageLogs_Cell_C")) continue;
        MlCell* c = nullptr;
        for (auto& e : g_MlCells) if (e.idx == g_RwIdx[i] && e.ser == g_RwSer[i]) { c = &e; break; }
        if (!c) for (auto& e : g_MlCells) if (e.idx < 0 || !ResolveWeak(e.idx, e.ser)) { e = MlCell(); e.idx = g_RwIdx[i]; e.ser = g_RwSer[i]; c = &e; break; }
        if (!c) continue;
        ++c->seen;
        float x, y, rw, rh, sc;
        bool ok = HcRect(w, &x, &y, &rw, &rh, &sc) && sc > 0.f;
        if (!c->logged) { c->logged = true; MlDescribe(w, "line", x, y, rw, rh, ok); }
        if (!ok) continue;
        if (!g_MlColKnown)
        {   // the column's place: the first line, 20 frames after it showed (its slide-in done), nothing moved yet
            if (c->seen < 20) continue;
            g_MlColKnown = true; g_MlColX = x; g_MlColY = y; g_MlColW = rw; g_MlColSc = sc;
            CoopLog("MSGLOG: the battle messages' column is at %.0f, %.0f (first line %.0f x %.0f, scale %.2f) - moving the lines from now on", x, y, rw, rh, sc);
        }
        else if (rw > g_MlColW + 1.f)
        {   // a wider line than any so far: the column's right edge stays at the screen's edge, so the column moves left
            static int wl = 0; if (wl < 10) { ++wl; CoopLog("MSGLOG: a wider message line (%.0f px, was %.0f) - the column moves %.0f px left", rw, g_MlColW, rw - g_MlColW); }
            g_MlColW = rw;
        }
        float wx = (clipX - 20.f * k - g_MlColW) - g_MlColX + g_Cfg.msgLogX * k;
        float wy = g_Cfg.msgLogY * k;
        SetRenderTr(w, wx / sc, wy / sc);   // every frame: the line's own animation may set its translation too
        static float lastX = -99999.f, lastY = -99999.f;
        if (fabsf(wx - lastX) > 0.5f || fabsf(wy - lastY) > 0.5f)
        {
            lastX = wx; lastY = wy;
            static int ml = 0; if (ml < 20) { ++ml; CoopLog("MSGLOG: battle message lines moved %.0f, %.0f px (right edge of the screen + MessageLogX / MessageLogY)", wx, wy); }
        }
    }
}
static void MessageLogPlace(void* canvas)
{
    ++g_MlFrame;
    float clipX = *(float*)((unsigned char*)canvas + OFF_CANVAS_CLIPX), clipY = *(float*)((unsigned char*)canvas + OFF_CANVAS_CLIPY);
    if (clipX < 200.f || clipY < 100.f) return;
    if (g_MlN <= 0 && g_RwN <= 0) return;
    if (!CmSetup(canvas)) return;   // SetRenderTranslation (menus/32)
    MessageLogCells(clipX, clipY);   // the lines themselves
    // (f1.162's move of the log widget itself is not used any more - only described, to learn why it never had a size)
    static int isWidget = 0;   // the UWidget functions (GetCachedGeometry) are only called on a UWidget
    if (isWidget == 0 && g_MlCls)
    {
        isWidget = -1; int n = 0;
        for (void* c = g_MlCls; c && n < 64; c = *(void**)((unsigned char*)c + 0x30), ++n) if (NameIs(c, "Widget")) { isWidget = 1; break; }
        CoopLog("MSGLOG: the message log class %s", isWidget == 1 ? "is a UMG widget" : "is NOT a UMG widget");
    }
    if (isWidget != 1) return;
    for (int i = 0; i < g_MlN; ++i)
    {
        bool have = false;
        for (auto& m : g_MlMv) if (m.idx == g_MlIdx[i] && m.ser == g_MlSer[i]) { have = true; break; }
        if (have) continue;
        for (auto& m : g_MlMv) if (m.idx < 0 || !ResolveWeak(m.idx, m.ser)) { m = MlMoved(); m.idx = g_MlIdx[i]; m.ser = g_MlSer[i]; break; }
    }
    for (auto& m : g_MlMv)
    {
        if (m.idx < 0 || m.described) continue;
        void* w = ResolveWeak(m.idx, m.ser);
        if (!w || !ClassChainHas(*(void**)((unsigned char*)w + 0x10), g_MlCls)) { m.idx = -1; continue; }   // f1.337: still the log class
        float x = 0.f, y = 0.f, rw = 0.f, rh = 0.f, sc = 0.f;
        bool ok = HcRect(w, &x, &y, &rw, &rh, &sc) && sc > 0.f;
        if (ok || ++m.tries >= 600) { m.described = true; MlDescribe(w, "log widget", x, y, rw, rh, ok); }   // (described once it has a size, or after ~10 s without one)
    }
}
