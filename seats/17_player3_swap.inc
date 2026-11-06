// =================================================================================
// PLAYER 3 - the state swap
// =================================================================================
// (restructure step 2a: every per-player value that used to be swapped is in the players' records - core/02 PlayerSlot)
static bool g_P3Init = false;

static void P3WinUpdate();
// Switch the current player (player 2 <-> player 3): the two records swap roles (each record points at its player's
// own settings, g_Player->cfg - nothing is copied).
static void RawSwapP3()
{
    PlayerSlot* t = g_Player; g_Player = g_OtherPlayer; g_OtherPlayer = t;
}
static void LoadP3(bool want)
{
    if (g_P3Loaded == want || !g_P3Enabled) return;
    if (!g_P3Init)
    {
        g_P3Init = true;
        g_Player3Slot.partySlot = g_P3Cfg.p2Party - 1; g_Player3Slot.joined = g_P3Cfg.joinAtStart;
    }
    int otherBefore = g_Player->joined ? g_Player->charType : -1;   // the player being swapped out
    RawSwapP3();
    g_P3Loaded = want;
    g_OtherType = otherBefore;
}
static bool  MakeWeak(void* obj, int* outIndex, int* outSerial); // defined further down

