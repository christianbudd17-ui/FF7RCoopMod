// unit_log.cpp (f1.207) - checks core/02 CoopLog's log file housekeeping: at launch a log under 20 MB is kept (appended
// to), one over 20 MB is renamed to FF7RCoopMod_Coop_previous.log; in a session the log is renamed past 40 MB, and the
// new log's first line says so. No game needed. Build / run: tools/test.sh (runs it in an empty folder).
// Each case is its own process (CoopLog's launch housekeeping runs once per process): unit_log.exe <case>.
#define COOP_MOD_VERSION "test"
#include "../../core/00_includes_xinput_types.inc"
#include "../../core/02_state_globals_log.inc"
#include <cstring>
static void LoadP3(bool) {}

static long long SizeOf(const wchar_t* p)
{
    WIN32_FILE_ATTRIBUTE_DATA fa{};
    if (!GetFileAttributesExW(p, GetFileExInfoStandard, &fa)) return -1;
    return ((long long)fa.nFileSizeHigh << 32) | fa.nFileSizeLow;
}
static void MakeFile(const wchar_t* p, long long bytes)
{
    FILE* f = _wfopen(p, L"wb");
    static char line[1024];
    memset(line, 'x', sizeof(line) - 1); line[sizeof(line) - 1] = '\n';
    for (long long n = 0; n < bytes; n += sizeof(line)) fwrite(line, 1, sizeof(line), f);
    fclose(f);
}
static bool FirstLineHas(const wchar_t* p, const char* what)
{
    FILE* f = _wfopen(p, L"rb");
    if (!f) return false;
    char buf[512] = {};
    fgets(buf, sizeof(buf), f);
    fclose(f);
    return strstr(buf, what) != nullptr;
}
static int bad = 0;
static void Check(bool ok, const char* what) { printf("%s %s\n", ok ? "ok  " : "FAIL", what); if (!ok) ++bad; }

int main(int argc, char** argv)
{
    const wchar_t* LOG = L"FF7RCoopMod_Coop.log";
    const wchar_t* PREV = L"FF7RCoopMod_Coop_previous.log";
    const char* c = argc > 1 ? argv[1] : "";
    if (!strcmp(c, "setup_small")) { DeleteFileW(LOG); DeleteFileW(PREV); MakeFile(LOG, 8ll << 20); MakeFile(PREV, 1000); return 0; }
    if (!strcmp(c, "small"))        // 8 MB at launch: kept, appended to; an existing _previous is NOT deleted any more
    {
        CoopLog("hello %d", 1);
        Check(SizeOf(LOG) > (8ll << 20) && SizeOf(LOG) < (8ll << 20) + 4096, "an 8 MB log is kept at launch and appended to");
        Check(SizeOf(PREV) == 1024, "an existing _previous.log is kept at launch");
    }
    else if (!strcmp(c, "setup_big")) { DeleteFileW(LOG); DeleteFileW(PREV); MakeFile(LOG, 21ll << 20); return 0; }
    else if (!strcmp(c, "big"))     // 21 MB at launch: renamed to _previous, a fresh log starts with the LOG line
    {
        CoopLog("hello %d", 2);
        Check(SizeOf(PREV) >= (21ll << 20), "a 21 MB log at launch becomes _previous.log");
        Check(SizeOf(LOG) > 0 && SizeOf(LOG) < 4096, "a fresh log is started");
        Check(FirstLineHas(LOG, "LOG: the last log was over 20 MB - it is now FF7RCoopMod_Coop_previous.log"), "its first line says so");
    }
    else if (!strcmp(c, "setup_session")) { DeleteFileW(LOG); DeleteFileW(PREV); return 0; }
    else if (!strcmp(c, "session")) // write past 40 MB in one session: renamed to _previous, the new log says so
    {
        static char pad[3001];
        memset(pad, 'y', 3000);
        int i = 0;
        while (SizeOf(PREV) < 0 && i < 20000) CoopLog("line %d %s", i++, pad);
        Check(SizeOf(PREV) > (40ll << 20) && SizeOf(PREV) < (45ll << 20), "past 40 MB in a session the log becomes _previous.log");
        Check(FirstLineHas(LOG, "LOG: the log passed 40 MB - everything before this line is in FF7RCoopMod_Coop_previous.log"),
              "the new log's first line says so");
        CoopLog("after %d", i);
        Check(SizeOf(LOG) < (1ll << 20), "the new log keeps going");
    }
    else { printf("usage: unit_log.exe setup_small|small|setup_big|big|setup_session|session\n"); return 1; }
    if (bad) printf("FAILED %d\n", bad); else printf("ALL OK (%s)\n", c);
    return bad;
}
