// =================================================================================
// PLAYERS 2/3's OWN BUTTON ICONS (to-do 7; RE: claude/FF7R_Button_Icons_RE.md)
// =================================================================================
// The game draws button icons as glyphs inside its text blocks (<button=shortcut1> etc.), chosen when a text block is
// REBUILT (0x1511D10) from an "icon style" kept in the 3 shared UEndFont objects (font +0x468 style: 0 PS4, 3 PS5, 1 Xbox;
// +0x469 = keyboard names). 0x13AF530 copies the style from the UI processor (+0x75C / +0x75D / +0x5F = player 1's device)
// into the fonts and then rebuilds every text with button tags (0x150EC30). One style for everyone = player 1's device
// (the author's f1.87 test: with player 1 on the keyboard, players 2/3's palettes showed 1 2 3 4).
// Here: when a text block that belongs to player 2 / 3 (inside their Command_C, or built while their focus context is
// live) is rebuilt, the fonts are switched to that player's style just for that rebuild and put back right after:
// the processor bytes are set to the player's device for one call of 0x13AF530 (its global refresh suppressed) and
// restored at once, so player 1's own state never changes.
// [PlayerN] ButtonIcons: Auto (the default; PlayStation icons for a Sony controller - PS5 glyphs for a DualSense - else Xbox),
// PlayStation, Xbox, Default (f1.89, the author: "Default (original game behavior)" - nothing switched, player 1's device's
// icons for everyone). Log lines "ICONS: ..." (capped) show what the game does, also on Default.
typedef void (*FontStyle_t)(void* S);
typedef void (*IconRefresh_t)();
typedef void (*TextRebuild_t)(unsigned char* s);
static FontStyle_t Orig_FontStyle = nullptr;
static IconRefresh_t Orig_IconRefresh = nullptr;
static TextRebuild_t Orig_TextRebuild = nullptr;
static constexpr uintptr_t RVA_UI_SINGLETON = 0x59CA970;   // S (getter 0x13ACA90); S+0x10 = the UI input processor
static bool g_IconApplying = false, g_IconNoRefresh = false;
static int g_IconRefreshIn = 0;   // frames until a refresh of every text with button tags (battle start, F6)
static unsigned char* IconS() { return g_Base ? (unsigned char*)(g_Base + RVA_UI_SINGLETON) : nullptr; }
// the 3 fonts (a TArray of weak references at *(S+0x28)); false = not loaded yet
static bool IconFonts(unsigned char* f[3])
{
    unsigned char* S = IconS(); if (!S) return false;
    unsigned char* list = *(unsigned char**)(S + 0x28);
    if (!list || !Readable(list, 16)) return false;
    int* d = *(int**)list; int n = *(int*)(list + 8);
    if (!d || n < 3 || n > 16 || !Readable(d, 24)) return false;
    for (int i = 0; i < 3; ++i)
    {
        f[i] = (unsigned char*)ResolveWeak(d[i * 2], d[i * 2 + 1]);
        if (!f[i] || !Readable(f[i] + 0x238, 0x240)) return false;
    }
    return true;
}
struct IconFontSave { unsigned char b468, b469; unsigned char arr[5][64]; bool ok[5]; };
static void IconFontsSave(unsigned char* f[3], IconFontSave sv[3])
{
    for (int i = 0; i < 3; ++i)
    {
        sv[i].b468 = f[i][0x468]; sv[i].b469 = f[i][0x469];
        for (int k = 0; k < 5; ++k)
        {
            unsigned char* a = *(unsigned char**)(f[i] + 0x238 + k * 0x10); int num = *(int*)(f[i] + 0x238 + k * 0x10 + 8);
            sv[i].ok[k] = a && num >= 64 && Readable(a, 64);
            if (sv[i].ok[k]) memcpy(sv[i].arr[k], a, 64);
        }
    }
}
static void IconFontsRestore(unsigned char* f[3], const IconFontSave sv[3])
{
    for (int i = 0; i < 3; ++i)
    {
        f[i][0x468] = sv[i].b468; f[i][0x469] = sv[i].b469;
        for (int k = 0; k < 5; ++k)
            if (sv[i].ok[k])
            {
                unsigned char* a = *(unsigned char**)(f[i] + 0x238 + k * 0x10); int num = *(int*)(f[i] + 0x238 + k * 0x10 + 8);
                if (a && num >= 64) memcpy(a, sv[i].arr[k], 64);
            }
    }
}
// player p's ButtonIcons choice (0 Auto, 1 PlayStation, 2 Xbox, 3 Default)
static bool In2SeatVidPid(int s, unsigned* vid, unsigned* pid);   // input/40b
static int IconChoiceOf(int p)
{
    return (p == 3 ? g_P3Cfg : g_P2Cfg).buttonIcons;
}
// the font style for player p's icons: 1 Xbox, 0 PS4, 3 PS5; -1 = Default (the game's own, nothing switched)
static int IconStyleFor(int p)
{
    int choice = IconChoiceOf(p);
    if (choice == 3) return -1;   // Default (original game behavior)
    unsigned vid = 0, pid = 0; In2SeatVidPid(p - 1, &vid, &pid);
    bool sony = vid == 0x054C, dualSense = sony && (pid == 0x0CE6 || pid == 0x0DF2);
    if (choice == 2) return 1;
    if (choice == 1) return dualSense ? 3 : 0;
    return sony ? (dualSense ? 3 : 0) : 1;   // Auto
}
// true when player 2 or 3 is on anything but Default (only then is anything switched or refreshed)
static bool IconAnyOwn() { return IconChoiceOf(2) != 3 || IconChoiceOf(3) != 3; }
// whose text block is this: 1 / 2 / 3 = inside that player's Command_C, else the player whose focus context is live (their
// own command system's update builds its texts there), else 0
static int IconOwner(unsigned char* s, int* viaFocus)
{
    int o = FocusOwner(*(void**)(s + 0x08), *(unsigned char**)(s + 0x10));
    if (viaFocus) *viaFocus = o;
    if (o >= 1) return o;   // inside a Command_C: that player's (player 1's own menu stays player 1's)
    if (g_FCtxOn && (g_FCtxLive == 2 || g_FCtxLive == 3)) return g_FCtxLive;
    return o < 0 ? 0 : o;
}
static void Hook_FontStyle(void* S)   // 0x13AF530 (player 1's device changed / the game's own calls)
{
    if (g_IconApplying) { Orig_FontStyle(S); return; }
    unsigned char* proc = S ? *(unsigned char**)((unsigned char*)S + 0x10) : nullptr;
    int b75C = -1, b75D = -1, b5F = -1;
    if (proc && Readable(proc + 0x5F, 1) && Readable(proc + 0x75C, 2)) { b75C = proc[0x75C]; b75D = proc[0x75D]; b5F = proc[0x5F]; }
    Orig_FontStyle(S);
    static int logs = 0;
    if (logs < 50)
    {
        ++logs; unsigned char* f[3]; char t[96] = "fonts not loaded";
        if (IconFonts(f)) snprintf(t, sizeof(t), "fonts: style %d %d %d, keyboard names %d %d %d", f[0][0x468], f[1][0x468], f[2][0x468], f[0][0x469], f[1][0x469], f[2][0x469]);
        CoopLog("ICONS: the game set its icon style (player 1's device: 75C %d 75D %d 5F %d) - %s", b75C, b75D, b5F, t);
    }
    g_IconRefreshIn = 0;   // the game just rebuilt every text with button tags itself (players 2/3's in their style - Hook_TextRebuild)
}
static void Hook_IconRefresh()   // 0x150EC30: rebuild every text with button tags
{
    if (g_IconNoRefresh) return;   // our own style switch - only the one text block is rebuilt
    static int logs = 0; if (logs < 50) { ++logs; CoopLog("ICONS: every text with button icons is rebuilt"); }
    Orig_IconRefresh();
}
static void Hook_TextRebuild(unsigned char* s)   // 0x1511D10 SEndTextBlock::RebuildGlyphs
{
    if (!s || !IconAnyOwn() || g_IconApplying || GetCurrentThreadId() != g_GameThreadId || !Orig_FontStyle || !Readable(s, 0xA60) || s[0x9B4])
    { Orig_TextRebuild(s); return; }   // (+0x9B4 = rebuild deferred: the game only marks it)
    int focus = -9, owner = IconOwner(s, &focus), style = -1;
    bool applied = false;
    unsigned char* f[3];
    if ((owner == 2 || owner == 3) && (style = IconStyleFor(owner)) >= 0 && IconFonts(f))
    {
        unsigned char* S = IconS(); unsigned char* proc = *(unsigned char**)(S + 0x10);
        if ((f[0][0x468] != style || f[0][0x469] != 0) && proc && Readable(proc + 0x5F, 1) && Readable(proc + 0x75C, 2))
        {
            IconFontSave sv[3]; IconFontsSave(f, sv);
            unsigned char o75C = proc[0x75C], o75D = proc[0x75D], o5F = proc[0x5F];
            g_IconApplying = true; g_IconNoRefresh = true;
            proc[0x75C] = style == 1 ? 2 : 0; proc[0x75D] = style == 1 ? 1 : 0; proc[0x5F] = style == 3 ? 1 : 0;   // Xbox / PS4 / PS5 (RE doc table)
            Orig_FontStyle(S);   // the fonts now hold that player's style (its tail call - the global rebuild - is suppressed)
            proc[0x75C] = o75C; proc[0x75D] = o75D; proc[0x5F] = o5F;
            g_IconNoRefresh = false;
            Orig_TextRebuild(s);
            IconFontsRestore(f, sv);
            g_IconApplying = false;
            applied = true;
        }
    }
    if (!applied) Orig_TextRebuild(s);
    static int logs = 0;
    if (s[0x9B7] && logs < 300)
    {
        ++logs;
        CoopLog("ICONS: text with button icons rebuilt - block %p, owner %d (Command_C check %d, live context %d)%s",
            s, owner, focus, (int)g_FCtxLive, applied ? (style == 1 ? " - player's icons: Xbox" : style == 3 ? " - player's icons: PS5" : " - player's icons: PS4")
                                                 : (owner >= 2 ? (style < 0 ? " - that player is on Default" : " - already that player's style") : ""));
    }
}
// main tick: after a battle starts (players 2/3's command systems come on), F6, or a change of player 2/3's icon style (hot
// swap, another controller, ButtonIcons), rebuild every text with button icons once, so each gets its player's icons
static void IconTick()
{
    static bool was2 = false, was3 = false; static int st2 = -9, st3 = -9, chk = 0;
    bool on2 = g_Player->ownBcmOn, on3 = g_OtherPlayer->ownBcmOn;   // (main tick: player 2's state loaded)
    if (((on2 && !was2) || (on3 && !was3)) && IconAnyOwn()) g_IconRefreshIn = 30;   // nothing to do when both are on Default
    was2 = on2; was3 = on3;
    if (++chk >= 30)
    {   // twice a second: the style each of players 2/3 should have now (a change to or from Default refreshes too - F6)
        chk = 0;
        int n2 = IconStyleFor(2), n3 = IconStyleFor(3);
        if ((st2 != -9 && n2 != st2) || (st3 != -9 && n3 != st3)) { if (g_IconRefreshIn <= 0) g_IconRefreshIn = 2; }
        static int c2 = -9, c3 = -9; int k2 = IconChoiceOf(2), k3 = IconChoiceOf(3);
        if (k2 != c2 || k3 != c3 || n2 != st2 || n3 != st3)
        {   // each player's choice in the log (at the start and whenever it or the controller changes; capped)
            static int logs = 0;
            static const char* const kName[4] = {"Auto", "PlayStation", "Xbox", "Default"};
            auto sName = [](int st) { return st == 1 ? "Xbox" : st == 0 ? "PS4" : st == 3 ? "PS5" : "player 1's (the game's own)"; };
            if (logs < 20) { ++logs; CoopLog("ICONS: button icons - player 2 %s -> %s, player 3 %s -> %s", kName[k2 & 3], sName(n2), kName[k3 & 3], sName(n3)); }
            c2 = k2; c3 = k3;
        }
        st2 = n2; st3 = n3;
    }
    if (g_IconRefreshIn > 0 && --g_IconRefreshIn == 0 && Orig_IconRefresh)
    {
        static int logs = 0; if (logs < 20) { ++logs; CoopLog("ICONS: texts with button icons rebuilt%s (a battle started / settings reloaded / a player's controller changed)", IconAnyOwn() ? " in each player's style" : " - everyone back to player 1's icons (players 2/3 on Default)"); }
        Orig_IconRefresh();
    }
}
static void IconRefreshSoon() { if (IconAnyOwn()) g_IconRefreshIn = 30; }   // F6 (a change back to Default: IconTick's style check)
static void InstallButtonIconHooks()
{
    static const unsigned char exStyle[15] = {0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18};
    static const unsigned char exRefresh[15] = {0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x55,0x41,0x56,0x41,0x57};
    static const unsigned char exRebuild[18] = {0x40,0x55,0x57,0x48,0x8D,0xAC,0x24,0xE8,0xFE,0xFF,0xFF,0x48,0x81,0xEC,0x18,0x02,0x00,0x00};
    void* t = MakeInlineHook(g_Base + 0x150EC30, (void*)Hook_IconRefresh, 15, exRefresh, "icon refresh (own button icons)");
    if (!t) { CoopLog("ICONS: not available - everyone sees player 1's button icons"); return; }
    Orig_IconRefresh = (IconRefresh_t)t;
    // (the text-rebuild hook goes in only when the style hook did: it calls Orig_FontStyle)
    t = MakeInlineHook(g_Base + 0x13AF530, (void*)Hook_FontStyle, 15, exStyle, "icon style (own button icons)");
    if (t) Orig_FontStyle = (FontStyle_t)t;
    if (Orig_FontStyle)
    {
        t = MakeInlineHook(g_Base + 0x1511D10, (void*)Hook_TextRebuild, 18, exRebuild, "text rebuild (own button icons)");
        if (t) Orig_TextRebuild = (TextRebuild_t)t;
    }
    CoopLog("ICONS: players 2/3's own button icons %s", Orig_FontStyle && Orig_TextRebuild ? "ready" : "NOT available (a hook failed)");
}
