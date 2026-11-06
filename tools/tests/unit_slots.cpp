// unit_slots.cpp - restructure step 2a: the per-player records (core/02 PlayerSlot) behave like the old value swap.
// Checks: starting values for players 2 and 3; WithP3 / WithP2 (nested too) switch which record the old names reach; x_p3
// always reaches the other player; the per-player settings follow (step 2c: each record points at its own); nothing changes while player 3 is switched off.
// Build: x86_64-w64-mingw32-g++-posix -O1 -static -o t.exe unit_slots.cpp   Run: WINEDEBUG=-all wine t.exe
#define COOP_MOD_VERSION "test"
#include "../../core/00_includes_xinput_types.inc"
#include "../../core/02_state_globals_log.inc"
#include "../../settings/03_settings_file.inc"
static bool g_P3Enabled_set = (g_P3Enabled = true);
#include "../../seats/17_player3_swap.inc"
static void P3WinUpdate() {}
static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)
int main()
{
    g_P3Cfg.p2Party = 3; g_P3Cfg.joinAtStart = true; g_P2Cfg.rumble = 70; g_P3Cfg.rumble = 30;
    CHECK(g_Player2Slot.cfg == &g_P2Cfg && g_Player3Slot.cfg == &g_P3Cfg && g_Player->cfg == &g_P2Cfg);   // step 2c: each record points at its own settings
    // starting values
    CHECK(g_Player->num == 2 && g_Player->partySlot == 1 && g_Player->joined == true && g_Player->color[1] == 1.f && !g_Player->pipFresh && g_Player->charType == -1);
    CHECK(g_OtherPlayer->num == 3 && g_OtherPlayer->partySlot == 2 && g_OtherPlayer->joined == false && g_OtherPlayer->color[0] == 1.f && g_OtherPlayer->pipFresh);
    g_Player->stickX = 0.25f; g_Player->tgtIdx = 7; g_Player->req[5] = 0x55; g_Player->pipCamPos[2] = 9.f;
    {
        WithP3 w;   // first switch: player 3's start-up values from the settings (LoadP3's g_P3Init)
        CHECK(g_P3Loaded && g_Player->num == 3 && g_Player->partySlot == 2 && g_Player->joined == true && g_Player->cfg->rumble == 30);
        CHECK(g_Player->stickX == 0.f && g_Player->tgtIdx == -1 && g_Player->req[5] == 0 && g_Player->pipCamPos[2] == 0.f);
        CHECK(g_OtherPlayer->num == 2 && g_OtherPlayer->stickX == 0.25f && g_OtherPlayer->tgtIdx == 7 && g_OtherPlayer->req[5] == 0x55);
        g_Player->stickX = -0.5f; g_Player->tgtIdx = 3;
        {
            WithP2 b;   // nested
            CHECK(!g_P3Loaded && g_Player->num == 2 && g_Player->stickX == 0.25f && g_Player->cfg->rumble == 70 && g_OtherPlayer->stickX == -0.5f);
            g_Player->tgtIdx = 8;
        }
        CHECK(g_Player->num == 3 && g_Player->stickX == -0.5f && g_Player->tgtIdx == 3 && g_OtherPlayer->tgtIdx == 8);
        { WithP3 again; CHECK(g_Player->num == 3 && g_Player->tgtIdx == 3); }   // already loaded: no switch
        CHECK(g_Player->num == 3);
    }
    CHECK(!g_P3Loaded && g_Player->num == 2 && g_Player->tgtIdx == 8 && g_Player->stickX == 0.25f && g_Player->cfg->rumble == 70 && g_OtherPlayer->stickX == -0.5f);
    // references taken before a switch keep pointing at the record they were taken from (the one behaviour difference -
    // the audit found no code that keeps one across a switch)
    int& ref = g_Player->tgtIdx; { WithP3 w; CHECK(ref == 8 && g_Player->tgtIdx == 3); }
    // player 3 off: WithP3 does nothing
    g_P3Enabled = false; { WithP3 w; CHECK(!g_P3Loaded && g_Player->num == 2); } g_P3Enabled = true;
    // step 2d: the own-menu paging values (were arrays indexed by num & 3) start like the arrays did and stay per player
    CHECK(g_Player->aiPageType == -1 && g_Player->lastPageDir == 1 && g_Player->pageReq == 0 && !g_Player->menuEnding);
    CHECK(g_OtherPlayer->aiPageType == -1 && g_OtherPlayer->lastPageDir == 1 && g_OtherPlayer->ownHideCol[3] == 0.f);
    { WithP3 w; g_Player->aiPageType = 5; g_Player->guardWait = 90; int& h = g_Player->guardWait; { WithP2 b; CHECK(g_Player->aiPageType == -1 && g_Player->guardWait == 0); } CHECK(h == 90); }
    CHECK(g_Player->aiPageType == -1 && g_OtherPlayer->aiPageType == 5 && g_OtherPlayer->guardWait == 90);
    // step 2c: the settings follow the record (no copy) and the other player's are reachable without switching
    { WithP3 w; CHECK(g_Player->cfg == &g_P3Cfg && g_OtherPlayer->cfg->rumble == 70 && g_P2Cfg.rumble == 70 && g_P3Cfg.rumble == 30); }
    CHECK(g_Player->cfg == &g_P2Cfg && g_OtherPlayer->cfg == &g_P3Cfg);
    printf(fails ? "%d FAILED\n" : "ALL OK\n", fails);
    return fails ? 1 : 0;
}
