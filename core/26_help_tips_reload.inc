// ---- v9.7: reload settings, on-screen tips (the mini-game / vehicle stand-down was removed - the author) ----
static void PipForceRespawn();      // defined with the picture-in-picture code
static void HcResetCaches();        // defined with the HUD copy
static const wchar_t* VkName(UINT vk, wchar_t* buf, size_t n)
{
    if (vk >= VK_F1 && vk <= VK_F24) swprintf(buf, n, L"F%u", vk - VK_F1 + 1);
    else if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) swprintf(buf, n, L"%c", (wchar_t)vk);
    else if (vk == VK_OFF || !vk) swprintf(buf, n, L"-");
    else swprintf(buf, n, L"key %u", vk);
    return buf;
}
static const wchar_t* PadName(DWORD mask)
{
    static const struct { DWORD b; const wchar_t* n; } T[] = {
        {0x1000, L"A"}, {0x2000, L"B"}, {0x4000, L"X"}, {0x8000, L"Y"}, {0x0100, L"LB"}, {0x0200, L"RB"}, {PAD_LT, L"LT"}, {PAD_RT, L"RT"},
        {0x0010, L"Start"}, {0x0020, L"Back"}, {0x0040, L"L3"}, {0x0080, L"R3"},
        {0x0001, L"D-pad up"}, {0x0002, L"D-pad down"}, {0x0004, L"D-pad left"}, {0x0008, L"D-pad right"} };
    for (auto& e : T) if (mask & e.b) return e.n;
    return L"-";
}
// ---- v12.9 (the author): controls overlay, shown / hidden with HelpKey (F5) ----
static bool g_HelpOn = false;
static const wchar_t* KeyCombo(UINT vk, bool c, bool sh, bool al, wchar_t* out, size_t n)
{
    wchar_t k[16]; VkName(vk, k, 16);
    if (!vk || vk == VK_OFF) { swprintf(out, n, L"-"); return out; }
    swprintf(out, n, L"%ls%ls%ls%ls", c ? L"Ctrl+" : L"", sh ? L"Shift+" : L"", al ? L"Alt+" : L"", k);
    return out;
}
static void DrawHelp(void* canvas)
{
    if (!g_HelpOn || g_InCutscene || g_GamePaused || !g_CF.text || !g_MarkerFont) return;
    float clipX = *(float*)((unsigned char*)canvas + OFF_CANVAS_CLIPX);
    float clipY = *(float*)((unsigned char*)canvas + OFF_CANVAS_CLIPY);
    if (clipX < 200.f || clipY < 100.f) return;
    struct Row { wchar_t key[40]; wchar_t what[80]; int kind; int order; };   // kind 0 = row, 1 = heading
    static Row rows[32]; int n = 0;
    auto H = [&](const wchar_t* t) { if (n < 32) { rows[n].key[0] = 0; wcsncpy(rows[n].what, t, 79); rows[n].what[79] = 0; rows[n].kind = 1; rows[n].order = 0; ++n; } };
    auto R = [&](const wchar_t* k, const wchar_t* t, int ord) { if (n < 32) { wcsncpy(rows[n].key, k, 39); rows[n].key[39] = 0; wcsncpy(rows[n].what, t, 79); rows[n].what[79] = 0; rows[n].kind = 0; rows[n].order = ord; ++n; } };
    // keyboard rows sorted by key (F1..F12), plain before Ctrl / Shift / Alt
    auto O = [](UINT vk, bool c, bool sh, bool al) { return (int)vk * 8 + (c ? 1 : 0) + (sh ? 2 : 0) + (al ? 4 : 0); };
    wchar_t b[48];
    H(L"Keyboard (player 1)");
    int k0 = n;
    R(KeyCombo(g_P2Cfg.joinVk, g_P2Cfg.joinCtrl, g_P2Cfg.joinShift, g_P2Cfg.joinAlt, b, 48), L"Player 2 joins / leaves", O(g_P2Cfg.joinVk, g_P2Cfg.joinCtrl, g_P2Cfg.joinShift, g_P2Cfg.joinAlt));
    if (g_P3Enabled) R(KeyCombo(g_P3Cfg.joinVk, g_P3Cfg.joinCtrl, g_P3Cfg.joinShift, g_P3Cfg.joinAlt, b, 48), L"Player 3 joins / leaves", O(g_P3Cfg.joinVk, g_P3Cfg.joinCtrl, g_P3Cfg.joinShift, g_P3Cfg.joinAlt));
    R(KeyCombo(g_Cfg.leadVk[0], g_Cfg.leadCtrl[0], g_Cfg.leadShift[0], g_Cfg.leadAlt[0], b, 48), L"Player 1 takes the lead (trades characters)", O(g_Cfg.leadVk[0], g_Cfg.leadCtrl[0], g_Cfg.leadShift[0], g_Cfg.leadAlt[0]));
    R(KeyCombo(g_Cfg.leadVk[1], g_Cfg.leadCtrl[1], g_Cfg.leadShift[1], g_Cfg.leadAlt[1], b, 48), L"Player 2 takes the lead", O(g_Cfg.leadVk[1], g_Cfg.leadCtrl[1], g_Cfg.leadShift[1], g_Cfg.leadAlt[1]));
    if (g_P3Enabled) R(KeyCombo(g_Cfg.leadVk[2], g_Cfg.leadCtrl[2], g_Cfg.leadShift[2], g_Cfg.leadAlt[2], b, 48), L"Player 3 takes the lead", O(g_Cfg.leadVk[2], g_Cfg.leadCtrl[2], g_Cfg.leadShift[2], g_Cfg.leadAlt[2]));
    R(KeyCombo(g_Cfg.pipVk, g_Cfg.pipCtrl, g_Cfg.pipShift, g_Cfg.pipAlt, b, 48), L"Split screen: always / battle only / off", O(g_Cfg.pipVk, g_Cfg.pipCtrl, g_Cfg.pipShift, g_Cfg.pipAlt));
    R(KeyCombo(g_Cfg.layVk, g_Cfg.layCtrl, g_Cfg.layShift, g_Cfg.layAlt, b, 48), L"Split screen layout: right / left / bottom / top", O(g_Cfg.layVk, g_Cfg.layCtrl, g_Cfg.layShift, g_Cfg.layAlt));
    R(KeyCombo(g_Cfg.camVk, g_Cfg.camCtrl, g_Cfg.camShift, g_Cfg.camAlt, b, 48), L"Co-op camera on / off", O(g_Cfg.camVk, g_Cfg.camCtrl, g_Cfg.camShift, g_Cfg.camAlt));
    R(KeyCombo(g_Cfg.reloadVk, false, false, false, b, 48), L"Reload the settings file", O(g_Cfg.reloadVk, false, false, false));
    R(KeyCombo(g_Cfg.helpVk, false, false, false, b, 48), L"Show / hide this list", O(g_Cfg.helpVk, false, false, false));
    for (int i = k0 + 1; i < n; ++i)   // insertion sort by key
        for (int j = i; j > k0 && rows[j].order < rows[j - 1].order; --j) { Row t = rows[j]; rows[j] = rows[j - 1]; rows[j - 1] = t; }
    {
        WithP2 w;
        if (g_Player->cfg->bJoin)
        {
            H(g_P3Enabled ? L"Controller (players 2 and 3)" : L"Controller (player 2)");
            wchar_t t[48]; const wchar_t* bn = g_Player->cfg->bJoin == 0x0010 ? L"Start / Menu" : PadName(g_Player->cfg->bJoin);
            swprintf(t, 48, L"Hold %ls (%.0f s)", bn, JOIN_HOLD_SECONDS); R(t, L"Join / leave", 0);
        }
    }
    float k = clipY / 1080.f;
    float lh = 30.f * k, padX = 28.f * k, keyW = 250.f * k, w = 820.f * k;
    float h = (n + 1) * lh + 2.f * padX;
    float x0 = (clipX - w) * 0.5f, y0 = (clipY - h) * 0.5f;
    { float px0 = 0.f, pw = clipX; if (HudP1Part(clipX, clipY, &px0, &pw)) x0 = px0 + (pw - w) * 0.5f; }   // f1.310: on player 1's monitor while spanning
    static const float bg[4] = {0.f, 0.f, 0.f, 0.78f};
    static const float edge[4] = {0.45f, 0.75f, 1.f, 0.9f};
    static const float white[4] = {1.f, 1.f, 1.f, 1.f}, keyCol[4] = {1.f, 0.85f, 0.35f, 1.f}, head[4] = {0.45f, 0.75f, 1.f, 1.f};
    CanvasBox(canvas, x0, y0, w, h, bg);   // was a thick line from x0 to x0 + w - its ends stuck out by h / 2 each side
    CanvasLine(canvas, x0, y0, x0 + w, y0, 2.f * k, edge); CanvasLine(canvas, x0 + w, y0, x0 + w, y0 + h, 2.f * k, edge);
    CanvasLine(canvas, x0 + w, y0 + h, x0, y0 + h, 2.f * k, edge); CanvasLine(canvas, x0, y0 + h, x0, y0, 2.f * k, edge);
    wchar_t title[80], hk[24]; swprintf(title, 80, L"CO-OP CONTROLS   (%ls to close)", KeyCombo(g_Cfg.helpVk, false, false, false, hk, 24));
    float y = y0 + padX + lh * 0.5f;
    CanvasText(canvas, title, x0 + w * 0.5f, y, white); y += lh;
    for (int i = 0; i < n; ++i, y += lh)
    {
        if (rows[i].kind == 1) { CanvasTextL(canvas, rows[i].what, x0 + padX, y, head); continue; }
        CanvasTextL(canvas, rows[i].key, x0 + padX + 16.f * k, y, keyCol);
        CanvasTextL(canvas, rows[i].what, x0 + padX + keyW, y, white);
    }
}
static void PollHelpKey()
{
    static bool wasDown = false;
    bool down = HotkeyDown(g_Cfg.helpVk, false, false, false);   // no modifiers
    if (down && !wasDown)
    {
        if (HotkeysAllowed() || g_HelpOn) { g_HelpOn = !g_HelpOn; CoopLog("HELP: controls list %s", g_HelpOn ? "shown" : "hidden"); }
        else NoteHotkeyIgnored("help key");
    }
    wasDown = down;
}
static void In2PadMapsReload();   // (input/40b): [Pad.VID:PID] button maps are read again
static void ReloadSettings()
{
    if (g_P3Loaded) return;
    In2PadMapsReload();
    // what this session decided (controllers in seats after hot swaps, who is in) stays as it is
    CoopConfig keep = g_Cfg; PlayerCfg keep2 = g_P2Cfg, keep3 = g_P3Cfg;
    int p2ps = g_Player->partySlot; bool coop = g_Player->joined; bool p3en = g_P3Enabled;   // (the seats keep their controllers in the seats module)
    LoadConfig();
    bool fileP3 = g_P3Enabled;   // LoadConfig read [Player3] only if the file has player 3 on
    g_Player->partySlot = p2ps; g_Player->joined = coop; g_P3Enabled = p3en;
    {   // start-only - the focus contexts / item hooks were set up from the values at start; a changed value applied
        // half-way gave a mixed mode (claude/FF7R_Two_Menus_StepC_RE.md)
    }
    g_Cfg.p1Controller = keep.p1Controller;   // ([Input2] SeatN changes count from the next game start)
    if (p3en && fileP3)
    {   // all of player 3's [Player3] settings apply live, like player 2's (before: only some of them); party slot and
        // join-at-start stay as at start (restart-only, like player 2's).
        // all of player 3's settings (settings/03 PlayerCfg) from the file, then the two start-only values put back
        g_P3Cfg.p2Party = keep3.p2Party; g_P3Cfg.joinAtStart = keep3.joinAtStart;
    }
    else
    {
        g_P3Cfg = keep3;
        if (p3en) CoopLog("SETTINGS: NOTE - the file has player 3 off ([Input2] Seat3) - player 3 stays on with their old settings until the game restarts");
    }
    bool reshape = keep.pipSide != g_Cfg.pipSide || keep.pipFormat != g_Cfg.pipFormat || keep.pipSource != g_Cfg.pipSource ||
                   keep.pipDetail != g_Cfg.pipDetail || keep.pipViewDist != g_Cfg.pipViewDist ||   // set when the camera is made
                   keep.pipFullPct != g_Cfg.pipFullPct || keep2.pipFov != g_P2Cfg.pipFov || keep3.pipFov != g_P3Cfg.pipFov ||
                   keep.pipTallFov != g_Cfg.pipTallFov || keep.pipThirdsOn != g_Cfg.pipThirdsOn || keep.pipVignette != g_Cfg.pipVignette || keep.pipBright != g_Cfg.pipBright;   // f1.81   // player 3's own FOV (size / corner / resolution / FullSizeHud gone)
    if (reshape) { PipForceRespawn(); if (g_P3Enabled) { WithP3 w; PipForceRespawn(); } }
    HcResetCaches();
    IconRefreshSoon();   // ButtonIcons apply at once (only when a player is not on Default)
    CoopLog("SETTINGS: reloaded while playing%s (controller seats, player 3 on/off, and join-at-start keep their values until a restart)", reshape ? " - windows re-made" : "");
    // (the author, f1.157 test: "It just said co-op settings reloaded, did not get the enemy apply message" - the set
    // he changed wasn't the one in use, so nothing had to wait): any party-size set changed in a battle -> the same notice
    bool setsChanged = false;
    for (int i = 0; i < 4; ++i)
    {
        const BalanceSet &a = keep.balSet[i], &b = g_Cfg.balSet[i];
        if (a.hp != b.hp || a.damage != b.damage || a.attackRate != b.attackRate || a.stagger != b.stagger || a.focusP2 != b.focusP2 ||
            a.stayOnHit != b.stayOnHit || a.turnHeavy != b.turnHeavy || a.weakChance != b.weakChance || a.weakHealth != b.weakHealth ||
            a.humansOnly != b.humansOnly) setsChanged = true;
    }
    if (keep.dynamicDifficulty != g_Cfg.dynamicDifficulty) setsChanged = true;   // f1.273 (code review #52): Dynamic <-> Global waits too
    bool nowNote = g_BalBattle && g_Cfg.enemyNow && (setsChanged || keep.humanAggroFile != g_Cfg.humanAggroFile);   // f1.274 (testing)
    bool waitNote = !nowNote && (g_BalReloadWaiting || (g_BalBattle && (setsChanged || keep.humanAggroFile != g_Cfg.humanAggroFile)));
    if (waitNote && !g_BalReloadWaiting) CoopLog("SETTINGS: enemy settings for another party size changed during a battle - they can only be used after it");
    HudNotice(nowNote ? L"Co-op settings reloaded - enemy settings applied now (testing)" : waitNote ? L"Co-op settings reloaded - enemy settings apply after this battle" : L"Co-op settings reloaded");
}
static void PollReloadKey()
{
    static bool wasDown = false;
    bool down = HotkeyDown(g_Cfg.reloadVk, false, false, false);   // no modifiers (like HelpKey)
    if (down && !wasDown)
    {
        if (!g_InCutscene) ReloadSettings();   // also while paused (alt-tabbed to the settings app)
        else NoteHotkeyIgnored("reload key");
    }
    wasDown = down;
}
// (the author, 2026-10-19): the v9.7 mini-game / vehicle stand-down (SpecialTick, [General] PauseInMiniGames) was
// removed. Its widget check fired on things that aren't mini-games (Mako 5 card-key / battle gimmick HUDs, the Sector 7
// CountBoard) and once on a KO hand-off, and then froze players 2 / 3 until "back to normal" (sometimes never). Mini-games
// read only the leader's controller anyway (XInput slot 0). If a section misbehaves, players 2/3 leave with F10 / Ctrl+F10.
