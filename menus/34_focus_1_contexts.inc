// =================================================================================
// TWO COMMAND MENUS AT ONCE - step A (the FOCUS logging, below) + f1.20 step B (per-player focus contexts, further
// down - what players 2/3's own menus run on; always on). See claude/FF7R_Two_Menus_RE.md.
// The game's UI input processor (*(base+0x59CA980), vtable 0x4BF24B0) is also its focus manager ("mgr"):
//   +0x08 focus path (TArray<TWeakPtr<SWidget>> +0x08 data / +0x10 num, + weak window) - ONE for everybody
//   +0x40 focus stack (TArray, +0x48 num; entries 0x28 = {widget weak, restore weak, int prio})
//   +0x98 key captures. Every key routes along the one path; SetFocus only moves into the top stack entry.
// The hooks route every push / pop / focus change into the right player's focus context (step B) and log it with the
// OWNER of the widget (whose Command_C it sits in: 1 = player 1's, 2 / 3 = players 2 / 3's own command system's,
// 0 = none, ? = can't tell) and the caller. Lines: FOCUS ...  Caps: 400 calls + 400 path moves. (The FOCUS / CTX
// per-key lines are research logging - the release log plan removes them.)
// =================================================================================
// (restructure 3d: menus/34 split by job - this file = the focus manager, owners and the per-player contexts; the hooks
// that route + log every focus call are in menus/34_focus_2_hooks_log, players 2 / 3's pad keys in menus/34_focus_3_pad_keys)
struct GSp { void* obj; unsigned char* ctrl; };   // TSharedPtr / TWeakPtr<SWidget>: object, reference controller
// whose widget it is (FocusOwner and the log): 1 / 2 / 3 = in that player's Command_C, or one of these (restructure 3d)
static constexpr int OWNER_NONE = 0;          // a live widget outside every Command_C
static constexpr int OWNER_GONE = -1;         // the widget is gone / unreadable
static constexpr int OWNER_EMPTY_PATH = -2;   // the focus path is empty (PathLeafOwner)
static constexpr int OWNER_NULL_WIDGET = -3;  // a hook argument with no widget in it (OwnerOfArg)
static constexpr int OWNER_NO_ARG = -4;       // a hook argument that isn't there (OwnerOfArg)
static unsigned char* FocusMgr() { return g_Base ? *(unsigned char**)(g_Base + 0x59CA980) : nullptr; }
// player p's battle command system (1 = the game's own, 2 / 3 = the mod-built ones; player 3's may be swapped in)
static unsigned char* BcmOf(int p)
{
    if (p == 1) return MainBcm();
    return ((p == 3) == g_P3Loaded) ? g_Player->ownBcm : g_OtherPlayer->ownBcm;
}
static void* CmdCOf(int p)
{
    return BcmCmdC(BcmOf(p));
}
// +1 shared reference from an object / controller pair (weak or shared); false = gone
static bool SpPin(void* obj, unsigned char* ctrl, GSp& out)
{
    out.obj = nullptr; out.ctrl = nullptr;
    if (!obj || !ctrl || !Readable(ctrl, 0x10)) return false;
    int c = *(int*)(ctrl + 8);
    if (c <= 0) return false;
    *(int*)(ctrl + 8) = c + 1;   // Slate pointers are not thread-safe ones (the game does plain inc / dec, game thread only)
    out.obj = obj; out.ctrl = ctrl;
    return true;
}
// drop one shared reference exactly like the game does (destroy the object at 0, free the controller at weak 0)
static void SpRelease(GSp& s)
{
    unsigned char* c = s.ctrl; s.obj = nullptr; s.ctrl = nullptr;
    if (!c) return;
    if (--*(int*)(c + 8) == 0)
    {
        void** vt = *(void***)c;
        ((void (*)(void*))vt[0])(c);
        if (--*(int*)(c + 0xC) == 0) ((void (*)(void*, int))vt[1])(c, 1);
    }
}
typedef void (*GetCachedWidget_t)(void* uwidget, GSp* out);   // 0x28D95C0 UWidget::GetCachedWidget (+1 ref out)
typedef bool (*IsDescOrSelf_t)(GSp* ancestor, GSp* w);        // 0x142F700 - consumes both (+1 refs)
// player p's Command_C as a Slate widget (+1 ref), false if not built / not shown yet
static bool PinCmdC(int p, GSp& out)
{
    out.obj = nullptr; out.ctrl = nullptr;
    void* w = CmdCOf(p);
    if (!w) return false;
    ((GetCachedWidget_t)(g_Base + 0x28D95C0))(w, &out);
    if (!out.obj) { SpRelease(out); return false; }
    return true;
}
// whose Command_C holds this widget: 2, 3, 1, OWNER_NONE, OWNER_GONE. Game thread only.
static int FocusOwner(void* obj, unsigned char* ctrl)
{
    if (!obj || !ctrl) return OWNER_GONE;
    static const int order[3] = {2, 3, 1};
    for (int i = 0; i < 3; ++i)
    {
        GSp a; if (!PinCmdC(order[i], a)) continue;
        GSp b; if (!SpPin(obj, ctrl, b)) { SpRelease(a); return OWNER_GONE; }
        if (((IsDescOrSelf_t)(g_Base + 0x142F700))(&a, &b)) return order[i];
    }
    return OWNER_NONE;
}
static int PathNum(unsigned char* mgr) { return *(int*)(mgr + 0x10); }
static int PathLeafOwner(unsigned char* mgr)
{
    int n = *(int*)(mgr + 0x10); unsigned char* d = *(unsigned char**)(mgr + 0x08);
    if (n <= 0 || n > 256 || !d || !Readable(d + (size_t)(n - 1) * 16, 16)) return OWNER_EMPTY_PATH;
    unsigned char* e = d + (size_t)(n - 1) * 16;
    return FocusOwner(*(void**)e, *(unsigned char**)(e + 8));
}
// "o:prio o:prio ..." for the focus stack (bottom first)
static void StackDesc(unsigned char* mgr, char* out, size_t cap)
{
    int n = *(int*)(mgr + 0x48); unsigned char* d = *(unsigned char**)(mgr + 0x40);
    size_t p = 0; out[0] = 0;
    if (n < 0 || n > 64 || (n && (!d || !Readable(d, (size_t)n * 0x28)))) { snprintf(out, cap, "(unreadable, num %d)", n); return; }
    for (int i = 0; i < n && p + 24 < cap; ++i)
    {
        unsigned char* e = d + (size_t)i * 0x28;
        int o = FocusOwner(*(void**)e, *(unsigned char**)(e + 8));
        p += snprintf(out + p, cap - p, "%s%c:%d", i ? " " : "", o == OWNER_GONE ? 'x' : (char)('0' + o), *(int*)(e + 0x20));
    }
    if (!n) snprintf(out, cap, "(empty)");
}
static const char* OwnTag(int o) { return o == OWNER_EMPTY_PATH ? "empty" : o == OWNER_GONE ? "gone" : o == OWNER_NONE ? "none" : o == 1 ? "P1" : o == 2 ? "P2" : "P3"; }

// ---- f1.20: TWO COMMAND MENUS AT ONCE - step B: per-player focus contexts (always on since f1.59) ----
// The focus path (+0x08, 0x20 bytes), stack (+0x40, 0x10) and key captures (+0x98, 0x50) of the focus manager are kept
// per player. Player 1's context is the one in the object; players 2 / 3's are kept here and swapped in (raw bytes =
// ownership moves, no reference changes; the game's own writes are move-assigns) only while game code that belongs to
// them runs: a push / pop / focus call on a widget inside their Command_C, their own command system's update, and the
// key events the mod makes from their pad. Player 1's real keys always route along player 1's path.
// g_FCtxOn / g_FCtxLive / g_CtxGateWas are defined in menus/08 (used by earlier files)
struct FocusCtx { unsigned char path[0x20], stack[0x10], capt[0x50], bits[8]; };   // +0x08, +0x40, +0x98, +0x188
// (the two key-state maps at +0xE8 / +0x138 stay shared: the game fills them with fixed keys in the ctor and 0x143E270 writes
//  through their lookups without a null check - empty per-player copies could crash; Tick only advances the live ones anyway)
static FocusCtx g_FCtx[4];
static void FCtxInitOne(FocusCtx& c) { memset(&c, 0, sizeof(c)); *(int*)(c.capt + 0x30) = -1; }   // as the processor's ctor 0x1431010 leaves them
static void FCtxSave(unsigned char* mgr, FocusCtx& c) { memcpy(c.path, mgr + 0x08, 0x20); memcpy(c.stack, mgr + 0x40, 0x10); memcpy(c.capt, mgr + 0x98, 0x50); memcpy(c.bits, mgr + 0x188, 8); }
static void FCtxLoad(unsigned char* mgr, const FocusCtx& c) { memcpy(mgr + 0x08, c.path, 0x20); memcpy(mgr + 0x40, c.stack, 0x10); memcpy(mgr + 0x98, c.capt, 0x50); memcpy(mgr + 0x188, c.bits, 8); }
// make player p's context the live one; returns whose was live (give it to LeaveCtx). Game thread only.
static int EnterCtx(int p)
{
    int prev = g_FCtxLive;
    if (!g_FCtxOn || p == prev || p < 1 || p > 3 || GetCurrentThreadId() != g_GameThreadId) return prev;
    unsigned char* mgr = FocusMgr();
    if (!mgr) return prev;
    FCtxSave(mgr, g_FCtx[prev]);   // the object's bytes now belong to g_FCtx[prev]
    FCtxLoad(mgr, g_FCtx[p]);      // and g_FCtx[p]'s to the object (g_FCtx[p] is a stale copy until it's saved again)
    g_FCtxLive = p;
    return prev;
}
static void LeaveCtx(int prev) { if (prev != g_FCtxLive) EnterCtx(prev); }
static LONG g_FCtxRouted[4] = {0, 0, 0, 0};
static int RouteOf(int owner) { return (owner >= 1 && owner <= 3) ? owner : g_FCtxLive; }   // not in a Command_C: whoever's code is running
