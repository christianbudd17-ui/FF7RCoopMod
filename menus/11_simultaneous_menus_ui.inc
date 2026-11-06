// the game's gamepad keys (EKeys FKey globals: 24 x 0x18 bytes from 0x5920608, FName first), found by name
static uint64_t GpKeyF(const char* want)
{
    static uint64_t f[24]; static char nm[24][48]; static int state = 0;
    if (state == 0)
    {
        state = 1;
        for (int i = 0; i < 24; ++i)
        {
            unsigned char* k = (unsigned char*)(g_Base + 0x5920608 + i * 0x18);
            f[i] = *(uint64_t*)k; nm[i][0] = 0;
            if (f[i]) NameOf(k - 0x18, nm[i], sizeof(nm[i]));
        }
        char buf[900]; int n = 0;
        for (int i = 0; i < 24; ++i) n += snprintf(buf + n, sizeof(buf) - n, " %s", nm[i]);
        CoopLog("MENU2: game gamepad keys:%s", buf);
    }
    for (int i = 0; i < 24; ++i) if (!strcmp(nm[i], want)) return f[i];
    return 0;
}
// ---- the game's UI input dispatcher ----
// before their own command pass: their pad's keys go into the game's UI inside their own focus context
static void Menu2Keys() { if (g_FCtxOn) CtxKeys(); }
