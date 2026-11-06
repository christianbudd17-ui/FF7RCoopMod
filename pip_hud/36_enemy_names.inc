// enemy names. The game only shows the name for player 1's target (EnemyStatus_C, its
// Txt_EnemyName text). Whenever that panel is up, find which enemy it belongs to (nearest in player 1's
// view) and remember the name - per enemy and per enemy type, so others of the same kind get it too.
struct EnName { int idx, ser; void* cls; wchar_t name[48]; };
static EnName g_EnNames[48]; static int g_EnNameN = 0;
static void* g_EnTxtFor = nullptr; static int g_EnTxtIdx = -1, g_EnTxtSer = 0;   // the panel instance's Txt_EnemyName
// (claude/FF7R_Enemy_Names_RE.md): the game's own name for ANY enemy, letter included - native 0x992670
// UnitGetDisplayName(unit = chara+0xCB0, FString* out), the same call that fills EnemyStatus_C's Txt_EnemyName (so a lone
// enemy gets no letter, exactly as the panel). Letters are given to every enemy by the battle manager tick. Cached per
// enemy, refreshed every second; an empty answer (dead / not set up yet) keeps the last name. [General] GameEnemyNames=0 =
// the learning below only. The learning stays as the fallback (and logs NAMECHK MATCH / MISMATCH against the game's name).
typedef void* (*UnitName_t)(void* unit, FStringRaw* out);
struct GameName { int idx = -1, ser = 0; ULONGLONG t = 0; wchar_t name[48] = L""; };
static GameName g_GmNames[32];
static int g_GmState = 0;   // 0 = not checked, 1 = ok, -1 = prologue differs
static const wchar_t* GameEnemyName(void* chara)
{
    if (!g_Cfg.gameEnemyNames || !chara || GetCurrentThreadId() != g_GameThreadId) return nullptr;
    if (g_GmState == 0)
    {
        static const unsigned char ex[17] = {0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x40,0x48,0x8B,0x81,0xB8,0x08,0x00,0x00};
        g_GmState = memcmp((void*)(g_Base + 0x992670), ex, sizeof(ex)) == 0 ? 1 : -1;
        CoopLog("NAMECHK: the game's enemy-name function %s", g_GmState == 1 ? "found - enemy names come from the game" : "NOT as expected (other game version?) - names are learnt as before");
    }
    if (g_GmState != 1) return nullptr;
    int idx, ser; if (!MakeWeak(chara, &idx, &ser)) return nullptr;
    GameName* slot = nullptr; GameName* freeS = nullptr; GameName* oldest = &g_GmNames[0];
    for (auto& g : g_GmNames)
    {
        if (g.idx == idx && g.ser == ser) { slot = &g; break; }
        if (!freeS && (g.idx < 0 || !ResolveWeak(g.idx, g.ser))) freeS = &g;
        if (g.t < oldest->t) oldest = &g;
    }
    ULONGLONG now = GetTickCount64();
    if (slot && now - slot->t < 1000) return slot->name[0] ? slot->name : nullptr;
    if (!slot) { slot = freeS ? freeS : oldest; *slot = GameName(); slot->idx = idx; slot->ser = ser; }
    slot->t = now;
    unsigned char* unit = Readable((unsigned char*)chara + OFF_BATTLE_UNIT, 8) ? *(unsigned char**)((unsigned char*)chara + OFF_BATTLE_UNIT) : nullptr;
    if (!unit || !Readable(unit + 0x5B8, 0x310)) return slot->name[0] ? slot->name : nullptr;   // (+0x5B8 .. +0x8C8: the fields 0x992670 reads)
    FStringRaw out{nullptr, 0, 0};
    ((UnitName_t)(g_Base + 0x992670))(unit, &out);
    wchar_t got[48] = L"";
    if (out.data && out.num > 1 && out.num < 200 && Readable(out.data, (size_t)out.num * 2)) { int n = out.num - 1 < 47 ? out.num - 1 : 47; memcpy(got, out.data, n * 2); got[n] = 0; }
    if (out.data) ((FMemoryFree_t)(g_Base + RVA_FMEMORY_FREE))(out.data);
    if (got[0] && wcscmp(got, slot->name))
    {
        static int logs = 0;
        if (logs < 60) { ++logs; char cn[96]; NameOf(chara, cn, sizeof(cn)); CoopLog("NAMECHK: %s is \"%ls\" (the game's name%s)", cn, got, slot->name[0] ? ", changed" : ""); }
        wcscpy(slot->name, got);
    }
    return slot->name[0] ? slot->name : nullptr;
}
static const wchar_t* EnemyNameOf(void* chara)
{
    if (!chara) return nullptr;
    if (const wchar_t* gn = GameEnemyName(chara)) return gn;
    void* cls = *(void**)((unsigned char*)chara + 0x10);
    const wchar_t* byCls = nullptr;
    for (int i = 0; i < g_EnNameN; ++i)
    {
        if (ResolveWeak(g_EnNames[i].idx, g_EnNames[i].ser) == chara) return g_EnNames[i].name;
        if (g_EnNames[i].cls == cls && !byCls) byCls = g_EnNames[i].name;
    }
    if (!byCls) return nullptr;
    // another enemy of the same kind - its name without the game's A / B / C letter
    static wchar_t base[48]; wcsncpy(base, byCls, 47); base[47] = 0;
    size_t n = wcslen(base);
    if (n >= 3 && base[n - 2] == L' ' && base[n - 1] >= L'A' && base[n - 1] <= L'Z') base[n - 2] = 0;
    return base;
}
static void ResolveTextId(const wchar_t* in, wchar_t* out, int n)
{
    wcsncpy(out, in, n - 1); out[n - 1] = 0;
    if (in[0] != L'$') return;
    static int st = 0; static void* cdo = nullptr; static void* f = nullptr; static int pI = -1, pR = -1;
    if (st == 0)
    {
        st = -1; cdo = FindObj("Default__EndMenuAPI", "EndMenuAPI");
        if (cdo) f = FindFunc(cdo, "GetString");
        if (f) { pI = ParamOff(f, "TextID"); pR = ParamOff(f, "ReturnValue"); }
        if (f && pI >= 0 && pR >= 0 && ParmsSize(f) <= 64) st = 1;
        CoopLog("HUD: text lookup (EndMenuAPI GetString) %s", st == 1 ? "ready" : "NOT available");
    }
    if (st != 1) return;
    alignas(16) unsigned char b[64] = {};
    FStringRaw fs{ (wchar_t*)in, (int)wcslen(in) + 1, (int)wcslen(in) + 1 };
    memcpy(b + pI, &fs, sizeof(fs));
    Orig_ProcessEvent(cdo, f, b);
    FStringRaw r = *(FStringRaw*)(b + pR);
    if (r.data && r.num > 1 && r.num < 200 && Readable(r.data, (size_t)r.num * 2)) { wcsncpy(out, r.data, n - 1); out[n - 1] = 0; }
}
static void EnemyNameLearn(void* canvas)
{
    static ULONGLONG last = 0; ULONGLONG now = GetTickCount64();
    if (now - last < 200) return;
    last = now;
    if (g_Hc.state == 0 && !HcResolve(canvas)) return;
    if (g_Hc.state != 1) return;
    void* panel = nullptr;
    for (int i = 0; i < g_RwN && !panel; ++i)
    {
        void* w = ResolveWeak(g_RwIdx[i], g_RwSer[i]); if (!w) continue;
        char nm[64]; ObjClassName(w, nm, sizeof(nm));
        if (!strcmp(nm, "EnemyStatus_C")) panel = w;
    }
    if (!panel) return;
    unsigned char* o = (unsigned char*)panel;
    if (o[0xB7] == 1 || o[0xB7] == 2) return;
    void* txt = (g_EnTxtFor == panel) ? ResolveWeak(g_EnTxtIdx, g_EnTxtSer) : nullptr;
    if (!txt && g_EnTxtFor == panel && g_EnTxtIdx == -1) return;   // already looked in this panel - not there
    if (!txt)
    {   // find this panel's Txt_EnemyName (an object whose outer is the panel's widget tree) - once per panel
        g_EnTxtFor = panel; g_EnTxtIdx = -1; g_EnTxtSer = 0;
        void* tree = *(void**)(o + 0x198);
        unsigned char* items = *(unsigned char**)(g_Base + RVA_GOBJ_OBJECTS); int num = *(int*)(g_Base + RVA_GOBJ_NUM);
        for (int i = 0; tree && items && i < num; ++i)
        {
            unsigned char* ob = *(unsigned char**)(items + (size_t)i * 24);
            if (!ob || *(void**)(ob + 0x20) != tree) continue;
            if (NameIs(ob, "Txt_EnemyName")) { MakeWeak(ob, &g_EnTxtIdx, &g_EnTxtSer); txt = ob; break; }
        }
        CoopLog("HUD: enemy name text %s", txt ? "found" : "NOT found in the enemy status panel");
        if (!txt) return;
    }
    FStringRaw sraw = *(FStringRaw*)((unsigned char*)txt + 0x118);
    if (!sraw.data || sraw.num <= 1 || sraw.num > 120 || !Readable(sraw.data, (size_t)sraw.num * 2)) return;
    // the name panel doesn't sit on its enemy (v11.8 matched the panel and got names wrong in groups).
    // The target reticle (BattleTarget_*) does: use it, and only when one enemy is clearly the closest.
    void* ret = nullptr;
    for (int i = 0; i < g_RwN && !ret; ++i)
    {
        void* w = ResolveWeak(g_RwIdx[i], g_RwSer[i]); if (!w) continue;
        char nm[64]; ObjClassName(w, nm, sizeof(nm));
        if (strstr(nm, "BattleTarget") && ((unsigned char*)w)[0xB7] != 1 && ((unsigned char*)w)[0xB7] != 2) ret = w;
    }
    static void* pairEn = nullptr; static wchar_t pairTxt[120]; static int pairN = 0;
    if (!ret) { pairN = 0; return; }
    float x, y, rw, rh, sc;
    if (!HcRect(ret, &x, &y, &rw, &rh, &sc)) return;
    float cx = x + rw * 0.5f, cy = y + rh * 0.5f;
    void* en[24]; int ne = CollectEnemies(en, 24);
    int best = -1; float bd = 1e12f, bd2 = 1e12f;
    float clipY = *(float*)((unsigned char*)canvas + OFF_CANVAS_CLIPY);
    for (int e = 0; e < ne; ++e)
    {
        float l[3], p[3];
        if (!ActorLoc(en[e], l) || !CanvasProject(canvas, l, p)) continue;
        float dx = p[0] - cx, dy = p[1] - cy, d = dx * dx + dy * dy;
        if (d < bd) { bd2 = bd; bd = d; best = e; } else if (d < bd2) bd2 = d;
    }
    float lim = 160.f * clipY / 1080.f;
    if (best < 0 || bd > lim * lim || bd2 < bd * 2.25f) { pairN = 0; return; }   // too far, or another enemy nearly as close
    wchar_t raw[120]; int n = sraw.num - 1 < 119 ? sraw.num - 1 : 119; memcpy(raw, sraw.data, n * 2); raw[n] = 0;
    if (pairEn == en[best] && !wcscmp(pairTxt, raw)) ++pairN; else { pairEn = en[best]; wcscpy(pairTxt, raw); pairN = 1; }
    if (pairN < 3) return;   // the same enemy + the same name for 3 checks (~0.6 s)
    EnName e{}; ResolveTextId(raw, e.name, 48);
    if (!e.name[0]) return;
    int have = -1;
    for (int i = 0; i < g_EnNameN; ++i) if (ResolveWeak(g_EnNames[i].idx, g_EnNames[i].ser) == en[best]) have = i;
    if (have >= 0)
    {
        if (!wcscmp(g_EnNames[have].name, e.name)) return;   // already right
        char cn[96]; NameOf(en[best], cn, sizeof(cn));
        CoopLog("HUD: enemy %s renamed \"%ls\" -> \"%ls\"", cn, g_EnNames[have].name, e.name);
        wcscpy(g_EnNames[have].name, e.name); return;
    }
    if (!MakeWeak(en[best], &e.idx, &e.ser)) return;
    e.cls = *(void**)((unsigned char*)en[best] + 0x10);
    if (g_EnNameN >= 48) { memmove(g_EnNames, g_EnNames + 1, sizeof(EnName) * 47); g_EnNameN = 47; }
    g_EnNames[g_EnNameN++] = e;
    static int logs = 0; if (logs < 30) { ++logs; char cn[96]; NameOf(en[best], cn, sizeof(cn)); CoopLog("HUD: enemy %s is \"%ls\" (text \"%ls\", reticle match %.0f px)", cn, e.name, raw, sqrtf(bd)); }
    {   // compare with the game's own name (log only)
        static int clogs = 0; const wchar_t* gn = GameEnemyName(en[best]);
        if (gn && clogs < 40) { ++clogs; CoopLog("NAMECHK: panel \"%ls\" game \"%ls\" %s", e.name, gn, wcscmp(gn, e.name) ? "MISMATCH" : "MATCH"); }
    }
}
