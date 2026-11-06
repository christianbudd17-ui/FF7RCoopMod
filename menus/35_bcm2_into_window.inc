// player 2's own Command_C (from their own command system) at the bottom left of their window
// (Where each player's own Command_C goes - cmdOffX / cmdOffY, cmdScaleNow ... - is in their record, core/02.)
static int g_B2TrState = 0;   // SetRenderTranslation found (the function itself is shared - menus/32 SetTrFind / SetRenderTr)
// the box around the visible pieces of a widget (MENUROW diagnostic). The same kind of tree walk the old HUD copy used (HcKindOf), drawing nothing.
static void B2Measure(void* w, int depth, float bb[4], float lowY, float bbLow[4], int* n, int* wide)
{
    if (!w || depth > 40 || ++*n > 800) return;
    unsigned char* o = (unsigned char*)w;
    if (!Readable(o, 0x300)) return;
    unsigned char vis = o[0xB7];
    if (vis == 1 || vis == 2) return;   // Collapsed / Hidden
    bool skip = false; int kind = HcKindOf(w, &skip);
    auto kids = [&](int only)
    {
        HcTArray slots = *(HcTArray*)(o + 0xF8);
        if (!slots.data || slots.num <= 0 || slots.num > 512 || !Readable(slots.data, (size_t)slots.num * 8)) return;
        for (int i = 0; i < slots.num; ++i)
        {
            if (only >= 0 && i != only) continue;
            unsigned char* sl = ((unsigned char**)slots.data)[i];
            if (sl && Readable(sl, 0x40)) B2Measure(*(void**)(sl + 0x30), depth + 1, bb, lowY, bbLow, n, wide);
        }
    };
    switch (kind)
    {
    case HK_USER:
    {
        if (((const float*)(o + 0x100))[3] <= 0.004f) return;   // ColorAndOpacity alpha
        void* tree = *(void**)(o + 0x198);
        if (tree && Readable(tree, 0x40)) B2Measure(*(void**)((unsigned char*)tree + 0x28), depth + 1, bb, lowY, bbLow, n, wide);
        return;
    }
    case HK_PANEL: kids(-1); return;
    case HK_SWITCH: kids(*(int*)(o + 0x110)); return;
    case HK_UNKNOWN: case HK_SKIP: return;
    default: break;
    }
    float x, y, rw, rh, sc;
    if (HcRect(w, &x, &y, &rw, &rh, &sc) && rw > 1.f && rh > 1.f)
    {
        if (rw > 1500.f * sc) ++*wide;   // full-screen backdrops don't count
        else
        {
            bb[0] = fminf(bb[0], x); bb[1] = fminf(bb[1], y); bb[2] = fmaxf(bb[2], x + rw); bb[3] = fmaxf(bb[3], y + rh);
            if (y >= lowY) { bbLow[0] = fminf(bbLow[0], x); bbLow[1] = fminf(bbLow[1], y); bbLow[2] = fmaxf(bbLow[2], x + rw); bbLow[3] = fmaxf(bbLow[3], y + rh); }
        }
    }
    if (kind == HK_BORDER || kind == HK_EBORDER) kids(-1);
}
// bottom = true -> no split-screen window: the menu goes along the bottom of the main screen, next to player 1's
//        (always - no longer settings; 440 px at 1080p from one menu to the next, P2 1 step, P3 2 steps;
// screens narrower than 16:9 shrink the spacing in proportion)
static void Bcm2IntoWindow(void* canvas, float clipX, float clipY, bool bottom = false)
{
    if (!g_Player->ownBcmOn || g_Player->num < 2 || g_Player->num > 3) return;   // players 2 and 3 (their state swapped in)
    int pi = g_Player->num & 3;
    void* c = Bcm2Widget(); if (!c) return;
    if (g_Hc.state == 0 && !HcResolve(canvas)) return;
    if (g_Hc.state != 1) return;
    if (g_B2TrState == 0)
    {
        g_B2TrState = SetTrFind(c) ? 1 : -1;
    }
    if (g_B2TrState != 1) return;
    float x, y, rw, rh, sc;
    float wx, wy, ww, wh;
    HcFrame saveF = g_HcF; bool ownF = false;
    if (bottom)
    {   // no window (and no HUD copy this frame) - HcRect needs a clean frame state
        g_Player->cmdBottomMs = GetTickCount64();
        float k = clipY / 1080.f;
        float narrow = (clipX / clipY) / (16.f / 9.f); if (narrow > 1.f) narrow = 1.f;   // narrower than 16:9 (16:10, 4:3) -> closer together
        wx = 440.f * narrow * (float)(g_Player->num - 1) * k; wy = 0.f; ww = clipX; wh = clipY;
        memset(&g_HcF, 0, sizeof(g_HcF)); g_HcF.canvas = canvas; g_HcF.win = {0.f, 0.f, clipX, clipY}; ownF = true;
    }
    else PipRect(clipX, clipY, &wx, &wy, &ww, &wh);
    struct RestoreF { HcFrame& s; bool on; ~RestoreF() { if (on) { float ox = g_HcF.offX, oy = g_HcF.offY; bool ok = g_HcF.offOk; g_HcF = s; if (ok && !g_HcF.offOk) { g_HcF.offX = ox; g_HcF.offY = oy; g_HcF.offOk = true; } } } } restoreF{saveF, ownF};
    float wantX = wx, wantY = bottom ? 0.f : wy + wh - clipY;
    if (!bottom && g_HudSpanOk && g_GameHwnd)
    {   // f1.311: the HUD box is player 1's part (31b) - the menu's own place is that box's bottom left, not the screen's
        RECT cr{}; GetClientRect(g_GameHwnd, &cr);
        float kx = cr.right > 0 ? clipX / (float)cr.right : 1.f, ky = cr.bottom > 0 ? clipY / (float)cr.bottom : 1.f;   // window px -> canvas px
        wantX = wx - g_HudSpanX * kx; wantY = wy + wh - (g_HudSpanY + g_HudSpanH) * ky;
    }
    if (!bottom && SplitThirds()) wantX -= g_Cfg.thirdsCmdNudge * clipY / 1080.f;   // ThirdsCommandNudge (a little to the left)
    ++g_Player->cmdFrame;
    bool shown = ((unsigned char*)c)[0xB7] != 1 && ((unsigned char*)c)[0xB7] != 2;
    // translation = offset from where the game puts it (bottom left of the screen) to the bottom left of player 2's window
    float s = clipY / 1080.f;
    if (shown && HcRect(c, &x, &y, &rw, &rh, &sc) && sc > 0.f) s = sc / g_Player->cmdScaleNow;   // without our own scale
    {   // their command HUD / menus / shortcuts size for their window (menus/32 CmdScaleWanted); no window = 100 %
        float want = bottom ? 1.f : CmdScaleWanted(clipX * clipY > 1.f ? ww * wh / (clipX * clipY) : 1.f);
        if ((want < 0.999f || g_Player->cmdScaleNow < 0.999f) && WidgetScale(c, want, 0.f, 1.f) && fabsf(want - g_Player->cmdScaleNow) > 0.001f)
        {
            static int logs = 0; if (logs < 30) { ++logs; CoopLog("CMD2: player %d's command HUD / menus at %.0f%% size (%s - CommandMenuScale)", g_Player->num, want * 100.f, bottom ? "no window" : "their window"); }
            g_Player->cmdScaleNow = want;
        }
    }
    if (!g_Player->cmdOffSet || fabsf(wantX - g_Player->cmdWantX) > 1.f || fabsf(wantY - g_Player->cmdWantY) > 1.f || fabsf(s - g_Player->cmdLastScale) > 0.001f)
    {
        g_Player->cmdOffX = wantX / s; g_Player->cmdOffY = wantY / s; g_Player->cmdOffSet = true; g_Player->cmdSetAt = g_Player->cmdFrame; g_Player->cmdWantX = wantX; g_Player->cmdWantY = wantY; g_Player->cmdLastScale = s;
        static int logs = 0; if (logs < 12) { ++logs; CoopLog("CMD2: player %d's Command_C goes %s (offset %.0f, %.0f px, scale %.3f)", g_Player->num, bottom ? "along the bottom of the main screen (no split-screen window)" : "to their window", wantX, wantY, s); }
    }
    if (shown) { static int logs2 = 0; if (logs2 < 6 && g_Player->cmdFrame - g_Player->cmdSetAt == 5 && HcRect(c, &x, &y, &rw, &rh, &sc)) { ++logs2; CoopLog("CMD2: player %d's Command_C now at %.0f, %.0f size %.0f x %.0f (window %.0f, %.0f, %.0f x %.0f)", g_Player->num, x, y, rw, rh, wx, wy, ww, wh); } }
    // only while player 2's palette / menu is open (mode B8 != 0); otherwise parked below the screen
    int b8 = *(int*)(g_Player->ownBcm + BCM_MODE);
    if (b8 == 0)
    {   // (the author, screenshots: "The characters have a HUD that sits there until they open their menu. Player one
        // always has that, but P2 and P3 do not"): their closed command HUD (ability / Attack / Commands Menu) stays in
        // their window during a battle, like player 1's - not outside a battle (parked below the screen then). Always on
        // (the author 2026-10-26: "should not be a setting, just default on").
        bool keep = *(int*)(g_Player->ownBcm + BCM_UI_STATE) >= BCM_UI_BATTLE;
        SetRenderTr(c, g_Player->cmdOffX, keep ? g_Player->cmdOffY : g_Player->cmdOffY + 4000.f);
        static int logs[4] = {0, 0, 0, 0};
        if (keep && logs[pi] < 2) { ++logs[pi]; unsigned char v = ((unsigned char*)c)[0xB7]; float a = ((const float*)((unsigned char*)c + 0x100))[3];
            CoopLog("CMD2: player %d's closed command HUD kept in %s (visibility %d, opacity %.2f)", g_Player->num, bottom ? "the bottom row" : "their window", v, a); }
        return;
    }
    SetRenderTr(c, g_Player->cmdOffX, g_Player->cmdOffY);
    if (bottom && shown)
    {   // measure the open menu now and then for the log (MENUROW - diagnostic only; no P2 / P3 tag,
        //        The author: "it's obvious based on position. P1 left, P2 middle, P3 right")
        static int mlogs = 0; static float lastW[4];
        if (mlogs < 24 && g_Player->cmdFrame % 30 == 0)
        {
            float bb[4] = {1e9f, 1e9f, -1e9f, -1e9f}, lo[4] = {1e9f, 1e9f, -1e9f, -1e9f}; int n = 0, wide = 0;
            B2Measure(c, 0, bb, clipY * 0.45f, lo, &n, &wide);
            float wLo = lo[2] - lo[0];
            if (lo[2] > lo[0] && fabsf(wLo - lastW[pi]) > 8.f)
            {
                ++mlogs; lastW[pi] = wLo; float k = clipY / 1080.f;
                CoopLog("MENUROW: player %d's open menu (B8 %d): lower part x %.0f-%.0f y %.0f-%.0f (%.0f px wide = %.0f at 1080p); everything x %.0f-%.0f y %.0f-%.0f; %d widget(s), %d full-screen; spacing %.0f px",
                    g_Player->num, b8, lo[0], lo[2], lo[1], lo[3], wLo, wLo / k, bb[0], bb[2], bb[1], bb[3], n, wide, wantX);
            }
        }
    }
}
// the own menu can open without a window when it is placed along the bottom (placement ran in the last half second)
static bool Bcm2BottomReady() { return g_Player->num >= 2 && g_Player->num <= 3 && GetTickCount64() - g_Player->cmdBottomMs < 500; }

