// =================================================================================
// WHO IS PLAYER 2? (party identity, party changes, empty slot)
// =================================================================================
// The party manager (0x657AC0) keeps the party order as character "types" at +0x230..+0x232,
// with the leader's type at +0x233. GetPartyMember(i) (0x15EF5B0) = CharByType(order[i]), and the
// order is re-arranged when the leader changes (slot 0 is always the current leader).
// So player 2 is tracked by CHARACTER (type), not by position:
//  - picked as order[PartySlot-1] the first time, if that isn't player 1's character;
//  - kept until that character leaves the party (story party change) or becomes the leader;
//    then player 2 is reassigned to whoever is now at PartySlot (rule: "player 2 follows slot 2");
//  - if nobody suitable is there (empty slot), P2Char() is null and every co-op feature idles.
static constexpr uintptr_t RVA_PARTY_MGR = 0x657AC0, RVA_PARTY_COUNT = 0x15EEFE0;
static constexpr uintptr_t RVA_CHAR_BY_TYPE = 0x15F0650, RVA_IS_ENDCHAR = 0x67FD30;
typedef unsigned char* (*PartyMgr_t)();
typedef int   (*PartyCount_t)();
typedef void* (*CharByType_t)(void* mgr, int zero, unsigned char type, void* null4);
typedef bool  (*IsEndChar_t)(void* obj);

static unsigned char* PartyMgr() { return ((PartyMgr_t)(g_Base + RVA_PARTY_MGR))(); }
static int PartyCount() { int n = ((PartyCount_t)(g_Base + RVA_PARTY_COUNT))(); return n < 0 ? 0 : (n > 3 ? 3 : n); }
static int OrderType(unsigned char* mgr, int i) { return (mgr && i >= 0 && i < 3) ? mgr[0x230 + i] : -1; }
static int LeaderType(unsigned char* mgr) { return mgr ? mgr[0x233] : -1; }
static void* CharByType(unsigned char* mgr, int type)
{
    if (!mgr || type < 0) return nullptr;
    void* c = ((CharByType_t)(g_Base + RVA_CHAR_BY_TYPE))(mgr, 0, (unsigned char)type, nullptr);
    if (c && ((IsEndChar_t)(g_Base + RVA_IS_ENDCHAR))(c)) return c;
    return nullptr;
}
// the LEADER's character (the one player 1's controller / the game's camera follow). NOTE: GetPartyMember(0) is NOT
// the leader - it is the first character in the party ORDER (mgr+0x230, RE of 0x15EF5B0 2026-10-19), which stays Cloud
// after player 1 switches to Tifa. Falls back to GetPartyMember(0) when the leader can't be read.
static void* LeaderChar()
{
    unsigned char* m = g_Base ? PartyMgr() : nullptr;
    void* c = m ? CharByType(m, LeaderType(m)) : nullptr;
    return c ? c : (GetPartyMember ? GetPartyMember(0) : nullptr);
}
static void NameOf(void* uobj, char* out, size_t outLen); // defined later
static bool Readable(const void* p, size_t n);   // defined further down
// a character's current HP via its battle unit (chara+0xCB0, vtable +0x80 GetHP). -1 = unknown.
static int CharHP(void* chara)
{
    unsigned char* c = (unsigned char*)chara;
    if (!c || !g_Base || !Readable(c + OFF_BATTLE_UNIT, 8)) return -1;
    unsigned char* unit = *(unsigned char**)(c + OFF_BATTLE_UNIT);
    if (!unit || !Readable(unit, 8)) return -1;
    void** vt = *(void***)unit;
    if (!vt || !Readable(vt + 0x80 / 8, 8)) return -1;
    uintptr_t fn = (uintptr_t)vt[0x80 / 8];
    if (fn < g_Base || fn > g_Base + 0x5000000) return -1;
    int hp = ((int (*)(void*))fn)(unit);
    return (hp < 0 || hp > 999999) ? -1 : hp;
}
// (review #31): ONE walk over the party order (party manager +0x230, PartyCount members). The callers had ~6 copies of
// this loop with DIFFERENT rules - which members they skip, and how they treat an HP that can't be read (-1). Each caller
// keeps its own rule, written out at the call with the two names below. m.Char() looks the character up when asked.
struct PartyMember
{
    unsigned char* mgr; int index, type;   // type -1 = can't be read
    void* chr; bool chrDone;
    void* Char() { if (!chrDone) { chr = CharByType(mgr, type); chrDone = true; } return chr; }   // nullptr = none
};
template <class F> static void ForEachPartyMember(unsigned char* mgr, F&& f)   // f(PartyMember&), in party order
{
    int n = PartyCount();
    for (int i = 0; i < n; ++i) { PartyMember m{mgr, i, OrderType(mgr, i), nullptr, false}; f(m); }
}
static bool NotKnockedOut(void* c) { return CharHP(c) != 0; }   // HP not 0 - an HP that can't be read counts as standing
static bool SurelyStanding(void* c) { return CharHP(c) > 0; }   // HP above 0 - an HP that can't be read counts as down
// The character player 2 controls right now, or null (empty slot / not available).
static void* P2Char()
{
    if (!g_Base) return nullptr;
    unsigned char* mgr = PartyMgr();
    if (!mgr) return nullptr;
    int n = PartyCount();
    int lead = LeaderType(mgr);
    bool inParty = false;
    for (int i = 0; i < n; ++i) if (OrderType(mgr, i) == g_Player->charType) inParty = true;
    if (g_Player->charType < 0 || !inParty || g_Player->charType == lead || (g_OtherType >= 0 && g_Player->charType == g_OtherType))
    {
        int cand = (g_Player->partySlot < n) ? OrderType(mgr, g_Player->partySlot) : -1;
        if (cand == lead || (cand >= 0 && cand == g_OtherType)) cand = -1;   // not the other human's
        if (cand < 0)   // the other human switched onto our slot's character (D-pad) - take a free one
            for (int i = 0; i < n && cand < 0; ++i)   // from 0 (the party order isn't always re-sorted when the leader changes)
            {
                int t = OrderType(mgr, i);
                if (t >= 0 && t != lead && t != g_OtherType && CharByType(mgr, t)) cand = t;
            }
        if (cand != g_Player->charType)
        {
            int old = g_Player->charType;
            g_Player->charType = cand;
            void* c = CharByType(mgr, cand);
            char nm[96] = "nobody"; if (c) NameOf(c, nm, sizeof(nm));
            CoopLog("PARTY: player %d is now %s (type %d, was %d; party of %d, leader type %d)%s",
                g_Player->num, nm, cand, old, n, lead, cand < 0 ? " - slot empty, co-op idles until someone is there" : "");
        }
    }
    return CharByType(mgr, g_Player->charType);
}
// Index of a character type in the party order (= its command-menu page), or -1.
static int OrderIndexOf(int type)
{
    unsigned char* mgr = g_Base ? PartyMgr() : nullptr;
    int n = PartyCount();
    for (int i = 0; i < n; ++i) if (OrderType(mgr, i) == type) return i;
    return -1;
}
static int P2Page() { return OrderIndexOf(g_Player->charType); }
static int P1Page() { unsigned char* m = g_Base ? PartyMgr() : nullptr; int i = OrderIndexOf(LeaderType(m)); return i < 0 ? 0 : i; }

static float NormStick(SHORT v, int deadzone)
{
    int iv = v;
    if (iv > -deadzone && iv < deadzone) return 0.f;
    float sign = iv < 0 ? -1.f : 1.f;
    float mag = (float)(iv < 0 ? -iv : iv) - (float)deadzone;
    float out = mag / (32767.f - (float)deadzone);
    if (out > 1.f) out = 1.f;
    return sign * out;
}

