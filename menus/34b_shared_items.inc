// =================================================================================
// SHARED ITEMS - queued item commands count as used (always on - no longer a setting).
// RE: claude/FF7R_Shared_Items_RE.md. The game reserves an item only when a character's item command is
// DISPATCHED (0x141B1C0: check 0x16972F0, ReserveItem 0xB07950) and takes it when the effect lands (0xB07BA0);
// GetItemNum 0xB25620 = max(0, inventory - reserved) is what the command menu / palette use to gray an item and to
// refuse a confirm. Commands still waiting in a member's queue reserve nothing, so with one Potion left two players
// could both pick it and the slower one's order was silently dropped. Now GetItemNum also subtracts every queued,
// not-yet-dispatched item command of that item in every command system (player 1's + the mod's own ones), so the
// last Potion grays out for everybody as soon as someone picks it. While the game dispatches a command (0x141ABD0)
// the plain value is returned (the dispatching command must not count against itself). The vanilla
// "ReleaseAllByItem" quirk (0xB07AD0) is left as it is (the author, 2026-10-19).
// BCM queue layout (verified): bcm+0x08 queue array / +0x10 count, 0xC8 per member: +0x00 state (1 popped,
// 2 retry, 3 done), +0x08 working command, +0xB8 pending array (+0xC0 count), commands 0x58 bytes: +0x08 type
// (5 = item), +0x38 item FName.
// =================================================================================
typedef int  (*GetItemNum_t)(void* mgr, uint64_t item, unsigned char alt);
typedef uint64_t (*BcmDispatch_t)(void* bcm, void* cmd, void* a3, void* a4);
typedef int  (*ItemIdOf_t)(uint64_t item);       // 0xB7B780: FName -> inventory item ID (-1 unknown)
typedef int  (*PartyCountAll_t)(unsigned char all);  // 0x15EF150 (the dispatcher's own bound, 0x141AA54)
static GetItemNum_t Orig_GetItemNum = nullptr;
static BcmDispatch_t Orig_BcmDispatch = nullptr;
static int g_ItemDispatchDepth = 0;
static bool g_ItemReserveOn = false;   // latched at hook install
static int ItemIdOf(uint64_t item) { return item ? ((ItemIdOf_t)(g_Base + 0xB7B780))(item) : -1; }
static uint64_t Hook_BcmDispatch(void* bcm, void* cmd, void* a3, void* a4)
{
    ++g_ItemDispatchDepth;
    uint64_t r = Orig_BcmDispatch(bcm, cmd, a3, a4);
    --g_ItemDispatchDepth;
    return r;
}
// queued (not yet dispatched) item commands of item ID 'id' in one command system; *seen = item commands looked at
static int QueuedItemsIn(unsigned char* bcm, int id, int party, int* who)
{
    if (!bcm || !Readable(bcm + 0x08, 0x10)) return 0;
    unsigned char* q = *(unsigned char**)(bcm + 0x08); int n = *(int*)(bcm + 0x10);
    if (!q || n <= 0 || n > 16) return 0;
    if (n > party) n = party;
    if (!Readable(q, (size_t)n * 0xC8)) return 0;
    int c = 0;
    for (int i = 0; i < n; ++i)
    {
        unsigned char* m = q + (size_t)i * 0xC8;
        int st = *(int*)m;
        unsigned char* w = m + 0x08;   // the working command: still waiting while state 1 (popped) / 2 (retry)
        if ((st == 1 || st == 2) && *(int*)(w + 0x08) == 5 && ItemIdOf(*(uint64_t*)(w + 0x38)) == id) { ++c; *who |= 1 << i; }
        unsigned char* p = *(unsigned char**)(m + 0xB8); int pn = *(int*)(m + 0xC0);
        if (!p || pn <= 0 || pn > 8 || !Readable(p, (size_t)pn * 0x58)) continue;
        for (int k = 0; k < pn; ++k)
        {
            unsigned char* e = p + (size_t)k * 0x58;
            if (*(int*)(e + 0x08) == 5 && ItemIdOf(*(uint64_t*)(e + 0x38)) == id) { ++c; *who |= 1 << i; }
        }
    }
    return c;
}
static int Hook_GetItemNum(void* mgr, uint64_t item, unsigned char alt)
{
    int r = Orig_GetItemNum(mgr, item, alt);
    if (!g_ItemReserveOn || alt || g_ItemDispatchDepth > 0 || r <= 0 || GetCurrentThreadId() != g_GameThreadId) return r;
    // In battles only (player 1's command system updated within 0.5 s and in its battle state 8 / 9): outside battles some
    // game code writes GetItemNum's answer back to the inventory (field item use 0x1106D40, 0xAFE590, 0x125FD90), so a
    // leftover queued command must never lower it there.
    if (!InBattleNow()) return r;
    int id = ItemIdOf(item);
    if (id < 0) return r;
    int party = ((PartyCountAll_t)(g_Base + 0x15EF150))(1);
    if (party <= 0) return r;   // the game's dispatcher would look at no queue either
    if (party > 8) party = 8;
    int who = 0;
    unsigned char* p1 = MainBcm();
    int q = QueuedItemsIn(p1, id, party, &who);
    // players 2 / 3's own command systems - only while on (their queues are emptied by SetBattle off); never one twice
    unsigned char* b2[2] = {g_Player->ownBcmOn ? g_Player->ownBcm : nullptr, g_OtherPlayer->ownBcmOn ? g_OtherPlayer->ownBcm : nullptr};
    if (b2[1] == b2[0]) b2[1] = nullptr;
    for (int i = 0; i < 2; ++i) if (b2[i] && b2[i] != p1) q += QueuedItemsIn(b2[i], id, party, &who);
    if (!q) return r;
    int left = r - q; if (left < 0) left = 0;
    static int logs = 0; static int lastId = -2, lastLeft = -2;
    if (logs < 60 && (id != lastId || left != lastLeft)) { ++logs; lastId = id; lastLeft = left; CoopLog("ITEMS: item %d - %d in stock for the menus, %d queued but not started yet (members 0x%X) -> menus show %d", id, r, q, who, left); }
    return left;
}
static void InstallSharedItemHooks()
{
    g_ItemReserveOn = true;   // always (ReserveQueuedItems is no longer a setting)
    static const unsigned char exNum[15] = {0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x54,0x24,0x10,0x55,0x56,0x57,0x41,0x54};
    static const unsigned char exDisp[15] = {0x48,0x8B,0xC4,0x48,0x89,0x48,0x08,0x55,0x56,0x41,0x55,0x48,0x8D,0x68,0xA1};
    void* t = MakeInlineHook(g_Base + 0x141ABD0, (void*)Hook_BcmDispatch, 15, exDisp, "command dispatch (item guard)");
    if (!t) { g_ItemReserveOn = false; CoopLog("ITEMS: dispatch hook failed - queued item commands are NOT counted"); return; }
    Orig_BcmDispatch = (BcmDispatch_t)t;
    t = MakeInlineHook(g_Base + 0xB25620, (void*)Hook_GetItemNum, 15, exNum, "GetItemNum (queued items)");
    if (!t) { g_ItemReserveOn = false; CoopLog("ITEMS: GetItemNum hook failed - queued item commands are NOT counted"); return; }
    Orig_GetItemNum = (GetItemNum_t)t;
    CoopLog("ITEMS: queued item commands count as used - the last item grays out for everyone once someone picks it (always since f1.54)");
}
