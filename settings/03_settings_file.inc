// =================================================================================
// SETTINGS FILE (FF7RCoopMod.ini, next to the game exe)
// =================================================================================
// Read once, the first time the game calls into this DLL. If the file doesn't exist, a commented
// default one is written. Restart the game after editing it.
// Pad inputs are kept as a 32-bit mask: the 16 XInput button bits, plus LT = 0x10000, RT = 0x20000.
static constexpr DWORD PAD_LT = 0x10000, PAD_RT = 0x20000;
#include "../input2/seats.h"   // (review #30): ParseWant - the one reading of [Input2] SeatN (SeatWant below)
static constexpr WORD VK_OFF = 0xFF;   // a hotkey set to None - never polled (some keyboards DO report 0xFF)
static bool HotkeyKeyHeld(UINT vk) { return vk && vk != VK_OFF && (GetAsyncKeyState((int)vk) & 0x8000); }
// a hotkey fires only with exactly its own Ctrl / Shift / Alt (like the lead and split-screen keys) -
// before, the join / camera / reload keys also fired with extra modifiers held (P3 JoinKey=Ctrl+F10 also toggled P2's F10)
// "is the game window in front" and Ctrl / Shift / Alt are read ONCE a
// frame, at the start of the main tick (core/27 KeySnapTake) - the hotkey checks used to ask Windows ~40 times a frame.
struct KeySnap { HWND fg; bool focus, ctrl, shift, alt; };
static KeySnap g_Keys = {};
static void KeySnapTake()
{
    HWND fg = GetForegroundWindow(); DWORD pid = 0;
    if (fg) GetWindowThreadProcessId(fg, &pid);
    g_Keys.fg = fg; g_Keys.focus = pid == GetCurrentProcessId();
    g_Keys.ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    g_Keys.shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    g_Keys.alt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
}
static bool HotkeyModsExact(bool c, bool s, bool a) { return g_Keys.ctrl == c && g_Keys.shift == s && g_Keys.alt == a; }
// a mod hotkey is down this frame: the game window in front, the key held, exactly its own Ctrl / Shift / Alt
// (finding #12 - the one check every hotkey uses; each caller keeps its own "just pressed" handling)
static bool HotkeyDown(UINT vk, bool ctrl, bool shift, bool alt) { return g_Keys.focus && HotkeyKeyHeld(vk) && HotkeyModsExact(ctrl, shift, alt); }
// the per-player settings - players 2 and 3 each have their own set (g_P2Cfg / g_P3Cfg, reached through the player's
// record: g_Player->cfg). They are listed only here: the F6 reload (core/26) copies a set whole, so a new per-player setting
// can't be forgotten there (before f1.85, each place listed the ~45 fields by hand).
// (the author: "could we make the reticles colors for P2 and P3 user defined?" ... "it can be a list of maybe 6-7
// colors, not a color wheel"): [PlayerN] Color = Default / one of these names. No Blue (the author: "disallow Blue
// as a color since that is always P1's color" - player 1's pointer; a file saying Blue = Default). The color is the player's color
// everywhere the mod draws it: their reticle + marker, their diamond badge, off-screen pointer, window frame, "P2" / "P3".
struct PlayerColor { const wchar_t* name; float c[3]; };
static const PlayerColor kPlayerColors[] = {
    {L"Green",  {0.30f, 1.00f, 0.45f}},   // player 2's default
    {L"Orange", {1.00f, 0.62f, 0.15f}},   // player 3's default
    {L"Yellow", {1.00f, 0.92f, 0.25f}},
    {L"Purple", {0.72f, 0.45f, 1.00f}},
    {L"Pink",   {1.00f, 0.45f, 0.80f}},
    {L"Red",    {1.00f, 0.30f, 0.28f}},
};
struct WindowCamCfg   // [Player2] / [Player3] Window* - ReadWindowCam starts from these defaults
{
    float pipDist = 260.f,   /* f1.332: 260 (the author's setting) */ pipHeight = 60.f, pipPitch = 20.f, pipFov = 80.f, pipTurn = 3.f;   // 320 / 12 -> 270 / 20 (the author's test)
    bool  pipCamControl = true; float pipCamSpeed = 150.f, pipFollowDelay = 3.f;   // right stick turns the window camera
};
struct PlayerCfg : WindowCamCfg
{
    UINT joinVk = VK_F10; bool joinCtrl = false, joinShift = false, joinAlt = false;   // [General] JoinKey / [Player3] JoinKey
    bool joinAtStart = true;   // (start-only: the F6 reload keeps the value from the start)
    // [Player2] / [Player3]
    int p2Party = 2;   // (p2Slot removed; fixed - player 2 = party member 2, player 3 = 3, PartySlot no longer read)
    // streaming-friendly controller handling (Parsec / Steam Remote Play guests)
    DWORD bJoin = 0x0010;         // hold this on player 2's controller to join / leave (Start)
    bool  autoLeave = true;       // controller gone for 3 s -> Barret back to the AI; back again -> rejoin
    float moveDeadzone = 0.24f;
    int   rumble = 100;          // [PlayerN] Rumble - % strength when their character is hit (0 = off)
    bool  camInvertY = false;   // [PlayerN] InvertY (the old CameraSpeed % is gone - WindowCameraSpeed)
    DWORD bAttack = 0x4000, bDodge = 0x2000, bGuard = 0x0200, bAbility = 0x8000, bMenu = 0x1000, bLock = 0x0080;
    DWORD bMenuUp = 0x0001, bMenuDown = 0x0002, bMenuConfirm = 0x1000, bMenuBack = 0x2000;
    DWORD bMenuPrev = 0x0100 | PAD_LT, bMenuNext = 0x0200 | PAD_RT;
    // shortcut palette - hold ShortcutHold, then press a Shortcut button
    DWORD bShortcutHold = 0x0100;                                   // LB
    DWORD bShortcut[4] = {0x4000, 0x8000, 0x2000, 0x1000};          // X, Y, B, A (slots 1-4 = the game's square / triangle / circle / cross)
    // PS4-style team controls
    DWORD bAlly1 = PAD_LT, bAlly2 = PAD_RT;                         // L2 / R2: open an AI teammate's command page
    DWORD bSwitchPrev = 0x0001, bSwitchNext = 0x0002;               // D-pad up / down: play a different (AI) teammate
    int   color = 0;         // [PlayerN] Color - 0 Default (player 2 green, player 3 orange), else 1 + kPlayerColors index
    int   buttonIcons = 0;   // [PlayerN] ButtonIcons - 0 Auto (PlayStation icons for a Sony controller, else Xbox; the default), 1 PlayStation, 2 Xbox, 3 Default (original game behavior - player 1's device's icons, nothing switched)
};
// (the author 2026-10-25: "delineate enemy settings under 3 drop down tabs... Single character party / Two character
// party / Three character party"): one set of enemy settings per party size - [Balance1] / [Balance2] / [Balance3]. Every
// default = the game's normal. The mod picks the set from the party size (enemies/21 PartyBalanceTick) and copies it into
// the active values below (balHP ... balWeakHealth) - never during a battle.
struct BalanceSet
{
    float hp = 1.f, damage = 1.f, attackRate = 1.f, stagger = 1.f, focusP2 = 1.f;   // EnemyHP / EnemyDamage / EnemyAttackRate / StaggerRate / FocusOnPlayer2 (fractions)
    float stayOnHit = 0.f, turnHeavy = 0.f;                                          // StayOnHitChance / TurnOnHeavyDamage (fractions, 0 = off)
    float weakChance = 0.f, weakHealth = 0.2f;                                       // PreferWeakTargets (Off 0 ... Extreme 1) / WeakTargetHealth
    bool  humansOnly = true;   // [Balance2] / [Balance3] OnlyWithHumanPlayers - used only while that many people play, else fewer
    // f1.272 (the author: "the menu slowdown and darkening should also be based on player 1, player 2, and player 3 settings"):
    // per party-size set now. A key missing from the set's section = the old shared key ([Balance] MenuSlowdown /
    // MenuSlowdownPlayer2 / 3, [General] MenuDarken / MenuDarkenSplitPlayer1 - 3), so an older ini keeps its values.
    float menuSlow[4] = {1.f, 1.f, 1.f, 1.f};   // [1] - [3] = players 1 - 3's command menu slowdown (fraction of the game's)
    bool  menuDarken = true;                    // one shared screen: the game's darkening while anyone's menu is open
    bool  menuDarkSplit[4] = {true, true, true, true};   // [1] - [3]: split screen, only that player's view darkens
    // f1.275 (the author: "Player's ATB speed should also be part of dynamic settings"): players 1 - 3's ATB fill rate per set
    // ([BalanceN] Player1Rates / Player2Rates / Player3Rates; missing = the shared [Balance] keys)
    float rate[3] = {0.9f, 0.9f, 0.9f};
};
struct CoopConfig   // the shared settings (one set, g_Cfg); each player's own settings are a PlayerCfg (g_P2Cfg / g_P3Cfg)
{
    // [General]
    UINT leadVk[3] = {VK_F9, VK_F9, VK_F9}; bool leadCtrl[3] = {false, true, false}, leadShift[3] = {false, false, true}, leadAlt[3] = {false, false, false};   // give the lead to player 1/2/3
    bool showMarker = true;
    // player status circles (top-right)
    bool showStatus = true;
    bool showOffscreen = true;   // edge arrow when player 2 is off screen
    // [PictureInPicture]
    bool  pipEnabled = false;
    UINT  pipVk = VK_F7; bool pipCtrl = false, pipShift = false, pipAlt = false;
    UINT  layVk = VK_F7; bool layCtrl = true, layShift = false, layAlt = false;   // LayoutKey
    int   pipShow = 2;       // 0 = always, 1 = only while player 2 is off screen, 2 = only in battle (default - the author)
    int   pipSide = 1;       // 0 = left, 1 = right (for Half/Third/Quarter); 2 = top, 3 = bottom (Half only); 4 = thirds
    int   msgLogX = 0, msgLogY = 0;   // [PictureInPicture] MessageLogX / MessageLogY - the battle messages (defeated / Gil / EXP) at the screen's right edge, then px at 1080p, + = right / down
    int   thirdsCmdNudge = 40, thirdsPanelNudge = 40;   // [PictureInPicture] ThirdsCommandNudge / ThirdsPanelNudge - Thirds only: px at 1080p left / right
    bool  pipThirdsOn = false;   // [PictureInPicture] ThirdsLayout - EXPERIMENTAL, off by default: Side=Thirds allowed
    bool  monSpanTest = false;   // [PictureInPicture] MultiMonitor (EXPERIMENTAL, off by default; f1.285 test name MonitorSpanTest still read): the split screen over every monitor
    int   monP1 = 0;             // [PictureInPicture] MonitorPlayer1 - 0 Main (Windows main monitor), 1 Left, 2 Middle, 3 Right
    int   monShare = 0;          // [PictureInPicture] MonitorShare - two monitors, players 2 + 3: 0 Columns (side by side), 1 Rows
    bool  monOneScreen = false;  // f1.311 [PictureInPicture] MultiMonitorAsOneScreen - test: the spanned monitors = one wide screen (thirds / halves of the whole)
    int   menuTiltFix = 1;       // [PictureInPicture] MenuTiltFix (not in the app): 1 = f1.320 tilted menus centred on where they're DRAWN (moved menus), 0 = game's own, 2 = f1.318 research hook
    bool  monNoPanini = true;    // f1.306 [PictureInPicture] MonitorNoPanini (research, not in the app): r.Upscale.Panini.D 0 while spanning
    int   monHudMap = 1;         // f1.300 [PictureInPicture] MonitorHudMap (research, not in the app): how the HUD picture is put onto player 1's monitor - 1 16:9 by width, 2 stretched, 0 off
    bool  monFade = true;        // f1.292 [PictureInPicture] MonitorFade - a quick fade to black over the one / all monitors switch (the author: "make a toggle option in settings")
    bool  pipForAI = false;      // [PictureInPicture] WindowsForAI - TESTING: the windows also for AI-played teammates (player 2 / 3 not in)
    // f1.213 (the author: "We need to figure out why the bosses are only targeting Player 1. It is imperative."):
    bool  bossNearest = false;   // [General] BossSpreadNearest - TESTING (f1.224): bosses that pick "the nearest party member" (Eligor) get the rotation too (f1.225: only a by-type pick whose mode can't be read)
    bool  enemyNow = false;      // f1.274 [General] EnemyChangesApplyNow - TESTING: enemy setting changes (F6 values, Same / Dynamic switch) apply mid-battle (the author: "put it in troubleshooting")
    bool  bossTestAI = false;    // [General] BossTargetsAITeammates - TESTING: AI-played teammates count as players for that (the author: "for the purpose of testing have the game treat those AI characters like humans")
    bool  partyIndiv = true;     // f1.330 default Individual (the author); [PictureInPicture] PartyPanel - Shared (the game's one panel) / Individual (each player's own, in their part of the screen)
    int   partyScale = 90;       // default 90 (was 100) - [PictureInPicture] PartyPanelScale - Individual panels' size in % (50 - 100)
    int   partyScale3 = 85;      // default 85 (was 90) - [PictureInPicture] PartyPanelScaleThird - the same in a third-of-the-screen view
    int   partyScale4 = 85;      // default 85 (was 75) - [PictureInPicture] PartyPanelScaleQuarter - the same in a quarter-of-the-screen view
    int   cmdScale = 90, cmdScale3 = 85, cmdScale4 = 85;   // defaults 90 / 85 / 85 (were 90 / 90 / 75) - [PictureInPicture] CommandMenuScale / ...Third / ...Quarter - command HUD / menus size per view (%)
    int   partyRows = 2;         // f1.331 default OwnAndAI (the author); f1.330 Own; [PictureInPicture] PartyPanelRows - 0 All / 1 Own (just that player's character) / 2 OwnAndAI (theirs + AI-played teammates) - Individual only
    bool  pipWallSolid = true;   // [PictureInPicture] WindowWallSolidObjects - the window cameras also stop at solid scenery ("BG" meshes like doors) on trace type 3
    float pipWallRadius = 20.f;  // [PictureInPicture] WindowWallRadius - the window cameras' wall check is a ball this big (cm); 0 = a thin line
    bool  pipTallFov = true;     // [PictureInPicture] TallWindowFov - windows narrower than 16:9 keep the 16:9 top-to-bottom angle
    bool  pipVignette = false;   // [PictureInPicture] WindowVignette - 0 = no vignette in the windows (the outer windows went dark)
    float pipBright = 0.f;       // [PictureInPicture] WindowBrightness - EV added to the windows' exposure (0 = the game's)
    wchar_t hcSkip[256] = L"EnemyStatus; BattleTarget; BattleText_Number; BattleText_ParryEffect";
    bool  aiGuardCopy = true;    // [General] AIGuardCopy: AI teammates guard when player 1 guards
    float pipDetail = 1.f, pipViewDist = 0.f;   // f1.330: Ultra by default (the author: "Make ultra the default")
    int   pipSource = 2, pipFormat = 2;   // SCS_FinalColorLDR, RTF_RGBA8
    // (the author: "a dynamic mode to the graphics settings for P2 and P3 ... if the frame rate dropped below a certain
    // threshold, it would automatically drop the graphics detail level in those windows"; "select which things it would
    // attempt to adjust and in what order"): [PictureInPicture] Dynamic* (pip_hud/30b)
    bool  pipDyn = false;        // DynamicQuality - on / off
    float pipDynFps = 55.f;      // DynamicTargetFps - the frame rate to keep (20 - 240)
    int   pipDynOrder[4] = {1, 2, 3, 4};   // DynamicOrder - what it lowers first: 1 Detail, 2 DrawDistance, 3 Sharpness, 4 HalfRate (0 = unused)
    float pipDynMaxDetail = 4.f; // DynamicLowestDetail - the plainest model detail it may use (Detail value: 1 / 1.5 / 2.5 / 4)
    float pipDynMinDist = 4000.f;// DynamicLowestDrawDistance - the shortest draw distance it may use (cm: 10000 / 6000 / 4000)
    int   pipDynMinSharp = 50;   // DynamicLowestSharpness - the softest picture it may use (% : 90 / 75 / 50)
    int   pipFullPct = 100;  // f1.330 Ultra; FullSizeHudQuality (% of the screen size) - FullSizeHud always on, Resolution / AvoidGameHud gone
    float statusRight = 12.f, statusTop = 12.f, statusSize = 0.5f;  // pixels at 1080p from the corner, size multiplier
    // [Camera] (framing camera)
    bool camEnabled = true;
    bool camSplitLock = true;        // [Camera] SplitAimFollowsLockOn - side-by-side split: while player 1 is locked on, centre what the game centres
    bool camSplitLockKeep = true;    // [Camera] SplitAimLockKeepsPlayer1 - ... but only as far as player 1 stays in their part of the screen
    bool camSplitLens = true;        // [Camera] SplitLens - player 1's split-screen view through an off-centre lens
    bool camSplitLockZoom = true;    // [Camera] SplitAimLockZoom - ... widening player 1's view first (up to +60%) so both fit
    float balWeakChance = 0.f;       // [Balance] PreferWeakTargets - chance an enemy picking a new target goes for a weakened player instead (Off 0 / Low .25 / Medium .5 / High .75 / Extreme 1)
    float balWeakHealth = 0.2f;      // [Balance] WeakTargetHealth - below this share of max health a player counts as weakened (default 20, the author)
    UINT camVk = VK_F8; bool camCtrl = false, camShift = false, camAlt = false;
    UINT reloadVk = VK_F6;       // [General] ReloadKey
    UINT helpVk = VK_F5;         // [General] HelpKey - controls overlay
    bool showFps = false;        // [General] ShowFPS
    bool showClock = false;      // [General] ShowClock - the PC's time (as in the log) in the top right corner, for testing
    bool gameEnemyNames = true;  // [General] GameEnemyNames - enemy names from the game (0 = learnt from player 1's target panel)
    bool keyboardKeepsLead = true;    // [General] KeyboardLeaderKeepsLead - no hot swap while the leader is on the keyboard (on by default - the author 2026-10-26)
    bool smoothShapes = true;    // [General] SmoothShapes - badges / pointers from smooth sprites (0 = old line drawing)
    float markerWinSize = 0.85f; // [General] MarkerSizeInWindows - players 2/3's reticle size inside the split-screen windows (%, 100 = player 1's size)
    bool dangerPerScreen = true; // [General] DangerPerScreen - with the split screen, each view shows its own player's near-death red edge (the game's whole-screen Dying filter off)
    float dangerHealth = 0.25f;  // [General] DangerHealth - at / below this share of max HP a character counts as near death (%)
    bool menuDarken = true;      // [General] MenuDarken - ONE shared screen (split screen not showing): the game darkens it while anyone's command menu is open (0 = it doesn't)
    // (the author, f1.208: "a separate filter for the co-op camera. So each character can darken their own screen on a split
    // screen scenario"): [General] MenuDarkenSplitPlayer1 / 2 / 3 - while the split screen shows, only THAT player's view
    // darkens while THEIR command menu is open (the game's whole-screen filter is never made then). [1] - [3] used.
    bool menuDarkSplit[4] = {true, true, true, true};
    bool targetLog = true;       // [General] TargetLog - who enemies go after + how much each player moves, in the log (file only)
    bool ownMenuCloseAfterCmd = true; // [General] OwnMenuCloseAfterCommand - after a command from players 2/3's own menu the game turns it to player 1's page and leaves it open - close it (a made Cancel)
    float menuSlow = 1.f;   // [Balance] MenuSlowdown (fraction of the game's slowdown)
    float menuSlowP[4] = {1.f, 1.f, 1.f, 1.f};   // [Balance] MenuSlowdownPlayer2 / 3 ([2] / [3]; missing = 100, MenuSlowdownOthers gone)
    float camBias = 0.4f;         // 0 = centred on player 1, 1 = centred on everything being framed; 0.6 -> 0.4 (the author: "keep the party leader in frame" - tested: "Yeah, this is better")
    float camBaseZoom = 800.f;    // BaseZoomOut - the closest the co-op camera gets to player 1 (cm; f1.200: measured from player 1, was "extra behind the game's camera")
    float camMaxDist = 3500.f;    // 2600 -> 3500; farthest the camera may sit from its aim point
    float camMargin = 0.60f;      // 0.7 -> 0.6 (the author's test: "seems decent"); 0.8 -> 0.7; fraction of the screen the framed things may fill (edges kept clear)
    float camMaxPitch = 35.f;     // 55 -> 35; steepest downward angle at full spread (degrees)
    float camTiltStart = 800.f;   // 500 -> 800; spread where the camera starts tilting down
    float camTiltFull = 3000.f;   // 2200 -> 3000; spread where it reaches MaxPitch
    float camEnemyRadius = 2500.f;// other enemies within this distance of player 1 may be framed too...
    float camEnemySlack = 0.25f;  // ...if that costs at most this much extra distance (0.25 = 25%)
    float camSmoothing = 3.0f;    // how quickly the camera eases to its new framing (higher = snappier)
    float camCutsceneDist = 3000.f; // game camera further than this from player 1 = not gameplay
    bool camTalkScenes = true;      // [Camera] TalkScenes - talking scenes / scripted moments (the game took the field controls) = game camera, no split screen
    bool  camWallCheck = true;    // move in front of walls/ceilings between the aim point and the camera
    bool  camAutoOrient = true;   // beyond MaxDistance: turn to look from behind player 1 toward player 2
    float camOrientSpeed = 60.f;  // degrees per second
    // [Balance1] / [Balance2] / [Balance3] - the enemy settings for a one / two / three character party
    BalanceSet balSet[4];   // f1.272: [3] = [BalanceGlobal] - used for every party size when [Balance] DynamicDifficulty=0
    bool dynamicDifficulty = true;   // f1.272 [Balance] DynamicDifficulty - 1 = the set follows how many people play (as before), 0 = always [BalanceGlobal]
    // The ACTIVE enemy values: a copy of balSet[g_BalSetIdx] (ApplyBalanceSet).
    // All 100 / 0 = the game's normal behaviour.
    float balHP = 1.f;        // enemy max HP multiplier
    float balAttackRate = 1.f;    // [Balance] EnemyAttackRate - how often enemies use abilities (fraction)
    float balEnemyDamage = 1.f;   // [Balance] EnemyDamage - damage the party takes (fraction)
    float balStagger = 1.f;   // how fast enemies' stagger gauge fills
    float balFocusP2 = 1.f;   // how much attention player 2's actions draw from enemies
    float stayOnHit = 0.f;         // chance a landed hit keeps the enemy on that player (hate scores; 0 = the game's normal)
    float turnHeavy = 0.f;         // [Balance] TurnOnHeavyDamage - share of the enemy's max HP that turns it on that player (0 = off)
    bool  humanRates = true;  // human-controlled party members get player 1's ATB/stagger rates (not the AI's)
    float ratePct[3] = {1.f, 1.f, 1.f};   // players 1 / 2 / 3 (people) - ATB fill rate, 1 = the game's normal speed for the character you control (0 = the AI party member's rules)
    bool  humanAggro = true;  // enemies treat human-controlled party members like player 1 (hate rates)
    bool  humanAggroFile = true;   // [Balance] EnemiesTreatHumansLikePlayer1 as read - humanAggro gets it with the enemy set (never mid-battle)
    // [Player1]
    bool p1Controller = false;   // [Input2] Seat1 isn't Keyboard / None (p1Slot removed)
    // ([Player2] / [Player3]: PlayerCfg above)
};
static CoopConfig g_Cfg;
// each player's own settings ([Player2] / [Player3], + player 2's join key / join-at-start in [General]). The code reads
// them through the player's record: g_Player->cfg->rumble = the current player's (core/02 PlayerSlot).
static PlayerCfg g_P2Cfg, g_P3Cfg;
static const bool g_CfgLinked = (g_Player2Slot.cfg = &g_P2Cfg, g_Player3Slot.cfg = &g_P3Cfg, true);
// which party-size set is active (0 = one character, 1 = two, 2 = three). Chosen by enemies/21 PartyBalanceTick
// (outside battles only); kept over an F6 reload (the reload re-copies that set's new values - F6 is the player's choice).
static int g_BalSetIdx = 0;
// (the author: changing enemy values mid-battle "could cause the game to crash or have an issue" - "disable that part
// of the settings update via F6 when a battle is currently going on"): enemies/21 PartyBalanceTick keeps g_BalBattle (a
// battle is on, incl. its 3 s end check); an F6 during it only stores the enemy values - they apply when it is over.
static bool g_BalBattle = false;
static bool g_BalReloadWaiting = false;   // an F6 in a battle changed enemy values - applied when the battle is over
static const char* BalSetName(int i)   // f1.272: for the log lines
{
    static const char* const n[4] = {"1-character", "2-character", "3-character", "global (any number of people)"};
    return n[i >= 0 && i <= 3 ? i : 0];
}
// the command-menu values of a set go live (f1.272); not enemy values - an F6 in a battle applies them at once
static void ApplyMenuSet(CoopConfig& c, int i)
{
    if (i < 0 || i > 3) i = 0;
    const BalanceSet& s = c.balSet[i];
    c.menuSlow = s.menuSlow[1];
    for (int p = 1; p <= 3; ++p) { c.menuSlowP[p] = s.menuSlow[p]; c.menuDarkSplit[p] = s.menuDarkSplit[p]; }
    c.menuDarken = s.menuDarken;
    for (int p = 0; p < 3; ++p) c.ratePct[p] = s.rate[p];   // f1.275: ATB per set - like the menus, a set change / F6 applies it at once
    c.humanRates = c.ratePct[1] > 0.f || c.ratePct[2] > 0.f;
}
static void ApplyBalanceSet(CoopConfig& c, int i)
{
    if (i < 0 || i > 3) i = 0;
    ApplyMenuSet(c, i);
    c.humanAggro = c.humanAggroFile;   // the one enemy setting in [Balance] (all party sizes) - same rule
    const BalanceSet& s = c.balSet[i];
    c.balHP = s.hp; c.balEnemyDamage = s.damage; c.balAttackRate = s.attackRate; c.balStagger = s.stagger; c.balFocusP2 = s.focusP2;
    c.stayOnHit = s.stayOnHit; c.turnHeavy = s.turnHeavy; c.balWeakChance = s.weakChance; c.balWeakHealth = s.weakHealth;
}
// (the author: "With one human player, it should be the Game's original default difficulty. With 2 or 3 human players,
// the default should be Co-op Balanced"): a missing key = these. One-character set = the game's normal; the two / three
// character sets = the settings app's Co-op Balanced preset (HP 125 / 150 %, stagger 90 / 85 %, damage 105 / 110 %, stay on
// hit 75 %). With OnlyWithHumanPlayers=1 (default) a party of AI teammates uses the one-character set = the normal game.
static BalanceSet BalanceSetDefault(int si)
{
    BalanceSet b;
    if (si == 1) { b.hp = 1.25f; b.stagger = 0.90f; b.damage = 1.05f; b.stayOnHit = 0.75f; }
    else if (si == 2) { b.hp = 1.50f; b.stagger = 0.85f; b.damage = 1.10f; b.stayOnHit = 0.75f; }
    return b;
}
// any active enemy value away from the game's normal (enemies/21: the solo enemy scan only runs then)
static bool BalanceActive(const CoopConfig& c)
{
    return c.balHP != 1.f || c.balEnemyDamage != 1.f || c.balAttackRate != 1.f || c.balStagger != 1.f || c.balFocusP2 != 1.f ||
           c.stayOnHit > 0.f || c.turnHeavy > 0.f || c.balWeakChance > 0.f;
}

static DWORD PadBits(const XINPUT_GAMEPAD& g)
{
    DWORD b = g.wButtons;
    if (g.bLeftTrigger > 100) b |= PAD_LT;
    if (g.bRightTrigger > 100) b |= PAD_RT;
    return b;
}

static void TrimW(wchar_t* t)
{
    wchar_t* a = t; while (*a == L' ' || *a == L'\t') ++a;
    if (a != t) memmove(t, a, (wcslen(a) + 1) * sizeof(wchar_t));
    size_t n = wcslen(t); while (n && (t[n-1] == L' ' || t[n-1] == L'\t' || t[n-1] == L'\r')) t[--n] = 0;
    // a tab before a "; note" is cut too (before, only spaces were - some readers trimmed a second time, others didn't,
    // so "Seat3=None<tab>; note" was read as Auto and "JoinAtStart=Auto<tab>; note" as 0)
    wchar_t* c = wcschr(t, L';'); if (c) { *c = 0; n = wcslen(t); while (n && (t[n-1] == L' ' || t[n-1] == L'\t')) t[--n] = 0; }
}

static DWORD PadNameToBit(const wchar_t* n)
{
    struct { const wchar_t* name; DWORD bit; } tbl[] = {
        {L"DPadUp",0x0001},{L"DPadDown",0x0002},{L"DPadLeft",0x0004},{L"DPadRight",0x0008},
        {L"Start",0x0010},{L"Back",0x0020},{L"L3",0x0040},{L"R3",0x0080},{L"LB",0x0100},{L"RB",0x0200},
        {L"A",0x1000},{L"B",0x2000},{L"X",0x4000},{L"Y",0x8000},{L"LT",PAD_LT},{L"RT",PAD_RT},{L"None",0} };
    for (auto& e : tbl) if (_wcsicmp(n, e.name) == 0) return e.bit;
    return 0xFFFFFFFF;
}
// "LB, LT" -> mask. Returns false if any name is unknown.
static bool ParsePad(const wchar_t* text, DWORD* out)
{
    wchar_t buf[128]; wcsncpy_s(buf, text, _TRUNCATE);
    DWORD mask = 0; wchar_t* ctx = nullptr;
    for (wchar_t* tok = wcstok_s(buf, L",+", &ctx); tok; tok = wcstok_s(nullptr, L",+", &ctx))
    {
        TrimW(tok); if (!*tok) continue;
        DWORD b = PadNameToBit(tok); if (b == 0xFFFFFFFF) return false;
        mask |= b;
    }
    *out = mask; return true;
}
static WORD KeyNameToVk(const wchar_t* n)
{
    if (!_wcsicmp(n, L"None") || !_wcsicmp(n, L"Off")) return VK_OFF;
    struct { const wchar_t* name; WORD vk; } tbl[] = {
        {L"Up",VK_UP},{L"Down",VK_DOWN},{L"Left",VK_LEFT},{L"Right",VK_RIGHT},{L"Enter",VK_RETURN},{L"Return",VK_RETURN},
        {L"Escape",VK_ESCAPE},{L"Esc",VK_ESCAPE},{L"Space",VK_SPACE},{L"Tab",VK_TAB},{L"Backspace",VK_BACK},
        {L"Shift",VK_SHIFT},{L"Ctrl",VK_CONTROL},{L"Control",VK_CONTROL},{L"Alt",VK_MENU},
        {L"Insert",VK_INSERT},{L"Delete",VK_DELETE},{L"Home",VK_HOME},{L"End",VK_END},{L"PageUp",VK_PRIOR},{L"PageDown",VK_NEXT} };
    for (auto& e : tbl) if (_wcsicmp(n, e.name) == 0) return e.vk;
    size_t len = wcslen(n);
    if (len == 1 && ((n[0] >= L'A' && n[0] <= L'Z') || (n[0] >= L'0' && n[0] <= L'9'))) return (WORD)n[0];
    if (len == 1 && n[0] >= L'a' && n[0] <= L'z') return (WORD)(n[0] - 32);
    if ((n[0] == L'F' || n[0] == L'f') && len >= 2 && len <= 3) { int f = _wtoi(n + 1); if (f >= 1 && f <= 24) return (WORD)(VK_F1 + f - 1); }
    if (_wcsnicmp(n, L"Num", 3) == 0 && len == 4 && n[3] >= L'0' && n[3] <= L'9') return (WORD)(VK_NUMPAD0 + (n[3] - L'0'));
    return 0;
}

#include "03a_default_ini.inc"   // DEFAULT_INI (restructure step 1, its own file)

static wchar_t g_IniPath[MAX_PATH];
static void IniStr(const wchar_t* sec, const wchar_t* key, wchar_t* out, DWORD n)
{
    out[0] = 0;
    GetPrivateProfileStringW(sec, key, L"", out, n, g_IniPath);
    TrimW(out);
}
static void IniPad(const wchar_t* sec, const wchar_t* key, DWORD* v)
{
    wchar_t t[128]; IniStr(sec, key, t, 128);
    if (!*t) return;
    DWORD m; if (ParsePad(t, &m)) *v = m;
    else CoopLog("SETTINGS: [%ls] %ls=%ls - unknown button name, keeping the default", sec, key, t);
}
static int IniInt(const wchar_t* sec, const wchar_t* key, int def)
{
    wchar_t t[32]; IniStr(sec, key, t, 32);
    return *t ? _wtoi(t) : def;
}
static int ClampI(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
// a number with an allowed range (was written out 5 times). Blank / missing = *v kept, false. Outside
// lo..hi = logged, *v kept, false. scale: the number in the file is divided by it ([Balance]: percent -> fraction, the
// log line shows percent).
static bool IniFloat(const wchar_t* sec, const wchar_t* key, float* v, float lo, float hi, float scale = 1.f)
{
    wchar_t ft[64]; IniStr(sec, key, ft, 64);
    if (!*ft) return false;
    float f = (float)_wtof(ft);
    if (f >= lo && f <= hi) { *v = f / scale; return true; }
    if (scale == 1.f) CoopLog("SETTINGS: [%ls] %ls=%ls out of range (%.2f-%.2f), keeping %.2f", sec, key, ft, lo, hi, *v);
    else CoopLog("SETTINGS: [%ls] %ls=%ls out of range (%.0f-%.0f), keeping %.0f", sec, key, ft, lo, hi, *v * scale);
    return false;
}
// a "Ctrl+Shift+F9" key (was written out 6 times). Blank / missing = the old values kept. noneOff: "None"
// = 0 (no key) - the other keys read None as VK_OFF (never polled either). False = there's text but no key name it knows
// (the old values are kept; the caller decides whether to log it).
static bool IniHotkey(const wchar_t* sec, const wchar_t* key, UINT* vk, bool* ctrl, bool* shift, bool* alt, bool noneOff = false)
{
    wchar_t t[128]; IniStr(sec, key, t, 128);
    if (!*t) return true;
    if (noneOff && !_wcsicmp(t, L"None")) { *vk = 0; return true; }
    bool c = false, s = false, a = false; WORD v = 0; wchar_t* ctx = nullptr;
    for (wchar_t* tok = wcstok_s(t, L"+", &ctx); tok; tok = wcstok_s(nullptr, L"+", &ctx))
    {
        TrimW(tok);
        if (!_wcsicmp(tok, L"Ctrl") || !_wcsicmp(tok, L"Control")) c = true;
        else if (!_wcsicmp(tok, L"Shift")) s = true;
        else if (!_wcsicmp(tok, L"Alt")) a = true;
        else v = KeyNameToVk(tok);
    }
    if (!v) return false;
    *vk = v; *ctrl = c; *shift = s; *alt = a;
    return true;
}
// [Input2] SeatN as written, without a "; note" after it (IniStr cuts it). "" (not set) = Auto - the old
// [PlayerN] keys (ControllerSlot / DirectInputController / Device / DirectInputA..RightY) are no longer read
static void SeatSetting(int num, wchar_t* out, DWORD n)
{
    wchar_t k[8]; swprintf(k, 8, L"Seat%d", num);
    IniStr(L"Input2", k, out, n);
}
// (review #30): THE reading of [Input2] SeatN - one parser (input2/seats.h ParseWant) for the settings (who has a
// controller, player 3 on / off) and the seats module (input/40b In2Start); before, the settings had their own copy and
// Seat3 was read twice. text = the setting as written (UTF-8, "" when blank). Blank = Auto; a setting that isn't
// understood = Auto too (*understood = false, In2Start logs it).
static in2::seats::Want SeatWant(int num, char* text, int n, bool* understood = nullptr)
{
    wchar_t v[220]; SeatSetting(num, v, 220);
    if (!WideCharToMultiByte(CP_UTF8, 0, v, -1, text, n - 1, nullptr, nullptr)) text[0] = 0;
    text[n - 1] = 0;
    in2::seats::Want w;
    bool ok = in2::seats::ParseWant(text[0] ? text : "Auto", &w);
    if (!ok) in2::seats::ParseWant("Auto", &w);
    if (understood) *understood = ok;
    return w;
}
// how this player's controller is picked: 0 = none (keyboard / nobody / off), 1 = Auto, 2 = a named controller
static int SeatPickKind(int num)
{
    char t[220]; in2::seats::Want w = SeatWant(num, t, sizeof(t));
    return w.kind == in2::seats::SEAT_AUTO ? 1 : w.kind == in2::seats::SEAT_DEVICE ? 2 : 0;
}
// JoinAtStart = 1 (in at the start), 0 (join by hand), Auto / blank (in at the start only when the controller
// was picked in the settings - an Auto-picked one waits for its player to hold JoinButton / press JoinKey, so a spare
// pad lying around never takes a character from a solo player)
static bool ReadJoinAtStart(const wchar_t* sec, int num)
{
    wchar_t v[32]; IniStr(sec, L"JoinAtStart", v, 32);
    // Auto (the default, also a missing key or 1) = in; 0 = out until they join. (The author 2026-10-26: "Auto" = with a
    // controller connected they start joined; without one the AI plays until it connects, then they join - PollP2Connection.
    // Before: Auto meant "in only with a controller picked by name / model".)
    bool isAuto = !v[0] || !_wcsicmp(v, L"Auto");
    bool r = isAuto || _wtoi(v) != 0;
    CoopLog("SETTINGS: player %d %s when the game starts (JoinAtStart=%ls%s)", num, r ? "is already in" : "is out until they join",
        v[0] ? v : L"Auto (default)", "");
    return r;
}

// [Player2] / [Player3] controller settings (same keys for both)
static void ReadPlayerPad(PlayerCfg& c, const wchar_t* sec, int num)
{
    wchar_t t[128];   // (which controller = [Input2] SeatN, input/40b)
    IniPad(sec, L"JoinButton", &c.bJoin);
    if (IniInt(sec, L"JoinWithController", 1) == 0) c.bJoin = 0;   // controller join switched off
    c.autoLeave = IniInt(sec, L"AutoLeaveOnDisconnect", 1) != 0;
    CoopLog("SETTINGS: player %d join button 0x%05lX held %.1fs, auto-leave on disconnect %d", num, c.bJoin, JOIN_HOLD_SECONDS, (int)c.autoLeave);
    c.p2Party = num;   // player 2 = party member 2, player 3 = 3 (PartySlot no longer read)
    IniStr(sec, L"MoveDeadzone", t, 128);
    if (*t) { float d = (float)_wtof(t); if (d >= 0.f && d <= 0.9f) c.moveDeadzone = d; }
    c.rumble = ClampI(IniInt(sec, L"Rumble", 100), 0, 200);
    c.camInvertY = IniInt(sec, L"InvertY", 0) != 0;
    IniPad(sec, L"Attack", &c.bAttack);
    IniPad(sec, L"Dodge", &c.bDodge);
    IniPad(sec, L"Guard", &c.bGuard);
    IniPad(sec, L"Ability", &c.bAbility);
    IniPad(sec, L"CommandMenu", &c.bMenu);
    IniPad(sec, L"LockOn", &c.bLock);
    IniPad(sec, L"MenuUp", &c.bMenuUp);
    IniPad(sec, L"MenuDown", &c.bMenuDown);
    IniPad(sec, L"MenuConfirm", &c.bMenuConfirm);
    IniPad(sec, L"MenuBack", &c.bMenuBack);
    IniPad(sec, L"MenuPreviousCharacter", &c.bMenuPrev);
    IniPad(sec, L"MenuNextCharacter", &c.bMenuNext);
    IniPad(sec, L"ShortcutHold", &c.bShortcutHold);
    IniPad(sec, L"Shortcut1", &c.bShortcut[0]);
    IniPad(sec, L"Shortcut2", &c.bShortcut[1]);
    IniPad(sec, L"Shortcut3", &c.bShortcut[2]);
    IniPad(sec, L"Shortcut4", &c.bShortcut[3]);
    IniPad(sec, L"AllyCommand1", &c.bAlly1);
    IniPad(sec, L"AllyCommand2", &c.bAlly2);
    IniPad(sec, L"SwitchCharacterPrevious", &c.bSwitchPrev);
    IniPad(sec, L"SwitchCharacterNext", &c.bSwitchNext);
    {   // [PlayerN] ButtonIcons
        wchar_t bi[32]; IniStr(sec, L"ButtonIcons", bi, 32);
        c.buttonIcons = !_wcsicmp(bi, L"PlayStation") ? 1 : !_wcsicmp(bi, L"Xbox") ? 2 : !_wcsnicmp(bi, L"Default", 7) ? 3 : 0;   // "Default..." (the app writes "Default (original game behavior)") = 3; anything else = Auto
    }
    {   // [PlayerN] Color
        wchar_t cn[32]; IniStr(sec, L"Color", cn, 32); c.color = 0;
        for (int i = 0; i < (int)(sizeof(kPlayerColors) / sizeof(kPlayerColors[0])); ++i) if (!_wcsicmp(cn, kPlayerColors[i].name)) c.color = i + 1;
        CoopLog("SETTINGS: player %d's color %ls", num, c.color ? kPlayerColors[c.color - 1].name : (num == 3 ? L"Default (orange)" : L"Default (green)"));
    }

}

// [Player2] / [Player3] Window* - each player's own window camera (settings app Split screen tab). A missing key = the
// built-in default (the shared [PictureInPicture] View / Distance / ... keys and the [PlayerN] CameraSpeed % are no longer
// read). These fields are per player (WindowCamCfg, part of PlayerCfg).
static void ReadWindowCam(PlayerCfg& d, const wchar_t* sec, int num)
{
    static_cast<WindowCamCfg&>(d) = WindowCamCfg();   // the defaults
    int n = 0;
    auto PB = [&](const wchar_t* key, bool* v) { wchar_t b[16]; IniStr(sec, key, b, 16); if (*b) { *v = _wtoi(b) != 0; ++n; } };
    auto PF = [&](const wchar_t* key, float* v, float lo, float hi) { if (IniFloat(sec, key, v, lo, hi)) ++n; };
    PB(L"WindowCameraControl", &d.pipCamControl);
    float spd = -1.f; PF(L"WindowCameraSpeed", &spd, 20.f, 600.f);
    if (spd > 0.f) d.pipCamSpeed = spd;
    PF(L"WindowAutoFollowDelay", &d.pipFollowDelay, 0.f, 60.f);
    PF(L"WindowDistance", &d.pipDist, 50.f, 3000.f);
    PF(L"WindowHeight", &d.pipHeight, -100.f, 500.f);
    PF(L"WindowPitch", &d.pipPitch, -30.f, 80.f);
    PF(L"WindowFOV", &d.pipFov, 40.f, 120.f);
    PF(L"WindowTurnSpeed", &d.pipTurn, 0.2f, 30.f);
    CoopLog("SETTINGS: player %d's window camera - behind them, distance %.0f height %.0f pitch %.0f fov %.0f turn %.1f, right stick %d @ %.0f deg/s, follow after %.1f s (%d Window* key(s) in the file, the rest default)",
        num, d.pipDist, d.pipHeight, d.pipPitch, d.pipFov, d.pipTurn,
        (int)d.pipCamControl, d.pipCamSpeed, d.pipFollowDelay, n);
}
// LoadConfig = one reader per part of the file, called in this order (the order matters for the log lines and for [General] JoinKey / JoinAtStart -> joined).
static void IniFindOrWriteDefault()   // g_IniPath = FF7RCoopMod.ini next to the game exe; written from DEFAULT_INI if missing
{
    GetModuleFileNameW(nullptr, g_IniPath, MAX_PATH);
    wchar_t* slash = wcsrchr(g_IniPath, L'\\'); if (slash) slash[1] = 0;
    wcscat_s(g_IniPath, L"FF7RCoopMod.ini");
    if (GetFileAttributesW(g_IniPath) == INVALID_FILE_ATTRIBUTES)
    {
        FILE* f = nullptr;
        if (_wfopen_s(&f, g_IniPath, L"wb") == 0 && f) { fwrite(DEFAULT_INI, 1, strlen(DEFAULT_INI), f); fclose(f); }
        CoopLog("SETTINGS: FF7RCoopMod.ini not found - wrote the default one");
    }
}

// [General]: join key, reload / help keys, on-screen options, the detailed log, menus
static void ReadGeneral(CoopConfig& c, PlayerCfg& p2)
{
    if (!IniHotkey(L"General", L"JoinKey", &p2.joinVk, &p2.joinCtrl, &p2.joinShift, &p2.joinAlt))
        CoopLog("SETTINGS: [General] JoinKey - couldn't read the key, keeping F10");
    p2.joinAtStart = ReadJoinAtStart(L"General", 2);   // 1 / 0 / Auto (player 2's is in [General])
    {
        // these two keys take no Ctrl / Shift / Alt - "Ctrl+F6" was read as one unknown name and silently
        // ignored (the settings app could record it). Now the key after the last '+' is used and the log says so.
        auto plainKey = [](const wchar_t* name, wchar_t* v) -> const wchar_t* {
            const wchar_t* k = wcsrchr(v, L'+');
            if (!k) return v;
            while (k[1] == L' ' || k[1] == L'\t') ++k;   // "Ctrl + F6"
            CoopLog("SETTINGS: NOTE - [General] %ls=%ls: this key works without Ctrl / Shift / Alt - using %ls", name, v, k + 1);
            return k + 1; };
        wchar_t rk[64]; IniStr(L"General", L"ReloadKey", rk, 64);
        if (*rk) { WORD vk = KeyNameToVk(plainKey(L"ReloadKey", rk)); if (vk) c.reloadVk = vk; else CoopLog("SETTINGS: [General] ReloadKey=%ls - unknown key name, keeping the default", rk); }
        wchar_t hk[64]; IniStr(L"General", L"HelpKey", hk, 64);
        if (*hk) { if (!_wcsicmp(hk, L"None")) c.helpVk = 0; else { WORD vk = KeyNameToVk(plainKey(L"HelpKey", hk)); if (vk) c.helpVk = vk; else CoopLog("SETTINGS: [General] HelpKey=%ls - unknown key name, keeping the default", hk); } }
        c.showFps = IniInt(L"General", L"ShowFPS", 0) != 0;
        c.showClock = IniInt(L"General", L"ShowClock", 0) != 0;   // (the author: "make a clock active that I can see")
        CoopLog("SETTINGS: detailed log ON - every event line is written (always on since f1.200; a DetailedLog line in the file is ignored)");
        c.smoothShapes = IniInt(L"General", L"SmoothShapes", 1) != 0;
        c.gameEnemyNames = IniInt(L"General", L"GameEnemyNames", 1) != 0;
        c.targetLog = IniInt(L"General", L"TargetLog", 1) != 0;   // (the author: keep checking who enemies attack)
        c.menuDarken = IniInt(L"General", L"MenuDarken", 1) != 0;   // (the author)
        c.dangerPerScreen = IniInt(L"General", L"DangerPerScreen", 1) != 0;   // (the author)
        c.dangerHealth = ClampI(IniInt(L"General", L"DangerHealth", 25), 1, 99) / 100.f;
        CoopLog("SETTINGS: near-death red edge %s (DangerPerScreen), at or below %.0f%% health (DangerHealth)", c.dangerPerScreen ? "per split-screen view" : "the game's own (whole screen)", c.dangerHealth * 100.f);
        for (int p = 1; p <= 3; ++p)
        {
            static const wchar_t* const kSplitDark[4] = {nullptr, L"MenuDarkenSplitPlayer1", L"MenuDarkenSplitPlayer2", L"MenuDarkenSplitPlayer3"};
            c.menuDarkSplit[p] = IniInt(L"General", kSplitDark[p], 1) != 0;
        }
        CoopLog("SETTINGS: command menu darkening - shared screen %s (MenuDarken); split screen, own view only: player 1 %s, player 2 %s, player 3 %s (MenuDarkenSplitPlayerN)",
            c.menuDarken ? "on" : "off", c.menuDarkSplit[1] ? "on" : "off", c.menuDarkSplit[2] ? "on" : "off", c.menuDarkSplit[3] ? "on" : "off");
        c.markerWinSize = ClampI(IniInt(L"General", L"MarkerSizeInWindows", 85), 50, 150) / 100.f;   // (the author: "a little bit bigger than the original")
        c.keyboardKeepsLead = IniInt(L"General", L"KeyboardLeaderKeepsLead", 1) != 0;   // on by default (a missing key = on)
        CoopLog("SETTINGS: hot swap while the leader is on the keyboard %s (KeyboardLeaderKeepsLead)", c.keyboardKeepsLead ? "blocked" : "allowed");
        {
            c.ownMenuCloseAfterCmd = IniInt(L"General", L"OwnMenuCloseAfterCommand", 1) != 0;
            CoopLog("SETTINGS: own menus close after a command %s (OwnMenuCloseAfterCommand)", c.ownMenuCloseAfterCmd ? "on" : "off");
            CoopLog("SETTINGS: own menus - players 2/3's marker on the target they are picking, player 1's reticle hidden over the windows (always on since f1.46)");
            // always on - paging to AI teammates (LT / RT), L2 / R2 AI teammate commands, player 1's menu in player 1's part,
            //        queued items count as used, own menus along the bottom without windows (440 px apart at 1080p)
        }
        CoopLog("SETTINGS: players 2/3 get their own command systems and their own command menus (always since f1.59)");
    }
}

// [General]: the hot-swap lead keys, markers / badges / status circles
static void ReadLeadAndMarkers(CoopConfig& c)
{
    {   // one key per person: that person takes the lead (trades with whoever leads now)
        static const wchar_t* lk[3] = {L"LeadKeyPlayer1", L"LeadKeyPlayer2", L"LeadKeyPlayer3"};
        for (int i = 0; i < 3; ++i)
            if (!IniHotkey(L"General", lk[i], &c.leadVk[i], &c.leadCtrl[i], &c.leadShift[i], &c.leadAlt[i], true))
                CoopLog("SETTINGS: [General] %ls - couldn't read the key", lk[i]);
    }
    c.showMarker  = IniInt(L"General", L"ShowTargetMarker", 1) != 0;
    c.showStatus  = IniInt(L"General", L"ShowStatusCircles", 1) != 0;
    c.showOffscreen = IniInt(L"General", L"ShowOffscreenArrow", 1) != 0;
    c.aiGuardCopy = IniInt(L"General", L"AIGuardCopy", 1) != 0;
    // always on - every controller reaches the game through the mod (XInput slot 0); off would let the game read
    // players 2/3's DirectInput / PlayStation controllers as player 1's (approved 2026-10-19)
    CoopLog("SETTINGS: DirectInput / PlayStation controllers neutral for the game's own readers (always since f1.22)");
    IniFloat(L"General", L"StatusCirclesRight", &c.statusRight, 0.f, 1800.f);
    IniFloat(L"General", L"StatusCirclesTop", &c.statusTop, 0.f, 1000.f);
    IniFloat(L"General", L"StatusCirclesSize", &c.statusSize, 0.3f, 4.f);
}

// [Camera]: the co-op camera
static void ReadCamera(CoopConfig& c)
{
    // [Camera]
    c.camEnabled = IniInt(L"Camera", L"Enabled", 1) != 0;
    c.camSplitLock = IniInt(L"Camera", L"SplitAimFollowsLockOn", 1) != 0;
    c.camSplitLockKeep = IniInt(L"Camera", L"SplitAimLockKeepsPlayer1", 1) != 0;
    c.camSplitLockZoom = IniInt(L"Camera", L"SplitAimLockZoom", 1) != 0;
    c.camSplitLens = IniInt(L"Camera", L"SplitLens", 1) != 0;
    IniHotkey(L"Camera", L"ToggleKey", &c.camVk, &c.camCtrl, &c.camShift, &c.camAlt);
    auto IniF = [&](const wchar_t* key, float* v, float lo, float hi) { IniFloat(L"Camera", key, v, lo, hi); };
    IniF(L"Bias", &c.camBias, 0.f, 1.f);
    IniF(L"BaseZoomOut", &c.camBaseZoom, 100.f, 3000.f);
    IniF(L"MaxDistance", &c.camMaxDist, 300.f, 20000.f);
    IniF(L"ScreenMargin", &c.camMargin, 0.2f, 1.f);
    IniF(L"MaxTilt", &c.camMaxPitch, 0.f, 89.f);
    IniF(L"TiltStartSpread", &c.camTiltStart, 0.f, 20000.f);
    IniF(L"TiltFullSpread", &c.camTiltFull, 1.f, 30000.f);
    IniF(L"EnemyRadius", &c.camEnemyRadius, 0.f, 20000.f);
    IniF(L"EnemyExtraAllowance", &c.camEnemySlack, 0.f, 5.f);
    IniF(L"Smoothing", &c.camSmoothing, 0.1f, 50.f);
    c.camAutoOrient = IniInt(L"Camera", L"AutoOrient", 1) != 0;
    c.camWallCheck = IniInt(L"Camera", L"WallCheck", 1) != 0;
    IniF(L"CutsceneDistance", &c.camCutsceneDist, 500.f, 100000.f);
    c.camTalkScenes = IniInt(L"Camera", L"TalkScenes", 1) != 0;   // (the author: "it should cut back to normal camera for the \"cinematic\" shots")
    IniF(L"AutoOrientSpeed", &c.camOrientSpeed, 5.f, 720.f);
    if (c.camTiltFull <= c.camTiltStart) c.camTiltFull = c.camTiltStart + 1.f;
    CoopLog("SETTINGS: camera %s, toggle VK 0x%02X, bias %.2f, base zoom %.0f, max dist %.0f, margin %.2f, tilt up to %.0f deg over spread %.0f-%.0f, enemies within %.0f if +%.0f%%, smoothing %.1f, auto-orient %d @ %.0f deg/s",
        c.camEnabled ? "ON" : "OFF", c.camVk, c.camBias, c.camBaseZoom, c.camMaxDist, c.camMargin, c.camMaxPitch, c.camTiltStart, c.camTiltFull,
        c.camEnemyRadius, c.camEnemySlack * 100.f, c.camSmoothing, (int)c.camAutoOrient, c.camOrientSpeed);
}

// [Balance] + [Balance1] / [Balance2] / [Balance3]: ATB, menu slowdown, the enemy settings per party size
static void ReadBalance(CoopConfig& c)
{
    // [Balance]
    {
        c.humanRates = IniInt(L"Balance", L"PlayerRulesForHumans", 1) != 0;
        {   // one percentage per human player (old on/off = 100 / 0 when the new keys are missing)
            static const wchar_t* rk[3] = {L"Player1Rates", L"Player2Rates", L"Player3Rates"};
            for (int i = 0; i < 3; ++i)
            {
                int v = ClampI(IniInt(L"Balance", rk[i], (i == 0 || c.humanRates) ? 90 : 0), 0, 100);   // default 90 = Co-op Balanced (player 1's only counts while someone else plays)   // never above 100 (the author 2026-10-20: "Let's not allow ATB over 100")
                c.ratePct[i] = v / 100.f;
            }
            c.humanRates = c.ratePct[1] > 0.f || c.ratePct[2] > 0.f;
            CoopLog("SETTINGS: ATB rate (shared fallback - a set without its own Player1Rates / 2 / 3 uses these) player 1 %.0f%%, player 2 %.0f%%, player 3 %.0f%% of the game's normal speed (0 = the AI's rules)", c.ratePct[0] * 100.f, c.ratePct[1] * 100.f, c.ratePct[2] * 100.f);
        }
        c.humanAggroFile = IniInt(L"Balance", L"EnemiesTreatHumansLikePlayer1", 1) != 0; // applied with the enemy set, below
        c.enemyNow = IniInt(L"General", L"EnemyChangesApplyNow", 0) != 0;   // f1.274 (testing)
        if (c.enemyNow) CoopLog("SETTINGS: TESTING - enemy setting changes apply during a battle too (EnemyChangesApplyNow=1)");
        c.bossTestAI = IniInt(L"General", L"BossTargetsAITeammates", 0) != 0;   // f1.213 / f1.214: bosses follow the enemy settings (enemies/22d)
        if (c.bossTestAI) CoopLog("SETTINGS: TESTING - bosses treat AI teammates like players (BossTargetsAITeammates=1)");
        c.bossNearest = IniInt(L"General", L"BossSpreadNearest", 0) != 0;   // f1.224 (the author: "Make it an optional setting to test")
        if (c.bossNearest) CoopLog("SETTINGS: TESTING - bosses that pick the nearest party member spread their attention too (BossSpreadNearest=1)");
        {
            int a = ClampI(IniInt(L"Balance", L"MenuSlowdown", 100), 0, 100);
            c.menuSlow = a / 100.f;
            int p2 = ClampI(IniInt(L"Balance", L"MenuSlowdownPlayer2", 100), 0, 100), p3 = ClampI(IniInt(L"Balance", L"MenuSlowdownPlayer3", 100), 0, 100);   // no MenuSlowdownOthers
            c.menuSlowP[1] = c.menuSlow; c.menuSlowP[2] = p2 / 100.f; c.menuSlowP[3] = p3 / 100.f;
            CoopLog("SETTINGS: command-menu slowdown %d%% (player 1's menu), %d%% (player 2's), %d%% (player 3's) of the game's own", a, p2, p3);
        }
        // the enemy settings, one set per party size. Each set starts from the game's normal, so a missing key = normal.
        c.dynamicDifficulty = IniInt(L"Balance", L"DynamicDifficulty", 1) != 0;   // f1.272
        CoopLog("SETTINGS: difficulty %s ([Balance] DynamicDifficulty)", c.dynamicDifficulty ? "follows how many people play ([Balance1] / [Balance2] / [Balance3])" : "the same for any number of people ([BalanceGlobal])");
        for (int si = 0; si < 4; ++si)
        {
            static const wchar_t* secs[4] = {L"Balance1", L"Balance2", L"Balance3", L"BalanceGlobal"};
            const wchar_t* sec = secs[si];
            BalanceSet& b = c.balSet[si];
            b = BalanceSetDefault(si < 3 ? si : 1);   // one person = the game's normal; two / three = Co-op Balanced (global: as two)
            auto BP = [&](const wchar_t* key, float* v, float lo, float hi) { IniFloat(sec, key, v, lo, hi, 100.f); };   // percent in the file
            BP(L"EnemyHP", &b.hp, 25.f, 1000.f);
            BP(L"EnemyDamage", &b.damage, 25.f, 500.f);
            BP(L"EnemyAttackRate", &b.attackRate, 50.f, 300.f);
            BP(L"StaggerRate", &b.stagger, 10.f, 500.f);
            BP(L"FocusOnPlayer2", &b.focusP2, 0.f, 1000.f);
            b.stayOnHit = ClampI(IniInt(sec, L"StayOnHitChance", 0), 0, 100) / 100.f;   // default 0 = the game's normal
            b.turnHeavy = ClampI(IniInt(sec, L"TurnOnHeavyDamage", 0), 0, 100) / 100.f;
            {   // (the author): Off / Low / Medium / High / Extreme (an old 0 / 1 = Off / Extreme). (the author: "make that a
                // feature, and then give a customizable % of health range"): the level = the chance (25 / 50 / 75 / 100 %) that an
                // enemy picking a new target goes for the weakest player below WeakTargetHealth instead (enemies/22, hate scores)
                wchar_t pw[32]; IniStr(sec, L"PreferWeakTargets", pw, 32);
                if (!_wcsicmp(pw, L"Low")) b.weakChance = 0.25f; else if (!_wcsicmp(pw, L"Medium")) b.weakChance = 0.5f;
                else if (!_wcsicmp(pw, L"High")) b.weakChance = 0.75f; else if (!_wcsicmp(pw, L"Extreme") || !wcscmp(pw, L"1")) b.weakChance = 1.f;
                b.weakHealth = ClampI(IniInt(sec, L"WeakTargetHealth", 20), 1, 99) / 100.f;
            }
            b.humansOnly = si == 0 || si == 3 || IniInt(sec, L"OnlyWithHumanPlayers", 1) != 0;   // (the one-character set has nobody to wait for)
            {   // f1.272: the command menus' slowdown / darkening of this set (missing = the old shared keys, read above)
                static const wchar_t* const kSlow[4] = {nullptr, L"MenuSlowdown", L"MenuSlowdownPlayer2", L"MenuSlowdownPlayer3"};
                static const wchar_t* const kDark[4] = {nullptr, L"MenuDarkenSplitPlayer1", L"MenuDarkenSplitPlayer2", L"MenuDarkenSplitPlayer3"};
                for (int p = 1; p <= 3; ++p)
                {
                    b.menuSlow[p] = ClampI(IniInt(sec, kSlow[p], (int)(c.menuSlowP[p] * 100.f + 0.5f)), 0, 100) / 100.f;
                    b.menuDarkSplit[p] = IniInt(sec, kDark[p], c.menuDarkSplit[p] ? 1 : 0) != 0;
                }
                b.menuDarken = IniInt(sec, L"MenuDarken", c.menuDarken ? 1 : 0) != 0;
                static const wchar_t* const kRate[3] = {L"Player1Rates", L"Player2Rates", L"Player3Rates"};   // f1.275
                for (int p = 0; p < 3; ++p)   // f1.276: the one character set's missing key = 100 (the game's normal - solo stays vanilla
                {                              // unless the player lowers it), not the shared [Balance] value (that was made for co-op)
                    int def = si == 0 ? 100 : (int)(c.ratePct[p] * 100.f + 0.5f);
                    // f1.333 (app review #6): the three character set's default = Co-op Balanced's 85 % - unless the file has the
                    // shared [Balance] key (then that, as before)
                    if (si == 2 && IniInt(L"Balance", kRate[p], -1) < 0) def = 85;
                    b.rate[p] = ClampI(IniInt(sec, kRate[p], def), 0, 100) / 100.f;
                }
            }
            char only[96] = "";
            if (si == 1 || si == 2) snprintf(only, sizeof(only), ", %s", b.humansOnly ? "only while that many people play" : "with AI teammates too");
            CoopLog("SETTINGS: %s party ([%ls]) - ATB P1 %.0f%% / P2 %.0f%% / P3 %.0f%%, menu slowdown P1 %.0f%% / P2 %.0f%% / P3 %.0f%%, darkening shared %s, own view P1 %s / P2 %s / P3 %s",
                si < 3 ? BalSetName(si) : "any size", sec, b.rate[0] * 100.f, b.rate[1] * 100.f, b.rate[2] * 100.f, b.menuSlow[1] * 100.f, b.menuSlow[2] * 100.f, b.menuSlow[3] * 100.f, b.menuDarken ? "on" : "off",
                b.menuDarkSplit[1] ? "on" : "off", b.menuDarkSplit[2] ? "on" : "off", b.menuDarkSplit[3] ? "on" : "off");
            CoopLog("SETTINGS: %s party ([%ls]) - enemy HP %.0f%%, damage %.0f%%, attack rate %.0f%%, stagger rate %.0f%%, focus on players 2/3 %.0f%%, stay on hit %.0f%%, turn on heavy damage %.0f%%, prefer weakened %.0f%% (below %.0f%% health)%s",
                si < 3 ? BalSetName(si) : "any size", sec, b.hp * 100.f, b.damage * 100.f, b.attackRate * 100.f, b.stagger * 100.f, b.focusP2 * 100.f,
                b.stayOnHit * 100.f, b.turnHeavy * 100.f, b.weakChance * 100.f, b.weakHealth * 100.f, only);
        }
        if (!g_BalBattle) { ApplyBalanceSet(c, g_BalSetIdx); g_BalReloadWaiting = false; }   // start-up / F6 outside a battle: at once
        else if (c.enemyNow)
        {   // f1.274 (testing, [General] EnemyChangesApplyNow): F6 in a battle - the enemy values apply at once (enemies already on
            // the field keep the health they got; damage / attack rate / stagger / targeting / a boss's damage factor change now)
            ApplyBalanceSet(c, g_BalSetIdx); g_BalReloadWaiting = false;
            CoopLog("SETTINGS: reloaded during a battle - TESTING: the enemy settings apply NOW (EnemyChangesApplyNow; enemies already here keep their health)");
        }
        else
        {   // F6 in a battle - the enemy values wait (only if they changed)
            CoopConfig t = c; ApplyBalanceSet(t, g_BalSetIdx);
            bool diff = t.balHP != c.balHP || t.balEnemyDamage != c.balEnemyDamage || t.balAttackRate != c.balAttackRate ||
                        t.balStagger != c.balStagger || t.balFocusP2 != c.balFocusP2 || t.stayOnHit != c.stayOnHit ||
                        t.turnHeavy != c.turnHeavy || t.balWeakChance != c.balWeakChance || t.balWeakHealth != c.balWeakHealth ||
                        t.humanAggro != c.humanAggro;
            if (diff) g_BalReloadWaiting = true;
            ApplyMenuSet(c, g_BalSetIdx);   // f1.272: the command menus' slowdown / darkening are not enemy values - at once
            CoopLog("SETTINGS: reloaded during a battle - %s", diff ? "the changed enemy settings WAIT until the battle is over (enemy settings never change during a battle)" :
                                                                    "the enemy settings didn't change");
        }
        CoopLog("SETTINGS: player rules for humans %d, enemies treat humans like player 1 %d", (int)c.humanRates, (int)c.humanAggro);
        CoopLog("SETTINGS: active enemy settings = the %s party's (enemy HP %.0f%%, damage %.0f%%, attack rate %.0f%%, stagger rate %.0f%%)", BalSetName(g_BalSetIdx), c.balHP * 100.f, c.balEnemyDamage * 100.f, c.balAttackRate * 100.f, c.balStagger * 100.f);
    }
}

// [PictureInPicture]: the split screen, its windows, the party panel / command HUD sizes, battle messages
static void ReadPictureInPicture(CoopConfig& c)
{
    // [PictureInPicture]
    {
        wchar_t t[128];
        c.pipEnabled = IniInt(L"PictureInPicture", L"Enabled", 1) != 0;   // default on (battle only) - the author: "Yes"
        c.menuTiltFix = IniInt(L"PictureInPicture", L"MenuTiltFix", 1);   // f1.307 / f1.320 (see 36h)
        c.monSpanTest = IniInt(L"PictureInPicture", L"MultiMonitor", IniInt(L"PictureInPicture", L"MonitorSpanTest", 0)) != 0;   // f1.290 (f1.285 test name as the fallback)
        {
            wchar_t mt[32];
            IniStr(L"PictureInPicture", L"MonitorPlayer1", mt, 32);
            c.monP1 = !_wcsicmp(mt, L"Left") ? 1 : !_wcsicmp(mt, L"Middle") ? 2 : !_wcsicmp(mt, L"Right") ? 3 : 0;
            IniStr(L"PictureInPicture", L"MonitorShare", mt, 32);
            c.monShare = !_wcsicmp(mt, L"Rows") ? 1 : 0;
            c.monFade = IniInt(L"PictureInPicture", L"MonitorFade", 1) != 0;
            c.monHudMap = IniInt(L"PictureInPicture", L"MonitorHudMap", 1);
            c.monNoPanini = IniInt(L"PictureInPicture", L"MonitorNoPanini", 1) != 0;
            c.monOneScreen = IniInt(L"PictureInPicture", L"MultiMonitorAsOneScreen", 0) != 0;
        }
        if (c.monSpanTest) CoopLog("SETTINGS: EXPERIMENTAL - multiple monitors on (MultiMonitor=1): player 1's monitor %s, two monitors with players 2 + 3 %s, fade %s, HUD map %d",
            c.monP1 == 1 ? "Left" : c.monP1 == 2 ? "Middle" : c.monP1 == 3 ? "Right" : "Main (Windows main monitor)", c.monShare ? "Rows" : "Columns", c.monFade ? "on" : "off", c.monHudMap);
        IniHotkey(L"PictureInPicture", L"ToggleKey", &c.pipVk, &c.pipCtrl, &c.pipShift, &c.pipAlt);
        IniHotkey(L"PictureInPicture", L"LayoutKey", &c.layVk, &c.layCtrl, &c.layShift, &c.layAlt, true);
        IniStr(L"PictureInPicture", L"Show", t, 128);
        c.pipShow = *t ? ((_wcsicmp(t, L"Battle") == 0) ? 2 : 0) : 2;   // OffScreen removed (treated as Always); missing = Battle
        auto PF = [&](const wchar_t* key, float* v, float lo, float hi) { IniFloat(L"PictureInPicture", key, v, lo, hi); };
        {   // the windows are always split screen (Half) - Size / Corner are no longer read
            wchar_t st[64];
            IniStr(L"PictureInPicture", L"Side", st, 64);
            if (!_wcsicmp(st, L"Left")) c.pipSide = 0; else if (!_wcsicmp(st, L"Right")) c.pipSide = 1;
            else if (!_wcsicmp(st, L"Top")) c.pipSide = 2; else if (!_wcsicmp(st, L"Bottom")) c.pipSide = 3;
            else if (!_wcsicmp(st, L"Thirds")) c.pipSide = 4;   // three columns, player 1 | 2 | 3 (halves until player 3 has a window)
        }
        // the window camera (Distance, Height, Pitch, FOV, TurnSpeed, CameraControl,
        //        CameraSpeed, AutoFollowDelay) is per player only - [Player2] / [Player3] Window* (ReadWindowCam)
        PF(L"Detail", &c.pipDetail, 0.5f, 10.f);
        PF(L"ViewDistance", &c.pipViewDist, 0.f, 1000000.f);
        { int q = IniInt(L"PictureInPicture", L"FullSizeHudQuality", 100); if (q >= 25 && q <= 100) c.pipFullPct = q; }
        {   // dynamic window quality
            c.pipDyn = IniInt(L"PictureInPicture", L"DynamicQuality", 0) != 0;
            c.pipDynFps = (float)ClampI(IniInt(L"PictureInPicture", L"DynamicTargetFps", 55), 20, 240);
            wchar_t od[128]; IniStr(L"PictureInPicture", L"DynamicOrder", od, 128);
            if (od[0])
            {   // "Detail, DrawDistance, Sharpness, HalfRate" - names in any order, each once; the rest unused
                static const wchar_t* const nm[4] = {L"Detail", L"DrawDistance", L"Sharpness", L"HalfRate"};
                int n = 0; for (int& v : c.pipDynOrder) v = 0;
                wchar_t* ctx = nullptr;
                for (wchar_t* t = wcstok_s(od, L", ;", &ctx); t && n < 4; t = wcstok_s(nullptr, L", ;", &ctx))
                    for (int k = 0; k < 4; ++k)
                        if (!_wcsicmp(t, nm[k]))
                        {
                            bool dup = false; for (int j = 0; j < n; ++j) if (c.pipDynOrder[j] == k + 1) dup = true;
                            if (!dup) c.pipDynOrder[n++] = k + 1;
                        }
            }
            PF(L"DynamicLowestDetail", &c.pipDynMaxDetail, 1.f, 10.f);
            PF(L"DynamicLowestDrawDistance", &c.pipDynMinDist, 1000.f, 1000000.f);
            { int q = IniInt(L"PictureInPicture", L"DynamicLowestSharpness", 50); if (q >= 25 && q <= 100) c.pipDynMinSharp = q; }
            if (c.pipDyn)
            {
                static const char* const on[5] = {"-", "detail", "draw distance", "sharpness", "half-rate windows"};
                CoopLog("SETTINGS: dynamic window quality ON - keeps %.0f fps, lowers %s, %s, %s, %s (lowest: detail %.1f, draw distance %.0f m, sharpness %d%%)",
                    c.pipDynFps, on[c.pipDynOrder[0]], on[c.pipDynOrder[1]], on[c.pipDynOrder[2]], on[c.pipDynOrder[3]],
                    c.pipDynMaxDetail, c.pipDynMinDist / 100.f, c.pipDynMinSharp);
            }
        }
        c.pipThirdsOn = IniInt(L"PictureInPicture", L"ThirdsLayout", 0) != 0;   // experimental, off by default
        c.thirdsCmdNudge = ClampI(IniInt(L"PictureInPicture", L"ThirdsCommandNudge", 40), 0, 300);   // (the author: "Can the menus be
        c.thirdsPanelNudge = ClampI(IniInt(L"PictureInPicture", L"ThirdsPanelNudge", 40), 0, 300);   //   shifted left slightly (left side menus) and right slightly (right side menus) for the 1/3rd screen config")
        c.msgLogX = ClampI(IniInt(L"PictureInPicture", L"MessageLogX", 0), -1500, 300);    // default 0 (at the right edge)   // (the author: "the text when an enemy is
        c.msgLogY = ClampI(IniInt(L"PictureInPicture", L"MessageLogY", 0), -600, 600);    //   killed ... add an option to move that the same way we move the menus")
        CoopLog("SETTINGS: battle messages - at the right edge of the screen, then %d px right, %d px down (MessageLogX / MessageLogY)", c.msgLogX, c.msgLogY);
        CoopLog("SETTINGS: thirds layout %s (ThirdsLayout)%s", c.pipThirdsOn ? "allowed" : "off", !c.pipThirdsOn && c.pipSide == 4 ? " - Side=Thirds acts as Right" : "");
        c.pipTallFov = IniInt(L"PictureInPicture", L"TallWindowFov", 1) != 0;
        c.pipForAI = IniInt(L"PictureInPicture", L"WindowsForAI", 0) != 0;   // (the author: "for the purposes of testing only")
        if (c.pipForAI) CoopLog("SETTINGS: TESTING - split-screen windows also for AI-played teammates (WindowsForAI=1)");
        c.pipWallRadius = (float)ClampI(IniInt(L"PictureInPicture", L"WindowWallRadius", 20), 0, 100);
        c.pipWallSolid = IniInt(L"PictureInPicture", L"WindowWallSolidObjects", 1) != 0;   // the storeroom door
        {   // (the author): party health / ATB panel
            wchar_t pp[32]; IniStr(L"PictureInPicture", L"PartyPanel", pp, 32); c.partyIndiv = pp[0] ? !_wcsicmp(pp, L"Individual") : true;   // f1.330: missing = Individual
            IniStr(L"PictureInPicture", L"PartyPanelRows", pp, 32); c.partyRows = !pp[0] ? 2 : !_wcsicmp(pp, L"Own") ? 1 : !_wcsicmp(pp, L"OwnAndAI") ? 2 : 0;   // f1.331: missing = OwnAndAI   // OwnAndAI
            c.partyScale = ClampI(IniInt(L"PictureInPicture", L"PartyPanelScale", 90), 50, 100);   // (the author: "My current app settings should be the default"): 90 / 85 / 85
            c.partyScale3 = ClampI(IniInt(L"PictureInPicture", L"PartyPanelScaleThird", 85), 50, 100);     // (the author: "delineated
            c.partyScale4 = ClampI(IniInt(L"PictureInPicture", L"PartyPanelScaleQuarter", 85), 50, 100);   //   between 1/2 window, 1/3 window, and 1/4 window")
            c.cmdScale = ClampI(IniInt(L"PictureInPicture", L"CommandMenuScale", 90), 50, 100);          // (the author: "set the
            c.cmdScale3 = ClampI(IniInt(L"PictureInPicture", L"CommandMenuScaleThird", 85), 50, 100);    //   defaults to ... 90/90/75"; "I reduced 1/3rd value to 85 ... should be our new default value")
            c.cmdScale4 = ClampI(IniInt(L"PictureInPicture", L"CommandMenuScaleQuarter", 85), 50, 100);
            CoopLog("SETTINGS: party health / ATB panel %s (PartyPanel%s)", c.partyIndiv ? (c.partyRows == 1 ? "individual - each player's own row in their part of the screen" : c.partyRows == 2 ? "individual - each player's own row + the AI-played teammates' in their part of the screen" : "individual - the whole panel in each player's part of the screen") : "shared - the game's one panel", c.partyIndiv ? ", PartyPanelRows" : "");
        }   // CAMWALL: the game's camera walls
        c.pipVignette = IniInt(L"PictureInPicture", L"WindowVignette", 0) != 0;
        PF(L"WindowBrightness", &c.pipBright, -3.f, 3.f);
        CoopLog("SETTINGS: windows - tall windows keep the 16:9 height angle %s (TallWindowFov), vignette %s (WindowVignette), brightness %+.1f EV (WindowBrightness)",
            c.pipTallFov ? "on" : "off", c.pipVignette ? "the game's" : "off", c.pipBright);
        CoopLog("SETTINGS: window HUD - the game's own, full size (always since f1.56), picture %d%% of the screen", c.pipFullPct);
        int cs = IniInt(L"PictureInPicture", L"CaptureSource", 2); if (cs >= 0 && cs <= 7) c.pipSource = cs;
        int fm = IniInt(L"PictureInPicture", L"Format", 2); if (fm >= 0 && fm <= 8) c.pipFormat = fm;
        CoopLog("SETTINGS: picture-in-picture %s (toggle VK 0x%02X), show %s, detail %.1f, view dist %.0f, source %d format %d",
            c.pipEnabled ? "ON" : "OFF", c.pipVk, c.pipShow == 1 ? "off-screen only" : c.pipShow == 2 ? "battle only" : "always",
            c.pipDetail, c.pipViewDist, c.pipSource, c.pipFormat);
    }
}

// [Input2] Seat1 + [Player2] / [Player3]: who has a controller, each player's controls and window camera
static void ReadPlayers(CoopConfig& c, PlayerCfg& p2, PlayerCfg& c3)
{
    c.p1Controller = SeatPickKind(1) != 0;   // [Input2] Seat1 (blank = Auto); Keyboard / None = keyboard only

    ReadPlayerPad(p2, L"Player2", 2);   // shared with player 3
    ReadWindowCam(p2, L"Player2", 2);
    g_Player->partySlot = p2.p2Party - 1;
    g_Player->joined = p2.joinAtStart;   // (the controllers themselves: the seats, input/40b In2Start)

    // player 3
    {
        c3 = PlayerCfg();
        c3.p2Party = 3; c3.joinVk = VK_F10; c3.joinCtrl = true;   // Ctrl+F10 (was F11 - the game's own windowed / fullscreen key, the author 2026-10-22)
        c3.joinShift = false; c3.joinAlt = false;
        {   // [Input2] Seat3 only (blank = Auto = on); None (or Keyboard - only player 1 uses the keyboard) = off
            char s3[220]; in2::seats::Want w3 = SeatWant(3, s3, sizeof(s3));   // (review #30): read once
            g_P3Enabled = w3.kind == in2::seats::SEAT_AUTO || w3.kind == in2::seats::SEAT_DEVICE;
            CoopLog("SETTINGS: player 3 %ls ([Input2] Seat3=%s)", g_P3Enabled ? L"on" : L"off", s3[0] ? s3 : "(blank = Auto)");
        }
        if (g_P3Enabled)
        {
            ReadPlayerPad(c3, L"Player3", 3);
            ReadWindowCam(c3, L"Player3", 3);
            IniHotkey(L"Player3", L"JoinKey", &c3.joinVk, &c3.joinCtrl, &c3.joinShift, &c3.joinAlt);
            c3.joinAtStart = ReadJoinAtStart(L"Player3", 3);   // 1 / 0 / Auto
            CoopLog("SETTINGS: player 3 ON - party slot %d, join key VK 0x%02X, joins at start %d", c3.p2Party, c3.joinVk, (int)c3.joinAtStart);
        }
        else CoopLog("SETTINGS: player 3 off ([Input2] Seat3=None)");
    }
}

// the closing SETTINGS lines
static void LogConfigSummary(CoopConfig& c, PlayerCfg& p2)
{
    CoopLog("SETTINGS loaded from FF7RCoopMod.ini next to the game");   // no full path (it can hold the Windows user name)
    CoopLog("SETTINGS: join key %s%s%sVK 0x%02X, join at start %d, marker %d",
        p2.joinCtrl ? "Ctrl+" : "", p2.joinShift ? "Shift+" : "", p2.joinAlt ? "Alt+" : "", p2.joinVk, (int)p2.joinAtStart, (int)c.showMarker);
    CoopLog("SETTINGS: player 1 = %s", c.p1Controller ? "CONTROLLER + keyboard" : "keyboard only");
    CoopLog("SETTINGS: player 2 = party slot %d, deadzone %.2f, attack 0x%05lX dodge 0x%05lX guard 0x%05lX ability 0x%05lX menu 0x%05lX lock 0x%05lX",
        p2.p2Party, p2.moveDeadzone, p2.bAttack, p2.bDodge, p2.bGuard, p2.bAbility, p2.bMenu, p2.bLock);
}

static void LoadConfig()
{
    IniFindOrWriteDefault();
    CoopConfig& c = g_Cfg;
    ReadGeneral(c, g_P2Cfg);
    ReadLeadAndMarkers(c);
    ReadCamera(c);
    ReadBalance(c);
    ReadPictureInPicture(c);
    ReadPlayers(c, g_P2Cfg, g_P3Cfg);
    LogConfigSummary(c, g_P2Cfg);
}
static void EnsureConfig()
{
    static volatile LONG done = 0;
    if (InterlockedCompareExchange(&done, 1, 0) == 0) LoadConfig();
}


