// ---- v5.9: "player rules" for human-controlled party members ----
// The game checks "is this the leader?" (unit+0x58 character type == PartyMgr+0x233) and, if not,
// applies AI-party-member (AIPC) rates. Found in ff7remake_.exe:
//  - ATB gauge over time:  0x8A3100, multiplies by vfunc unit+0x930 = 0x8A33B0 (ATBTimeCoefficientAIPC)
//  - ATB from actions:     0x96EE90, multiplies by vfunc unit+0x1B0 = 0x96F120 (ATBActionCoefficientAIPC)
//  - ATB while guarding:   0x986CF0 @0x987083 picks ATBTimeCoefficientGuard(AIPC)
//  - stagger from normal attacks: 0x966EE0 @0x966FD4 applies BPDamageScaleNormalAttackAIPC
//  - enemy hate (aggro):   0x93ED90 @0x93F27A picks Hate*UCPC (leader) or Hate*AIPC rates
// The two getters are hooked to return 1.0 (= the leader's rate) for human players' units. The three
// inline checks are replaced by small stubs: the original "is leader" test, OR "is this character type
// in our mask". The masks are refreshed every frame from who's playing, so with nobody joined (or the
// settings off) everything behaves exactly like the game.
static volatile uint32_t g_MaskRates[8] = {0}, g_MaskHate[8] = {0};
static volatile uint32_t g_MaskHuman[8] = {0};   // every character type a person plays (players 2/3), no setting
static bool InMask(volatile uint32_t* m, unsigned t) { return t < 256 && (m[t >> 5] & (1u << (t & 31))); }
typedef float (*UnitGetF_t)(void* unit);
static UnitGetF_t Orig_ATBTimeAIPC = nullptr, Orig_ATBActAIPC = nullptr;
static volatile float g_TypeRate[256];   // per character type, the human player's ATB rate (share of player 1's)
static float Hook_ATBTimeAIPC(void* unit)
{
    if (unit && InMask(g_MaskRates, ((unsigned char*)unit)[0x58])) return g_TypeRate[((unsigned char*)unit)[0x58]];
    return Orig_ATBTimeAIPC(unit);
}
static float Hook_ATBActAIPC(void* unit)
{
    if (unit && InMask(g_MaskRates, ((unsigned char*)unit)[0x58])) return g_TypeRate[((unsigned char*)unit)[0x58]];
    return Orig_ATBActAIPC(unit);
}
// player 1's (the leader's) rate. Both ATB functions skip the AIPC coefficient for the leader
// (unit+0x58 == PartyMgr+0x233), so they're wrapped: the result is multiplied by the leader seat's
// percentage. 0% = the AI party member's coefficient (what the leader would get as an AI teammate).
static volatile float g_LeadRate = 1.f;
typedef float (*UnitATB_t)(void* unit, uint64_t a2);
static UnitATB_t Orig_ATBTime = nullptr, Orig_ATBAct = nullptr;
static bool UnitIsLeader(void* unit)
{
    unsigned char* m = PartyMgr();
    return unit && m && ((unsigned char*)unit)[0x58] == m[0x233];
}
static float Hook_ATBTime(void* unit, uint64_t a2)
{
    float r = Orig_ATBTime(unit, a2);
    float k = g_LeadRate;
    if (k != 1.f && UnitIsLeader(unit)) r *= (k > 0.f ? k : (Orig_ATBTimeAIPC ? Orig_ATBTimeAIPC(unit) : 1.f));
    return r;
}
static float Hook_ATBAct(void* unit, uint64_t a2)
{
    float r = Orig_ATBAct(unit, a2);
    float k = g_LeadRate;
    if (k != 1.f && UnitIsLeader(unit)) r *= (k > 0.f ? k : (Orig_ATBActAIPC ? Orig_ATBActAIPC(unit) : 1.f));
    return r;
}
// StubBP: 71 bytes; PM@2 MASK@30 T1@49 T2@63
static const unsigned char kStubBP[] = {0x48,0xb8,0x11,0x11,0x11,0x11,0x11,0x11,0x11,0x11,0xff,0xd0,0x0f,0xb6,0x88,0x33,0x02,0x00,0x00,0x38,0x4b,0x58,0x74,0x21,0x0f,0xb6,0x4b,0x58,0x48,0xba,0x22,0x22,0x22,0x22,0x22,0x22,0x22,0x22,0x0f,0xa3,0x0a,0x72,0x0e,0xff,0x25,0x00,0x00,0x00,0x00,0x33,0x33,0x33,0x33,0x33,0x33,0x33,0x33,0xff,0x25,0x00,0x00,0x00,0x00,0x44,0x44,0x44,0x44,0x44,0x44,0x44,0x44};
static const int kStubBPOff[4] = {2,30,49,63};
// StubGuard: 78 bytes; PM@2 MASK@37 T1@56 T2@70
static const unsigned char kStubGuard[] = {0x48,0xb8,0x11,0x11,0x11,0x11,0x11,0x11,0x11,0x11,0xff,0xd0,0x89,0xee,0x4c,0x89,0xff,0x0f,0xb6,0x88,0x33,0x02,0x00,0x00,0x41,0x38,0x4e,0x58,0x74,0x22,0x41,0x0f,0xb6,0x4e,0x58,0x48,0xba,0x22,0x22,0x22,0x22,0x22,0x22,0x22,0x22,0x0f,0xa3,0x0a,0x72,0x0e,0xff,0x25,0x00,0x00,0x00,0x00,0x33,0x33,0x33,0x33,0x33,0x33,0x33,0x33,0xff,0x25,0x00,0x00,0x00,0x00,0x44,0x44,0x44,0x44,0x44,0x44,0x44,0x44};
static const int kStubGuardOff[4] = {2,37,56,70};
// StubHate: 78 bytes; PM@2 MASK@35 T1@55 T2@70
static const unsigned char kStubHate[] = {0x48,0xb8,0x11,0x11,0x11,0x11,0x11,0x11,0x11,0x11,0xff,0xd0,0x0f,0xb6,0x88,0x33,0x02,0x00,0x00,0x41,0x38,0x4e,0x58,0x0f,0x94,0xc0,0x74,0x15,0x41,0x0f,0xb6,0x4e,0x58,0x48,0xba,0x22,0x22,0x22,0x22,0x22,0x22,0x22,0x22,0x0f,0xa3,0x0a,0x0f,0x92,0xc0,0xff,0x25,0x00,0x00,0x00,0x00,0x33,0x33,0x33,0x33,0x33,0x33,0x33,0x33,0x90,0xff,0x25,0x00,0x00,0x00,0x00,0x44,0x44,0x44,0x44,0x44,0x44,0x44,0x44};
static const int kStubHateOff[4] = {2,35,55,70};

static bool PatchLeaderSite(uintptr_t rva, const unsigned char* expect, size_t len, const unsigned char* stub, size_t stubLen,
                            const int off[4], volatile uint32_t* mask, uintptr_t t1, uintptr_t t2, const char* name)
{
    unsigned char* site = (unsigned char*)(g_Base + rva);
    if (len < 14 || memcmp(site, expect, len) != 0) { CoopLog("HOOK FAILED %s: bytes differ - not patched", name); ++g_HooksFailed; return false; }
    unsigned char* mem = (unsigned char*)VirtualAlloc(nullptr, 128, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!mem) { CoopLog("HOOK FAILED %s: VirtualAlloc", name); ++g_HooksFailed; return false; }
    memcpy(mem, stub, stubLen);
    *(uint64_t*)(mem + off[0]) = g_Base + RVA_PARTY_MGR;
    *(uint64_t*)(mem + off[1]) = (uint64_t)mask;
    *(uint64_t*)(mem + off[2]) = g_Base + t1;
    *(uint64_t*)(mem + off[3]) = g_Base + t2;
    FlushInstructionCache(GetCurrentProcess(), mem, 128);
    DWORD old;
    if (!VirtualProtect(site, len, PAGE_EXECUTE_READWRITE, &old)) { CoopLog("HOOK FAILED %s: VirtualProtect", name); ++g_HooksFailed; return false; }
    unsigned char patch[32]; memset(patch, 0x90, sizeof(patch));
    patch[0] = 0xFF; patch[1] = 0x25; *(DWORD*)(patch + 2) = 0; *(uint64_t*)(patch + 6) = (uint64_t)mem;
    bool clear;
    {   // (finding #34) the other threads stay paused only for the write - NO log line / allocation in this block
        OtherThreadsPaused p((uintptr_t)site, len);
        clear = p.clear;
        memcpy(site, patch, len);
        FlushInstructionCache(GetCurrentProcess(), site, len);
    }
    if (!clear) CoopLog("HOOK NOTE %s: a game thread was inside its first bytes 20 times - written anyway (as before f1.205)", name);
    VirtualProtect(site, len, old, &old);
    FlushInstructionCache(GetCurrentProcess(), site, len);
    CoopLog("Patched %s", name);
    return true;
}
static void* MakeInlineHook(uintptr_t target, void* hook, size_t stealLen, const unsigned char* expect, const char* name, void** outOrig);
static void InstallHumanRulePatches()
{
    static const unsigned char exGetter[15] = {0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x20};
    void* t1 = MakeInlineHook(g_Base + 0x8A33B0, (void*)Hook_ATBTimeAIPC, 15, exGetter, "ATBTimeCoefficientAIPC getter");
    if (t1) Orig_ATBTimeAIPC = (UnitGetF_t)t1;
    void* t2 = MakeInlineHook(g_Base + 0x96F120, (void*)Hook_ATBActAIPC, 15, exGetter, "ATBActionCoefficientAIPC getter");
    if (t2) Orig_ATBActAIPC = (UnitGetF_t)t2;
    static const unsigned char exTime[15] = {0x48,0x8b,0xc4,0x48,0x89,0x58,0x08,0x48,0x89,0x68,0x10,0x48,0x89,0x70,0x18};
    void* t3 = MakeInlineHook(g_Base + 0x8A3100, (void*)Hook_ATBTime, 15, exTime, "ATB over time (player 1 rate)");
    if (t3) Orig_ATBTime = (UnitATB_t)t3;
    static const unsigned char exAct[15] = {0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18};
    void* t4 = MakeInlineHook(g_Base + 0x96EE90, (void*)Hook_ATBAct, 15, exAct, "ATB from actions (player 1 rate)");
    if (t4) Orig_ATBAct = (UnitATB_t)t4;
    static const unsigned char exBP[17]    = {0xe8,0xe7,0x0a,0xcf,0xff,0x0f,0xb6,0x88,0x33,0x02,0x00,0x00,0x38,0x4b,0x58,0x74,0x74};
    static const unsigned char exGuard[23] = {0xe8,0x38,0x0a,0xcd,0xff,0x8b,0xf5,0x49,0x8b,0xff,0x0f,0xb6,0x88,0x33,0x02,0x00,0x00,0x41,0x38,0x4e,0x58,0x75,0x30};
    static const unsigned char exHate[19]  = {0xe8,0x41,0x88,0xd1,0xff,0x0f,0xb6,0x88,0x33,0x02,0x00,0x00,0x41,0x38,0x4e,0x58,0x0f,0x94,0xc0};
    PatchLeaderSite(0x966FD4, exBP, 17, kStubBP, sizeof(kStubBP), kStubBPOff, g_MaskRates, 0x966FE5, 0x967059, "stagger AIPC check (0x966FD4)");
    PatchLeaderSite(0x987083, exGuard, 23, kStubGuard, sizeof(kStubGuard), kStubGuardOff, g_MaskRates, 0x9870CA, 0x98709A, "guard ATB AIPC check (0x987083)");
    PatchLeaderSite(0x93F27A, exHate, 19, kStubHate, sizeof(kStubHate), kStubHateOff, g_MaskHate, 0x93F28D, 0x93F28D, "enemy hate UCPC check (0x93F27A)");
    // (the author: "P3 was able to build the stagger bar all the way to Max, but it took a hit from P1 to actually
    // trigger it"; "Stagger should only happen from human controlled players, NOT AI players"): in the damage code
    // 0x966EE0, once a hit fills the enemy's gauge, the stagger only STARTS (0x967820) if the hitter is the leader
    // (unit +0x58 == PartyMgr +0x233, 0x967374) - or a party member whose attack has the flag [attack +0x40] +0x50 (some AI
    // abilities). Otherwise the gauge waits at max. Players 2/3 now count as the leader here; AI teammates keep the game's rule.
    static const unsigned char exBurst[17] = {0xe8,0x47,0x07,0xcf,0xff,0x0f,0xb6,0x88,0x33,0x02,0x00,0x00,0x38,0x4b,0x58,0x74,0x2b};
    PatchLeaderSite(0x967374, exBurst, 17, kStubBP, sizeof(kStubBP), kStubBPOff, g_MaskHuman, 0x967385, 0x9673B0, "stagger start leader check (0x967374)");
}
// Called every frame: which character types are being played by a person (besides player 1).
static void SetHumanMasks(const int* types, int n)
{
    uint32_t m[8] = {0}, mr[8] = {0};
    for (int i = 0; i < n; ++i) if (types[i] >= 0 && types[i] < 256)
    {
        m[types[i] >> 5] |= 1u << (types[i] & 31);
        int person = i < 2 ? g_SeatPerson[i + 1] : 1; if (person < 0 || person > 2) person = i + 1;
        float pct = g_Cfg.ratePct[person];   // types[0] = seat 2's character, types[1] = seat 3's - the rate follows whoever sits there
        if (pct > 0.f) { mr[types[i] >> 5] |= 1u << (types[i] & 31); g_TypeRate[types[i]] = pct; }
    }
    {   // the leader's (seat 1's) rate. f1.276 (the author: "How do you know someone wouldn't want to adjust their ATB when
        // playing single player?"): also when nobody else plays - the one character set's (default 100 = the game's normal) or
        // the Global set's (Same difficulty: "their global settings apply all the time"). Was: only while someone else played.
        int p = g_SeatPerson[0]; if (p < 0 || p > 2) p = 0;
        g_LeadRate = g_Cfg.ratePct[p];
    }
    for (int i = 0; i < 8; ++i)
    {
        g_MaskRates[i] = mr[i];
        g_MaskHuman[i] = m[i];
        g_MaskHate[i] = g_Cfg.humanAggro ? m[i] : 0;
    }
    static uint32_t last = 0xFFFFFFFF;
    if (m[0] != last) { last = m[0]; CoopLog("PLAYER RULES: human-played character types mask 0x%08X (rates %d, aggro %d)", m[0], (int)g_Cfg.humanRates, (int)g_Cfg.humanAggro); }
}

static void CmdMenuTick();


