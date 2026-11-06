// =================================================================================
// P2 TARGETING (soft target + R3 lock-on + right-stick switching)
// =================================================================================
// What we found in ff7remake_.exe:
//  - Every character in battle has a "battle unit" object at chara+0xCB0. Its current target lives
//    at unit+0x38/+0x40 - the same slot for the party leader and for AI party members.
//  - 0x96BFA0 UnitSetTarget(unit, targetChara) is the game's own setter. The AI's SetTarget
//    (AEndBattleAIController vtable +0xA18 -> 0x922310) is just a wrapper around it. It does nothing
//    while unit+0x310 != 0 (target is locked by an ability/combo in progress), which is what we want.
//  - 0x957CE0 GetEnemyMembers(TArray<AEndCharacter*>* out) fills the list of enemies in the
//    current battle. It needs the battle manager globals at 0x57E9720 / 0x57E99D0 to exist.
//    The array memory is the game's, freed with FMemory::Free (0x16C0370).
//  - 0x954C00 IsTargetable(chara) is the filter the game itself uses when building that list.
// With Barret's AI decisions off, nothing picks his target any more, so his attacks stop
// tracking. Every frame while player 2 is in, we pick the target for him:
//  - SOFT (default): nearest enemy, weighted toward the direction player 2 is pushing the left
//    stick (camera-relative), or the way Barret is facing when the stick is idle. A new candidate
//    must be clearly better than the current one before we switch, so it doesn't flicker.
//  - R3 on pad 2: toggle LOCK on the current target. While locked the target never changes.
//    Lock drops automatically if that enemy disappears/dies.
//  - Right stick flick left/right on pad 2: switch to the next enemy to the left/right
//    (and lock it, like the vanilla lock-on switch).
static constexpr uintptr_t RVA_UNIT_SET_TARGET   = 0x96BFA0;
static constexpr uintptr_t RVA_GET_ENEMY_MEMBERS = 0x957CE0;
static constexpr uintptr_t RVA_IS_TARGETABLE     = 0x954C00;
static constexpr uintptr_t RVA_FMEMORY_FREE      = 0x16C0370;
static constexpr uintptr_t RVA_BATTLE_GLOBAL_A   = 0x57E9720;
static constexpr uintptr_t RVA_BATTLE_GLOBAL_B   = 0x57E99D0;
// (OFF_BATTLE_UNIT = chara +0xCB0: core/02)
static constexpr size_t    OFF_UNIT_TARGET       = 0x38;
static constexpr size_t    OFF_UNIT_TARGETLOCK   = 0x310;
static constexpr size_t    OFF_ROOT_COMPONENT    = 0x160;  // AActor::RootComponent
static constexpr size_t    OFF_REL_LOCATION      = 0x17C;  // USceneComponent::RelativeLocation
static constexpr size_t    OFF_REL_ROTATION      = 0x188;  // USceneComponent::RelativeRotation (P,Y,R)
static constexpr size_t    OFF_CONTROL_ROTATION  = 0x3A8;  // AController::ControlRotation (P,Y,R)
static constexpr WORD      BTN_R3                = 0x0080;

typedef void (*UnitSetTarget_t)(void* unit, void* targetChara);
typedef void (*GetEnemyMembers_t)(void* outArray);
typedef bool (*IsTargetable_t)(void* chara);
typedef void (*FMemoryFree_t)(void* p);

struct TArrayRaw { void** data; int num; int max; };

// (the player's target state g_Tgt* / drawValid: in each player's record, core/02 PlayerSlot)
static int g_EnemyIdx[64], g_EnemySer[64], g_EnemyN = 0; // current battle's enemies (weak refs)

static void* ResolveWeak(int idx, int serial);              // defined further down
static void ResetP2Targeting() { g_Player->drawValid = false; g_Player->tgtLocked = false; g_Player->tgtIdx = -1; g_Player->tgtAppliedIdx = -1; g_Player->tgtLastEnemyCount = -1; }

