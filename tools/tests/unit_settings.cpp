// unit_settings.cpp - loads FF7RCoopMod.ini (next to the exe) through LoadConfig and prints every setting of
// g_Cfg (shared) + g_P2Cfg (player 2) and g_P3Cfg (player 3), so two builds can be compared line for line (f1.85: the condense build's
// proof - the author's ini, the default, odd values, player 3 off: same output before and after; the log too).
// Build: x86_64-w64-mingw32-g++-posix -O1 -static -o t.exe unit_settings.cpp   Run: copy t.exe next to a FF7RCoopMod.ini,
// WINEDEBUG=-all wine t.exe > dump.txt (no ini = the default one is written, like in the game).
#define COOP_MOD_VERSION "test"
#include "../../core/00_includes_xinput_types.inc"
#include "../../core/02_state_globals_log.inc"
#include "../../settings/03_settings_file.inc"
static void LoadP3(bool) {}
static void PF(const char* who, const char* n, float v) { uint32_t b; memcpy(&b, &v, 4); printf("%s.%s = %.6g (%08X)\n", who, n, v, b); }
static void PI(const char* who, const char* n, long long v) { printf("%s.%s = %lld\n", who, n, v); }
static void PW(const char* who, const char* n, const wchar_t* v) { printf("%s.%s = \"%ls\"\n", who, n, v); }
static void Dump(const char* who, const CoopConfig& c, const PlayerCfg& p)   // shared settings + one player's
{
    char nm[64];
    PI(who, "joinVk", p.joinVk);
    PI(who, "joinCtrl", p.joinCtrl);
    PI(who, "joinShift", p.joinShift);
    PI(who, "joinAlt", p.joinAlt);
    for (int i = 0; i < 3; ++i) { snprintf(nm, 64, "leadVk[%d]", i); PI(who, nm, c.leadVk[i]); }
    for (int i = 0; i < 3; ++i) { snprintf(nm, 64, "leadCtrl[%d]", i); PI(who, nm, c.leadCtrl[i]); }
    for (int i = 0; i < 3; ++i) { snprintf(nm, 64, "leadShift[%d]", i); PI(who, nm, c.leadShift[i]); }
    for (int i = 0; i < 3; ++i) { snprintf(nm, 64, "leadAlt[%d]", i); PI(who, nm, c.leadAlt[i]); }
    PI(who, "joinAtStart", p.joinAtStart);
    PI(who, "showMarker", c.showMarker);
    PI(who, "showStatus", c.showStatus);
    PI(who, "showOffscreen", c.showOffscreen);
    PI(who, "pipEnabled", c.pipEnabled);
    PI(who, "pipVk", c.pipVk);
    PI(who, "pipCtrl", c.pipCtrl);
    PI(who, "pipShift", c.pipShift);
    PI(who, "pipAlt", c.pipAlt);
    PI(who, "layVk", c.layVk);
    PI(who, "layCtrl", c.layCtrl);
    PI(who, "layShift", c.layShift);
    PI(who, "layAlt", c.layAlt);
    PI(who, "pipShow", c.pipShow);
    PI(who, "pipSide", c.pipSide);
    PF(who, "pipDist", p.pipDist);
    PF(who, "pipHeight", p.pipHeight);
    PF(who, "pipPitch", p.pipPitch);
    PF(who, "pipFov", p.pipFov);
    PF(who, "pipTurn", p.pipTurn);
    PI(who, "pipThirdsOn", c.pipThirdsOn);
    PI(who, "pipTallFov", c.pipTallFov);
    PF(who, "pipWallRadius", c.pipWallRadius);
    PI(who, "pipForAI", c.pipForAI);
    PI(who, "pipVignette", c.pipVignette);
    PF(who, "pipBright", c.pipBright);
#ifndef NO_HCSKIP
    PW(who, "hcSkip", c.hcSkip);
#endif
    PI(who, "aiGuardCopy", c.aiGuardCopy);
    PI(who, "pipCamControl", p.pipCamControl);
    PF(who, "pipCamSpeed", p.pipCamSpeed);
    PF(who, "pipFollowDelay", p.pipFollowDelay);
    PF(who, "pipDetail", c.pipDetail);
    PF(who, "pipViewDist", c.pipViewDist);
    PI(who, "pipSource", c.pipSource);
    PI(who, "pipFormat", c.pipFormat);
    PI(who, "pipFullPct", c.pipFullPct);
    PF(who, "statusRight", c.statusRight);
    PF(who, "statusTop", c.statusTop);
    PF(who, "statusSize", c.statusSize);
    PI(who, "camEnabled", c.camEnabled);
    PI(who, "camSplitLock", c.camSplitLock);
    PI(who, "camSplitLockKeep", c.camSplitLockKeep);
    PI(who, "camSplitLens", c.camSplitLens);
    PI(who, "camSplitLockZoom", c.camSplitLockZoom);
    PF(who, "balWeakChance", c.balWeakChance);
    PF(who, "balWeakHealth", c.balWeakHealth);
    PI(who, "camVk", c.camVk);
    PI(who, "camCtrl", c.camCtrl);
    PI(who, "camShift", c.camShift);
    PI(who, "camAlt", c.camAlt);
    PI(who, "reloadVk", c.reloadVk);
    PI(who, "helpVk", c.helpVk);
    PI(who, "showFps", c.showFps);
    PI(who, "showClock", c.showClock);
    PI(who, "gameEnemyNames", c.gameEnemyNames);
    PI(who, "keyboardKeepsLead", c.keyboardKeepsLead);
    PI(who, "smoothShapes", c.smoothShapes);
    PI(who, "ownMenuCloseAfterCmd", c.ownMenuCloseAfterCmd);
    PF(who, "menuSlow", c.menuSlow);
    for (int i = 0; i < 4; ++i) { snprintf(nm, 64, "menuSlowP[%d]", i); PF(who, nm, c.menuSlowP[i]); }
    PF(who, "camBias", c.camBias);
    PF(who, "camBaseZoom", c.camBaseZoom);
    PF(who, "camMaxDist", c.camMaxDist);
    PF(who, "camMargin", c.camMargin);
    PF(who, "camMaxPitch", c.camMaxPitch);
    PF(who, "camTiltStart", c.camTiltStart);
    PF(who, "camTiltFull", c.camTiltFull);
    PF(who, "camEnemyRadius", c.camEnemyRadius);
    PF(who, "camEnemySlack", c.camEnemySlack);
    PF(who, "camSmoothing", c.camSmoothing);
    PF(who, "camCutsceneDist", c.camCutsceneDist);
    PI(who, "camTalkScenes", c.camTalkScenes);
    PI(who, "camWallCheck", c.camWallCheck);
    PI(who, "camAutoOrient", c.camAutoOrient);
    PF(who, "camOrientSpeed", c.camOrientSpeed);
    PF(who, "balHP", c.balHP);
    PF(who, "balAttackRate", c.balAttackRate);
    PF(who, "balEnemyDamage", c.balEnemyDamage);
    PF(who, "balStagger", c.balStagger);
    PF(who, "balFocusP2", c.balFocusP2);
    PF(who, "stayOnHit", c.stayOnHit);
    PF(who, "turnHeavy", c.turnHeavy);
    for (int i = 0; i < 3; ++i)   // f1.151: the enemy settings per party size
    {
        const BalanceSet& b = c.balSet[i];
        snprintf(nm, 64, "balSet[%d].hp", i); PF(who, nm, b.hp);
        for (int p = 0; p < 3; ++p) { snprintf(nm, 64, "balSet[%d].rate[%d]", i, p); PF(who, nm, b.rate[p]); }
        snprintf(nm, 64, "balSet[%d].damage", i); PF(who, nm, b.damage);
        snprintf(nm, 64, "balSet[%d].attackRate", i); PF(who, nm, b.attackRate);
        snprintf(nm, 64, "balSet[%d].stagger", i); PF(who, nm, b.stagger);
        snprintf(nm, 64, "balSet[%d].focusP2", i); PF(who, nm, b.focusP2);
        snprintf(nm, 64, "balSet[%d].stayOnHit", i); PF(who, nm, b.stayOnHit);
        snprintf(nm, 64, "balSet[%d].turnHeavy", i); PF(who, nm, b.turnHeavy);
        snprintf(nm, 64, "balSet[%d].weakChance", i); PF(who, nm, b.weakChance);
        snprintf(nm, 64, "balSet[%d].weakHealth", i); PF(who, nm, b.weakHealth);
        snprintf(nm, 64, "balSet[%d].humansOnly", i); PI(who, nm, b.humansOnly);
    }
    PI(who, "humanRates", c.humanRates);
    for (int i = 0; i < 3; ++i) { snprintf(nm, 64, "ratePct[%d]", i); PF(who, nm, c.ratePct[i]); }
    PI(who, "humanAggro", c.humanAggro);
    PI(who, "humanAggroFile", c.humanAggroFile);   // restructure step 1 (f1.192): the two fields the dump had missed
    PI(who, "p1Controller", c.p1Controller);
    PI(who, "p2Party", p.p2Party);
    PI(who, "bJoin", p.bJoin);
    PI(who, "autoLeave", p.autoLeave);
    PF(who, "moveDeadzone", p.moveDeadzone);
    PI(who, "rumble", p.rumble);
    PI(who, "camInvertY", p.camInvertY);
    PI(who, "buttonIcons", p.buttonIcons);
    PI(who, "targetLog", c.targetLog);
    PI(who, "pipDyn", c.pipDyn);
    PI(who, "pipWallSolid", c.pipWallSolid);
    PI(who, "partyIndiv", c.partyIndiv);
    PI(who, "partyRows", c.partyRows);
    PI(who, "partyScale", c.partyScale);
    PI(who, "partyScale3", c.partyScale3);
    PI(who, "partyScale4", c.partyScale4);
    PI(who, "cmdScale", c.cmdScale);
    PI(who, "cmdScale3", c.cmdScale3);
    PI(who, "cmdScale4", c.cmdScale4);
    PI(who, "thirdsCmdNudge", c.thirdsCmdNudge);
    PI(who, "msgLogX", c.msgLogX); PI(who, "msgLogY", c.msgLogY);
    PI(who, "thirdsPanelNudge", c.thirdsPanelNudge);
    PI(who, "color", p.color);
    PF(who, "pipDynFps", c.pipDynFps);
    for (int k = 0; k < 4; ++k) { char nm[32]; snprintf(nm, 32, "pipDynOrder[%d]", k); PI(who, nm, c.pipDynOrder[k]); }
    PF(who, "pipDynMaxDetail", c.pipDynMaxDetail);
    PF(who, "pipDynMinDist", c.pipDynMinDist);
    PI(who, "pipDynMinSharp", c.pipDynMinSharp);
    PI(who, "menuDarken", c.menuDarken);
    PI(who, "bossTestAI", c.bossTestAI);
    PI(who, "enemyNow", c.enemyNow);
    PI(who, "bossNearest", c.bossNearest);
    for (int p = 1; p <= 3; ++p) { char n[32]; snprintf(n, sizeof(n), "menuDarkSplit[%d]", p); PI(who, n, c.menuDarkSplit[p]); }
    PI(who, "dangerPerScreen", c.dangerPerScreen);
    PF(who, "dangerHealth", c.dangerHealth);
    PF(who, "markerWinSize", c.markerWinSize);
    PI(who, "bAttack", p.bAttack);
    PI(who, "bDodge", p.bDodge);
    PI(who, "bGuard", p.bGuard);
    PI(who, "bAbility", p.bAbility);
    PI(who, "bMenu", p.bMenu);
    PI(who, "bLock", p.bLock);
    PI(who, "bMenuUp", p.bMenuUp);
    PI(who, "bMenuDown", p.bMenuDown);
    PI(who, "bMenuConfirm", p.bMenuConfirm);
    PI(who, "bMenuBack", p.bMenuBack);
    PI(who, "bMenuPrev", p.bMenuPrev);
    PI(who, "bMenuNext", p.bMenuNext);
    PI(who, "bShortcutHold", p.bShortcutHold);
    for (int i = 0; i < 4; ++i) { snprintf(nm, 64, "bShortcut[%d]", i); PI(who, nm, p.bShortcut[i]); }
    PI(who, "bAlly1", p.bAlly1);
    PI(who, "bAlly2", p.bAlly2);
    PI(who, "bSwitchPrev", p.bSwitchPrev);
    PI(who, "bSwitchNext", p.bSwitchNext);
}
int main()
{
    LoadConfig();
    Dump("p2", g_Cfg, g_P2Cfg);
    // (before restructure step 2c player 3's settings were a whole CoopConfig whose shared part was never used - the
    //  defaults + player 1's controller flag; printed the same way so old and new dumps compare line for line)
    CoopConfig s3; s3.p1Controller = g_Cfg.p1Controller;
    Dump("p3", s3, g_P3Cfg);
    printf("g_P3Enabled = %d\nP2_PARTY_SLOT = %d\ng_CoopOn = %d\ng_BalSetIdx = %d\n", (int)g_P3Enabled, g_Player->partySlot, (int)g_Player->joined, g_BalSetIdx);
    return 0;
}
