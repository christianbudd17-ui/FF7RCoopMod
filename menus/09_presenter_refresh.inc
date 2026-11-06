// =================================================================================
// Command HUD presenter refresh (kept from v14.7 OwnPalettes - the rest of that feature was removed in f1.15).
// The command manager (bcm) keeps the command-HUD "presenter" inline at bcm+0x38 (0x198 bytes; shortcut data
// refresh 0x13C4A80). When the game refreshes a presenter (shortcut palette data changed), the extra players'
// own command systems (BCM2, menus/10) are marked dirty so their pass refreshes theirs too (passes/12).
// =================================================================================
static constexpr uintptr_t RVA_PRES_REFRESH = 0x13C4A80;
static constexpr size_t PRES_OFF = 0x38;
typedef void (*PresFn_t)(void*);
static PresFn_t Orig_PresRefresh = nullptr;
static bool g_PresSelf = false;         // our own refresh calls (don't mark others)
static void* ResolveWeak(int idx, int serial);
static bool NameIs(void* obj, const char* want);
static bool MakeWeak(void* obj, int* outIndex, int* outSerial);
static void Hook_PresRefresh(void* p)
{
    if (!g_PresSelf && (!g_Player->ownBcm || (unsigned char*)p != g_Player->ownBcm + PRES_OFF)) g_Player->ownBcmDirty = true;
    if (!g_PresSelf && (!g_OtherPlayer->ownBcm || (unsigned char*)p != g_OtherPlayer->ownBcm + PRES_OFF)) g_OtherPlayer->ownBcmDirty = true;
    Orig_PresRefresh(p);
}
static void WidgetSetVis(void* w, unsigned char v)   // UWidget::SetVisibility (vtable +0x248, as the game calls it)
{
    if (!w) return;
    typedef void (*SetVis_t)(void*, unsigned char);
    ((SetVis_t)(*(void***)w)[0x248 / 8])(w, v);
}
