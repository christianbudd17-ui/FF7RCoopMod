static bool OwnCmdMenuOpen();   // (menus/10): this player's own command menu is open
// (review A9): the game's FKey globals the mod asks about (each holds the key's FName) - one name each, in one place
// (the attack / guard / shortcut ones were raw numbers in several files)
static constexpr uintptr_t RVA_KEY_ATTACK = 0x5903B18, RVA_KEY_DODGE = 0x5903C08, RVA_KEY_GUARD = 0x5903BC0, RVA_KEY_ABILITY = 0x5903B60;
static constexpr uintptr_t RVA_KEY_SHORTCUTMENU = 0x5903C80;   // the shortcut palette's open / hold key
static constexpr uintptr_t RVA_KEY_SLOT[4] = {0x5903E30, 0x5903E48, 0x5903E60, 0x5903E78};   // Shortcut1-4_Second: a palette slot
static constexpr uintptr_t RVA_KEY_TARGETLOCK = 0x5903C20;
static constexpr uintptr_t RVA_KEY_COMMANDMENU = 0x5903B30, RVA_KEY_NEXTMEMBER = 0x5903C68, RVA_KEY_PREVMEMBER = 0x5903C50;
static uint64_t KeyFName(uintptr_t rva) { return *(uint64_t*)(g_Base + rva); }
// (review #23 / #28): the current player's controller, read ONCE at the top of their frame (PlayerFrameInput) and
// shared by PollPad2, PollP2Connection and P2SwitchTick - each of them read it again before (three reads of the
// same state a frame). The battle command pass (menus/08c BcmBefore) still reads its own: it runs later in the frame.
static void ReadPadThisFrame()
{
    g_Player->padFrameOk = Real_XInputGetState && PadGetMine(&g_Player->padFrame) == ERROR_SUCCESS;
    if (!g_Player->padFrameOk) memset(&g_Player->padFrame, 0, sizeof(g_Player->padFrame));
}
// The current player's sticks and buttons from this frame's read of their controller (ReadPadThisFrame).
static void PollPad2()
{
    g_Player->fieldPrevButtons = g_Player->padNow;   // last frame's buttons (field attack edge)
    g_Player->stickX = g_Player->stickY = 0.f;
    g_Player->rStickX = g_Player->rStickY = 0.f;
    g_Player->padNow = 0;
    if (!Real_XInputGetState) return;
    const XINPUT_STATE& st = g_Player->padFrame;
    int dgi = g_Player->num == 3 ? 1 : 0; InterlockedIncrement(&g_DgPadCalls[dgi]);   // DIAG
    if (!g_Player->padFrameOk) return;
    InterlockedIncrement(&g_DgPadOk[dgi]);
    if (st.Gamepad.wButtons) InterlockedIncrement(&g_DgPadBtn[dgi]);
    if (st.Gamepad.sThumbLX > 8000 || st.Gamepad.sThumbLX < -8000 || st.Gamepad.sThumbLY > 8000 || st.Gamepad.sThumbLY < -8000) InterlockedIncrement(&g_DgPadStick[dgi]);
    int dz = (int)(g_Player->cfg->moveDeadzone * 32767.f);   // from the settings file
    g_Player->stickX = NormStick(st.Gamepad.sThumbLX, dz);
    g_Player->stickY = NormStick(st.Gamepad.sThumbLY, dz);
    g_Player->rStickX = NormStick(st.Gamepad.sThumbRX, RIGHT_STICK_DEADZONE);
    g_Player->rStickY = NormStick(st.Gamepad.sThumbRY, RIGHT_STICK_DEADZONE);
    if (OwnCmdMenuOpen()) g_Player->stickX = g_Player->stickY = g_Player->rStickX = g_Player->rStickY = 0.f;   // (code review C1): their own menu open - the sticks
                                                                    // don't move / aim (buttons go on: their command system reads them)

    g_Player->padNow = PadBits(st.Gamepad);
}

// Join keys (v2.1 used Ctrl+F12; now [General] JoinKey = F10 for player 2, [Player3] JoinKey = Ctrl+F10 since f1.90,
// or JoinButton on the pad). Only count while the game window is the active window, so typing elsewhere can't flip them.
static void ResetP2Targeting(); // v3.4, defined in the P2 TARGETING section
static void PollCameraToggleKey();

// ---- v5.7: online-friendly player 2 controller handling ----
// For streaming (Parsec / Steam Remote Play): the guest's controller appears on the host as a
// normal controller in whatever slot is free, can come and go, and the guest can't reach the host's
// keyboard. So: Auto ([Input2] SeatN) picks the first free connected gamepad; holding JoinButton
// (Start) joins/leaves from the controller; and a lost controller hands the character to the AI
// after 3 s, then gives it back when the controller returns.
static wchar_t g_Notice[160] = L"";
static float g_Fps = 0.f;   // smoothed frame rate (ShowFPS)
static ULONGLONG g_NoticeAt = 0;
static void HudNotice(const wchar_t* msg) { wcsncpy(g_Notice, msg, 159); g_Notice[159] = 0; g_NoticeAt = GetTickCount64(); }
static const wchar_t* PadName(DWORD mask);   // core/26
// these were function statics; now per player
static void SetCoop(bool on, const char* why)
{
    if (g_Player->joined == on) return;
    g_Player->joined = on;
    ResetP2Targeting();
    CoopLog("PLAYER %d is now %s (%s)", g_Player->num, on ? "IN" : "OUT", why);
    wchar_t who[40]; PlayerLabel(g_Player->num, who, 40);
    wchar_t msg[96]; swprintf(msg, 96, on ? L"%ls joined" : L"%ls left - the AI takes over", who);
    HudNotice(msg);
}
static void PollP2Connection()
{
    if (!Real_XInputGetState) return;
    ULONGLONG now = GetTickCount64();
    const XINPUT_STATE& st = g_Player->padFrame;
    // this seat's controller (by identity; from [Input2] SeatN - Auto = the first free gamepad - at the
    // start, input/40b) - the v5.7 Auto scan of XInput slots is gone; read once this frame (ReadPadThisFrame)
    bool ok = g_Player->padFrameOk;
    int seat = g_Player->num == 3 ? 2 : 1;
    // no controller at all when the game starts counts as "lost" too
    if (g_Player->connFirstCheck) { g_Player->connFirstCheck = false; if (!ok) { g_Player->connLostAt = now; char t[180]; In2SeatText(seat, t, sizeof(t)); CoopLog("PAD: no player %d controller yet (%s)", g_Player->num, t); } }
    if (ok != g_Player->connected)
    {
        g_Player->connected = ok;
        char t[180]; In2SeatText(seat, t, sizeof(t));
        CoopLog("PAD: player %d controller (%s) %s", g_Player->num, t, ok ? "CONNECTED" : "DISCONNECTED");
        wchar_t msg[96];
        wchar_t who[40]; PlayerLabel(g_Player->num, who, 40);
        if (ok)
        {   // a player who is out (Auto-picked controller, JoinAtStart=Auto) is told how to join
            g_Player->connLostAt = 0;
            if (!g_Player->joined && !g_Player->autoLeft && g_Player->cfg->bJoin) swprintf(msg, 96, L"%ls's controller connected - hold %ls to join", who, g_Player->cfg->bJoin == 0x0010 ? L"Start" : PadName(g_Player->cfg->bJoin));
            else swprintf(msg, 96, L"%ls's controller connected", who);
            HudNotice(msg);
        }
        else { g_Player->connLostAt = now; if (g_Player->joined) { swprintf(msg, 96, L"%ls's controller disconnected", who); HudNotice(msg); } }
    }
    if (!ok && g_Player->joined && g_Player->cfg->autoLeave && g_Player->connLostAt && now - g_Player->connLostAt > AUTO_LEAVE_AFTER_MS)
    {
        SetCoop(false, "controller disconnected");
        g_Player->autoLeft = true;
    }
    if (ok && g_Player->autoLeft) { g_Player->autoLeft = false; SetCoop(true, "controller back"); }

    // hold the join button to join / leave
    bool held = ok && g_Player->cfg->bJoin && (PadBits(st.Gamepad) & g_Player->cfg->bJoin) == g_Player->cfg->bJoin;
    if (!held) { g_Player->joinHeldSince = 0; g_Player->joinFired = false; return; }
    if (!g_Player->joinHeldSince) g_Player->joinHeldSince = now;
    if (!g_Player->joinFired && now - g_Player->joinHeldSince >= (ULONGLONG)(JOIN_HOLD_SECONDS * 1000.f))
    {
        g_Player->joinFired = true;
        g_Player->autoLeft = false;
        SetCoop(!g_Player->joined, "join button on the player's controller");
    }
}
static bool HotkeysAllowed();
static void NoteHotkeyIgnored(const char* what);
static void PollCoopToggleKey()
{
    bool& wasDown = g_Player->joinKeyWasDown;   // per player
    // key from the settings file, exactly its own modifiers
    bool down = HotkeyDown(g_Player->cfg->joinVk, g_Player->cfg->joinCtrl, g_Player->cfg->joinShift, g_Player->cfg->joinAlt);
    if (down && !wasDown && !HotkeysAllowed()) NoteHotkeyIgnored("join key");
    else if (down && !wasDown)
    {
        g_Player->autoLeft = false;
        SetCoop(!g_Player->joined, "keyboard join key");
    }
    wasDown = down;
}

static bool ActionPassAxis(float* out);   // pip_hud/30 (f1.223): players 2 / 3's command pass - their own stick
static uint64_t Hook_GetAxisPair(void* keyX, void* keyY, float* out)
{
    // Always call the original: it also releases the key objects the caller passed in.
    uint64_t r = Orig_GetAxisPair(keyX, keyY, out);
    if (g_InPass2 && out)
    {
        out[0] = g_Player->stickX;
        out[1] = g_Player->stickY;
    }
    else if (out) ActionPassAxis(out);
    return r;
}

static void NameOf(void* uobj, char* out, size_t outLen); // defined later

// outside battle, the extra movement pass (player 2's character as "leader") asks for the
// Attack key itself - answer it from player 2's controller so they can hit boxes etc. In battle the
// command-manager pass already handles attacks, so it stays 0 there.
static bool InBattleNow();
// (review A9): the field attack, shared by the movement pass's three key hooks (KeyHeld / KeyPressed here, KeyE00 / KeyF30
// seats/13, WidgetKey menus/08b - each answers in its own format): true if fname is the Attack key outside battle with no menu
// turn; down / was = this player's attack button now / last frame
static bool FieldAttackKey(uint64_t fname, bool* down, bool* was)
{
    static uint64_t attackF = 0;
    if (!attackF && g_Base) attackF = KeyFName(RVA_KEY_ATTACK);
    if (!(fname && fname == attackF && !InBattleNow())) return false;
    *down = (g_Player->padNow & g_Player->cfg->bAttack) != 0; *was = (g_Player->fieldPrevButtons & g_Player->cfg->bAttack) != 0;
    return true;
}
static uint64_t P2PassKey(void* key, bool pressedEdge)
{
    bool down, was;
    if (FieldAttackKey(key ? *(uint64_t*)key : 0, &down, &was)) return pressedEdge ? (down && !was ? 1 : 0) : (down ? 1 : 0);
    return 0;
}
// the game's "key held" check: inside player 2 / 3's movement pass it is answered from their controller (P2PassKey)
static uint64_t Hook_KeyHeld(void* key)
{
    uint64_t r = Orig_KeyHeld(key);
    return g_InPass2 ? P2PassKey(key, false) : r;
}

static uint64_t Hook_KeyPressed(void* key)
{
    uint64_t r = Orig_KeyPressed(key);
    if (g_InPass2) return P2PassKey(key, true);
    return r;
}


// two more key-state helpers used by the per-character battle control code (0x963xxx):
static constexpr uintptr_t RVA_KEY_Q3 = 0x16E2640; // -> 0x1394BB0
static constexpr uintptr_t RVA_KEY_Q4 = 0x16E2780; // -> 0x1394CE0
static KeyCheck_t Orig_KeyQ3 = nullptr;
static KeyCheck_t Orig_KeyQ4 = nullptr;
static uint64_t Hook_KeyQ3(void* key)
{
    uint64_t r = Orig_KeyQ3(key);
    return g_InPass2 ? 0 : r;
}
static uint64_t Hook_KeyQ4(void* key)
{
    uint64_t r = Orig_KeyQ4(key);
    return g_InPass2 ? 0 : r;
}

static void SaveState(unsigned char* pc, unsigned char* dst)
{
    memcpy(dst, pc + STATE_A_OFF, STATE_A_LEN);
    memcpy(dst + STATE_A_LEN, pc + STATE_B_OFF, STATE_B_LEN);
}
static void LoadState(unsigned char* pc, const unsigned char* src)
{
    memcpy(pc + STATE_A_OFF, src, STATE_A_LEN);
    memcpy(pc + STATE_B_OFF, src + STATE_A_LEN, STATE_B_LEN);
}









