// ---- v9.2: the game's own PlayStation 4 controller reader ----
// Besides XInput and DirectInput, the game reads DualShock 4 pads (054C:05C4 / 09CC) itself, straight from
// the HID device (reader thread 0x3F837E0; every report goes through the ReadFile wrapper 0x3F878C0:
// int(HANDLE, BYTE* buf, DWORD len, DWORD* got, BOOL wait), 0 = ok). So a DS4 that belongs to player 2 or 3
// (through Steam Input / DS4Windows as XInput, or as a DirectInput controller) would ALSO drive player 1.
// every DS4 report the game reads is idle (DirectInputPads=Auto) - the players' PlayStation pads are read by the
// new controller module from its own handle, and the leader's reaches the game as XInput slot 0. (The v9.2 press-
// timing match of each handle against the players' controllers - HidPollX / HidTrack / owner - was removed.)
typedef int (*HidRead_t)(HANDLE h, unsigned char* buf, DWORD len, DWORD* got, uint64_t wait);
static HidRead_t Orig_HidRead = nullptr;
// DS4 input report: USB id 0x01 (data from byte 1) or Bluetooth id 0x11 (data from byte 3).
static void HidIdle(unsigned char* b, DWORD n, int off)
{
    if (n < (DWORD)off + 9) return;
    b[off + 0] = b[off + 1] = b[off + 2] = b[off + 3] = 0x80;   // sticks centred
    b[off + 4] = 0x08;                                          // D-pad released, face buttons up
    b[off + 5] = 0;                                             // shoulders, triggers, Share/Options, L3/R3
    b[off + 6] &= 0xFC;                                         // PS / touchpad click (keep the counter)
    b[off + 7] = b[off + 8] = 0;                                // trigger pressure
    if (n > (DWORD)off + 38) { b[off + 34] |= 0x80; b[off + 38] |= 0x80; }   // no fingers on the touchpad
}
// the DualSense (054C:0CE6 / 0DF2). The game reads it here too (Bluetooth report 0x31, data from
// byte 2; USB report 0x01, data from byte 1): LX LY RX RY L2 R2 counter, buttons at +7 (D-pad nibble,
// square cross circle triangle), +8 (L1 R1 L2 R2 Create Options L3 R3), +9 (PS, touchpad, mute).
// the game always gets it idle; the player's state comes from the new controller module's own HID reads
// (the v14.3 decoding of the game's reports, DsDecode / g_DsState, was removed).
// the game reads SEVERAL reports per call (its buffer is a multiple of the report length: 468 / 546 bytes = 6 / 7
// Bluetooth DualSense reports of 78). Before f1.12 only the first report of each read was made idle - the rest reached
// the game with the real buttons and sticks. Now every report in the buffer is made idle (stride = the device's input
// report length, from its HID caps).
static int HidInfoOf(HANDLE h, int* stride)   // returns 1 = DS4, 2 = DualSense, 0 = unknown; *stride = input report length (0 = unknown)
{
    typedef BOOLEAN (__stdcall *GetAttr_t)(HANDLE, void*);
    typedef BOOLEAN (__stdcall *GetPre_t)(HANDLE, void**);
    typedef BOOLEAN (__stdcall *FreePre_t)(void*);
    typedef LONG (__stdcall *GetCaps_t)(void*, void*);
    static GetAttr_t ga = nullptr;   // hid.dll's functions, looked up once (st: 0 = not yet, else done)
    static GetPre_t gp = nullptr;
    static FreePre_t fp = nullptr;
    static GetCaps_t gc = nullptr;
    static int st = 0;
    if (st == 0)
    {
        st = -1; HMODULE m = LoadLibraryW(L"hid.dll");
        if (m) { ga = (GetAttr_t)GetProcAddress(m, "HidD_GetAttributes"); gp = (GetPre_t)GetProcAddress(m, "HidD_GetPreparsedData");
                 fp = (FreePre_t)GetProcAddress(m, "HidD_FreePreparsedData"); gc = (GetCaps_t)GetProcAddress(m, "HidP_GetCaps"); }
        if (ga) st = 1;
    }
    *stride = 0;
    if (st != 1) return 0;
    struct { ULONG Size; USHORT Vid, Pid, Ver; } a = {sizeof(a), 0, 0, 0};
    if (!ga(h, &a)) return 0;
    if (gp && fp && gc)
    {
        void* pp = nullptr;
        if (gp(h, &pp) && pp)
        {
            struct { USHORT Usage, UsagePage, InLen, OutLen, FeatLen; USHORT rest[17 + 10]; } c; memset(&c, 0, sizeof(c));   // HIDP_CAPS
            if (gc(pp, &c) == 0x00110000) *stride = c.InLen;   // HIDP_STATUS_SUCCESS
            fp(pp);
        }
    }
    if (a.Vid != 0x054C) return 0;
    if (a.Pid == 0x0CE6 || a.Pid == 0x0DF2) return 2;
    return 1;
}
static void DsIdle(unsigned char* p)
{
    p[0] = p[1] = p[2] = p[3] = 0x80; p[4] = p[5] = 0;
    p[7] = 0x08; p[8] = 0; p[9] &= 0xF8;
}
static int Hook_HidRead(HANDLE h, unsigned char* buf, DWORD len, DWORD* got, uint64_t wait)
{
    int r = Orig_HidRead(h, buf, len, got, wait);
    if (r != 0 || !buf || !got) return r;
    DWORD n = *got; if (n < 10 || n > len) return r;
    // (code review B3): the handle -> kind / report length cache. Before: 8 entries, never cleared, keyed by handle only -
    // a handle number reused for another kind of pad kept the old kind (wrong idle layout: a DualSense could reach the game /
    // "D-pad up held"), and from the 9th handle on every report logged a line. Now: an entry is checked again whenever the
    // report ID changes (also covers Bluetooth simple -> full reports), a full cache replaces its oldest entry, and the
    // "PS PAD" line is logged when an entry is new or its kind / length changed (max 20 lines).
    static HANDLE kh[8];
    static int kk[8], ks[8], kid[8];
    static int kn = 0, knext = 0, klogs = 0;
    int kind = -1, stride = 0, slot = -1; for (int i = 0; i < kn; ++i) if (kh[i] == h) { slot = i; break; }
    if (slot >= 0 && kid[slot] == buf[0]) { kind = kk[slot]; stride = ks[slot]; }
    else
    {
        kind = HidInfoOf(h, &stride);
        bool changed = slot < 0 || kk[slot] != kind || ks[slot] != stride;
        if (slot < 0) { if (kn < 8) slot = kn++; else { slot = knext; knext = (knext + 1) % 8; } }
        kh[slot] = h; kk[slot] = kind; ks[slot] = stride; kid[slot] = buf[0];
        if (changed && klogs < 20)
        {
            ++klogs;
            CoopLog("PS PAD: the game reads a PlayStation controller directly (handle %p, report 0x%02X, %lu bytes = %lu report(s) of %d) - %s - all idle for the game", h, buf[0], n,
                    stride > 0 ? n / (DWORD)stride : 1, stride, kind == 2 ? "DualSense (PS5)" : kind == 1 ? "DualShock 4" : "unknown kind");
        }
    }
    DWORD step = stride >= 10 && (DWORD)stride <= n ? (DWORD)stride : n;   // unknown length: the whole read is one report
    for (DWORD o = 0; o + 10 <= n; o += step)
    {
        unsigned char* b = buf + o; DWORD m = n - o < step ? n - o : step;
        if (kind == 2)
        {   // DualSense: Bluetooth 0x31 data from byte 2, USB 0x01 from byte 1
            int doff = b[0] == 0x31 ? 2 : b[0] == 0x01 ? 1 : -1;
            if (doff > 0 && m >= (DWORD)doff + 10) DsIdle(b + doff);
        }
        else
        {   // DualShock 4: USB 0x01 from byte 1, Bluetooth 0x11 from byte 3
            int off = b[0] == 0x01 ? 1 : b[0] == 0x11 ? 3 : -1;
            if (off > 0) HidIdle(b, m, off);
        }
    }
    return r;
}
