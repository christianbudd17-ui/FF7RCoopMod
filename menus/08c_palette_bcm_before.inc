// =================================================================================
// L2 / R2 TEAMMATE PICK, THE SHORTCUT PALETTE, BcmBefore (restructure 3b, from menus/08): each player's pad read for the
// battle command update, palette ownership (hold LB) and turning the palette to the player's own character
// =================================================================================
// an AI-played teammate for L2 (which=1) / R2 (which=2): party members that aren't player 1's,
// this player's own or the other human's character. R2 takes the second one if there are two.
static int PickAllyType(int which)
{
    unsigned char* mgr = PartyMgr(); if (!mgr) return -1;
    int lead = LeaderType(mgr), list[3], k = 0;
    ForEachPartyMember(mgr, [&](PartyMember& m) {   // rule: not player 1's / this player's / the other human's character
        int t = m.type;
        if (t < 0 || t == lead || t == g_Player->charType || (g_OtherType >= 0 && t == g_OtherType)) return;
        void* c = m.Char();
        if (c && NotKnockedOut(c)) list[k++] = t;          // never a knocked-out teammate (the menu can't turn to their page)
    });
    if (!k) return -1;
    return (which == 2 && k > 1) ? list[1] : list[0];
}

// the shortcut palette opens on player 1's character (like the command menu) and has
// previous/next character. Press NextMember for the owner until it shows their character; slot
// presses wait until then. Which field holds the palette's page isn't proven: bcm+0x30 (the field
// ShortcutMenu resets), else +0x2C, else +0x28 - all three are logged.
static void StepShortcutPage(unsigned char* bcm)
{
    if (g_Player->shortcutPageOk || !bcm) return;
    int f28 = *(int*)(bcm + BCM_BASE_PAGE), f2c = *(int*)(bcm + BCM_SHOWN_PAGE), f30 = *(int*)(bcm + BCM_PALETTE_PAGE);
    int page = f30 >= 0 ? f30 : (f2c >= 0 ? f2c : f28);
    int want = P2Page();   // the character this player controls right now (follows D-pad switches)
    ULONGLONG now = GetTickCount64();
    if (!g_Player->shortcutT0) g_Player->shortcutT0 = now;
    if (g_Player->shortcutWait > 0) { g_Player->shortcutWait--; return; }
    if (page == want)
    {
        g_Player->shortcutPageOk = true;
        CoopLog("P%d SHORTCUT: palette shows page %d (player's character) after %d turns [28=%d 2c=%d 30=%d]", g_Player->num, page, g_Player->shortcutTries, f28, f2c, f30);
        return;
    }
    // the old loop gave up after (party size - 1) turns even when a turn hadn't taken (the palette
    // was still opening), and the palette stayed on the wrong character. Now: retry until 2 s have passed.
    if (now - g_Player->shortcutT0 > 2000 || want < 0)
    {
        g_Player->shortcutPageOk = true;   // give up turning; let the slots work on whatever is showing
        CoopLog("P%d SHORTCUT: gave up turning after %d turns / %llu ms (page field reads %d not %d) [28=%d 2c=%d 30=%d] - slots go to what's showing",
            g_Player->num, g_Player->shortcutTries, (unsigned long long)(now - g_Player->shortcutT0), page, want, f28, f2c, f30);
        return;
    }
    int n = PartyCount(); if (n < 2) n = 2;
    int d = ((want - page) % n + n) % n;
    g_Player->shortcutDir = (n > 2 && d == n - 1) ? -1 : 1;   // the shorter way round
    if (page == g_Player->shortcutLastPage && g_Player->shortcutTries > 0)
        CoopLog("P%d SHORTCUT: last turn didn't take (still page %d) - trying again", g_Player->num, page);
    g_Player->shortcutLastPage = page;
    g_Player->shortcutTries++;
    g_Player->shortcutTurn = true;
    g_Player->shortcutWait = 8;
    CoopLog("P%d SHORTCUT: palette on page %d, want %d - %s character [28=%d 2c=%d 30=%d]", g_Player->num, page, want, g_Player->shortcutDir < 0 ? "previous" : "next", f28, f2c, f30);
}

// player 2's (and 3's, swapped) own copy of the command request struct
// (req / reqValid / act / actValid and REQ_LEN / ACT_OFF / ACT_LEN: in each player's record, core/02 PlayerSlot)
// the per-player parts of the battle command update, run for player 2 and then for player 3.
struct BcmPlayer { void* p = nullptr; bool coop = false; DWORD pressed = 0; };
// the shortcut palette's state in the command manager (LOCKDIAG diffs: +0x28..+0x33 page,
// +0x48 bit 0, +0xB8 = 1 UI mode (4 = command menu), +0x168)
struct PalSave { unsigned char pg[12]; uint32_t f48; int fB8; int f168; };
static void PalGet(unsigned char* b, PalSave& p) { memcpy(p.pg, b + BCM_BASE_PAGE, 12); p.f48 = *(uint32_t*)(b + BCM_PALETTE_FLAGS); p.fB8 = *(int*)(b + BCM_MODE); p.f168 = *(int*)(b + BCM_PALETTE_STATE); }
static void PalPut(unsigned char* b, const PalSave& p) { memcpy(b + BCM_BASE_PAGE, p.pg, 12); *(uint32_t*)(b + BCM_PALETTE_FLAGS) = p.f48; *(int*)(b + BCM_MODE) = p.fB8; *(int*)(b + BCM_PALETTE_STATE) = p.f168; }
static void PalClose(unsigned char* b) { *(int*)(b + BCM_SHOWN_PAGE) = -1; *(int*)(b + BCM_PALETTE_PAGE) = -1; *(uint32_t*)(b + BCM_PALETTE_FLAGS) &= ~1u; *(int*)(b + BCM_MODE) = 0; *(int*)(b + BCM_PALETTE_STATE) = -1; }
static void BcmBefore(unsigned char* bcm, BcmPlayer& bp)
{
    bool coop = g_Player->joined && GetPartyMember;
    void* p2 = coop ? P2Char() : nullptr;
    void* leader = coop ? LeaderChar() : nullptr;   // (L2): was GetPartyMember(0) = first in the order - player 2 on Cloud after player 1 switched lost their passes
    coop = coop && p2 && p2 != leader;
    g_Player->cmdButtons = 0;
    if (coop && Real_XInputGetState)
    {
        XINPUT_STATE st{};
        if (PadGetMine(&st) == ERROR_SUCCESS) g_Player->cmdButtons = PadBits(st.Gamepad);
    }
    DWORD pressed = g_Player->cmdButtons & ~g_Player->cmdButtonsPrev;
    // (their menu button / L2 / R2 open their OWN menu - their own command system asks for those keys, Bcm2MenuKey)

    // shortcut palette ownership (hold ShortcutHold)
    {
        // a player's palette exists only in their own command system (the shared-palette path is gone)
        bool holding = coop && !g_GamePaused && !g_InCutscene && (g_Player->cmdButtons & g_Player->cfg->bShortcutHold) && g_Player->ownBcmOn && g_Player->ownBcm
            && *(int*)(g_Player->ownBcm + BCM_UI_STATE) == BCM_UI_BATTLE;
        if (g_Player->shortcutOwner == g_Player->num)
        {
            if (!holding || GetTickCount64() - g_Player->shortcutSince > 15000)
            {
                g_Player->shortcutOwner = 0; g_Player->shortcutPressFName = 0;
                CoopLog("P%d SHORTCUT: palette released", g_Player->num);
            }
            else
            {
                for (int i = 0; i < 4; ++i)
                    if (pressed & g_Player->cfg->bShortcut[i]) { g_Player->shortcutPressFName = KeyFName(RVA_KEY_SLOT[i]); CoopLog("P%d SHORTCUT: slot %d pressed", g_Player->num, i + 1); break; }
            }
        }
        else if (g_Player->shortcutOwner == 0 && holding && (pressed & g_Player->cfg->bShortcutHold))
        {
            g_Player->shortcutOwner = g_Player->num; g_Player->shortcutSince = GetTickCount64(); g_Player->shortcutPressFName = 0;
            g_Player->shortcutPageOk = false; g_Player->shortcutTurn = false; g_Player->shortcutWait = 6; g_Player->shortcutTries = 0;
            g_Player->shortcutT0 = 0; g_Player->shortcutLastPage = -99; g_Player->shortcutDir = 1;
            CoopLog("P%d SHORTCUT: palette opened for player %d's character", g_Player->num, g_Player->num);
        }
    }
    bp.p = p2; bp.coop = coop; bp.pressed = pressed;
}
static void* FindObj(const char* name, const char* clsName);
static void* FieldNext(void* f);
static void* FindFunc(void* obj, const char* name);
static int ParamOff(void* fn, const char* name);
static int ParmsSize(void* fn);
