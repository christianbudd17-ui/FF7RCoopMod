// input2/in2_internal.h - what the controller module's own files share (in2.cpp core + device list + thread,
// in2_hid.cpp PlayStation / HID, in2_dinput.cpp DirectInput, in2_xinput.cpp XInput slots). (review #19): in2.cpp was
// one file doing four jobs; split per channel behind the same in2.h. NOT for the rest of the mod - only in2.h is.
// (The unit test tools/tests/unit_in2.cpp includes this header to check the decoders / slot matching directly.)
#pragma once
#define DIRECTINPUT_VERSION 0x0800
#include "in2.h"
#include <dinput.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <cctype>

namespace in2 {
namespace detail {

// ---------------------------------------------------------------------------------------------------------
// state
// ---------------------------------------------------------------------------------------------------------
constexpr int   MAXD = 32;
constexpr DWORD POLL_MS = 4, SCAN_MS = 2000, HID_FRESH_MS = 500, MATCH_WINDOW_MS = 60;

struct XGamepad { WORD b; BYTE lt, rt; SHORT lx, ly, rx, ry; };
struct XState { DWORD packet; XGamepad g; };

struct Dev
{
    Device pub;                       // what readers get (copied to g_pub after every poll)
    // DirectInput
    IDirectInputDevice8W* di; GUID diInst; int diFails; DWORD diSeenScan;
    // HID
    bool hidSeen; DWORD hidSeenScan; int sony;   // 0 = not a Sony pad, 1 = DualShock 4, 2 = DualSense
    HANDLE hidRead; volatile LONG readerOn; bool readLogged; USHORT inLen, featLen; int readFails, readFailLogs; DWORD readRetryT;
    DWORD diSkipUntil; GUID diSkipGuid; int diReopens;
    Pad hidState; DWORD hidMs;        // written by the reader thread under g_lock
    bool hidFull;                     // (B4): the last report was a full one (sent continuously, not only on change)
    // presses / XInput matching
    uint16_t prevBtn; uint32_t prevRaw; int pressLogs;
    uint16_t edgeBtn; DWORD edgeT;    // last DirectInput-side press edge (for matching to an XInput slot)
    int matchSlot, matchCount;
    // what it says it is (LooksLikeGamepad)
    uint8_t hidUsage, diType; int diButtons, diAxes;
    // DIAG: HID reader timing (written by its reader thread, logged by the worker every 30 s), DirectInput read time
    volatile LONG dgReports, dgQuick, dgMaxGapMs, dgDiMaxUs, dgDiSkipped;
    volatile LONG dgLost; int dgLastCnt;   // DIAG (dgLastCnt -1 = none yet)
};

// the shared state (defined in in2.cpp)
extern Dev   g_d[MAXD];
extern Device g_pub[MAXD];
extern int   g_n, g_pubN, g_nextId;
extern CRITICAL_SECTION g_lock;
extern Host  g_h;
extern volatile LONG g_running;
extern IDirectInput8W* g_di;
extern bool  g_xConn[4];
extern XState g_x[4];
extern uint16_t g_xPrev[4], g_xEdgeBtn[4];
extern DWORD g_xEdgeT[4];
extern int   g_xBoundTo[4];   // device index tied to the slot
extern int   g_pressLogsTotal;
extern uint32_t g_rej[256]; extern int g_rejN;   // HID interfaces already found not to be controllers (path hashes)
extern DWORD g_scanNo;
extern bool  g_diWaitWindow;   // DirectInput controllers wait for the game window (scan again soon)

#define LOG(...) do { if (g_h.log) g_h.log(__VA_ARGS__); } while (0)

// small helpers + the device list (in2.cpp)
void Lower(char* s);
void WideToUtf8(const wchar_t* w, char* out, size_t n);
bool VidPidFromPath(const char* p, uint16_t* vid, uint16_t* pid);
int  SonyKind(uint16_t vid, uint16_t pid);
const char* ButtonName(uint16_t bit);
uint16_t PovToDpad(DWORD pov);
int16_t ClampS(long v);
int  FindByPathIdx(const char* path);
int  NewDev(const char* path);
void SetIdentity(Dev& d, uint16_t vid, uint16_t pid, const char* name);
void DescribeTo(const Device& d, char* out, size_t n);
void Publish();
// HID / PlayStation (in2_hid.cpp)
void DecodeDs4(const uint8_t* p, Pad& o);
void DecodeDualSense(const uint8_t* p, Pad& o);
void LoadHidApi();
void ScanHid();
// DirectInput (in2_dinput.cpp)
void DecodeDi(const DIJOYSTATE2& js, Layout lay, Pad& o);
HRESULT DiRead(IDirectInputDevice8W* dev, DIJOYSTATE2* js);
void DiClose(Dev& d);
void ScanDi();
// XInput slots (in2_xinput.cpp)
void Unbind(int slot, const char* why);
void PollXInput(bool scan);
void MatchXInput();

}   // namespace detail
}   // namespace in2
