// Exercise the real delayed callback with a native-constructor contract stub.
#include "plugin.cpp"

namespace {
struct TestUnit { std::int32_t type, classId, guid; };
TestUnit doll{1, 216, 42}, carrier{3, 587, 43}, visual{3, 117, 44};
int originalCalls{}, constructorCalls{};
int damageCalls{}, cleanupCalls{};
std::int32_t currentFrames = 1, serverResult = 2;
bool damageArgumentsValid{true};
bool sourceAssertion{};
void* observedOwner{};
void* lookupOwner = &doll;
int lookupCalls{};
bool revived{};
bool lookupArgumentsValid{true};
auto __fastcall Lookup(void*, std::int32_t type, std::int32_t guid) noexcept -> void* {
    ++lookupCalls;
    lookupArgumentsValid = type == 1 && guid == 42;
    return lookupOwner;
}
auto __fastcall State(void*, std::int32_t state) noexcept -> std::int32_t {
    return state == ReviveStateId && revived ? 1 : 0;
}
auto __fastcall UnitType(void* unit) noexcept -> std::int32_t {
    return static_cast<TestUnit*>(unit)->type;
}
auto __fastcall UnitId(void* unit) noexcept -> std::int32_t {
    return static_cast<TestUnit*>(unit)->guid;
}
auto __fastcall Frames(void*) noexcept -> std::int32_t { return currentFrames; }
auto __fastcall Original(void*, void*) noexcept -> std::int32_t {
    ++originalCalls;
    return serverResult;
}
auto __fastcall CastId(void*) noexcept -> std::uint32_t { return 1; }
auto __fastcall Constructor(void*, std::int32_t, void* owner,
        std::int32_t, std::int32_t, std::int32_t, std::int32_t,
        std::int32_t, std::int32_t, std::int32_t, std::int32_t) noexcept -> void* {
    ++constructorCalls;
    observedOwner = owner;
    sourceAssertion = UnitType(owner) != 0 && UnitType(owner) != 1;
    return sourceAssertion ? nullptr : &visual;
}
auto __fastcall Damage(void*, void* source, std::int32_t x, std::int32_t y,
        std::int32_t radius, void*, std::int32_t, std::int32_t, void*,
        std::uint32_t flags) noexcept -> std::int32_t {
    ++damageCalls;
    damageArgumentsValid = source == &carrier && x == 10 && y == 20
        && radius == 4 && flags == CorpseExplosionFlags;
    return 1;
}
void __fastcall Cleanup(void*) noexcept { ++cleanupCalls; }
}

auto RunDelayedCase(bool shouldConstruct, std::int32_t frames = 1,
        std::int32_t originalResult = 2) -> bool {
    ResetState();
    originalCalls = constructorCalls = lookupCalls = 0;
    damageCalls = cleanupCalls = 0;
    damageArgumentsValid = true;
    currentFrames = frames;
    serverResult = originalResult;
    sourceAssertion = false;
    observedOwner = nullptr;
    Operational = true;
    GetUnitType = UnitType;
    GetUnitId = UnitId;
    GetMissileCurrentFrames = Frames;
    OriginalServerDoOne = Original;
    NextGameCounter = CastId;
    CreateSkillMissile = Constructor;
    GetServerUnit = Lookup;
    CheckState = State;
    ApplyAreaDamage = Damage;
    DestroyDamage = Cleanup;
    int game{};
    CarrierSlot tracked{.game = &game, .missile = &carrier, .guid = 43,
        .ownerGuid = 42, .ownerClassId = 216,
        .x = 10, .y = 20, .damage = 100, .radius = 4, .active = true};
    (void)AddCarrier(tracked);
    const auto result = HookServerDoOne(&game, &carrier);
    if (sourceAssertion) {
        std::puts("FAIL: BC_ASSERT: UnitGetType(ptCreate->hSource) == UNIT_PLAYER || UnitGetType(ptCreate->hSource) == UNIT_MONSTER");
        return false;
    }
    if (originalCalls != 1 || result != originalResult || !lookupArgumentsValid
            || constructorCalls != (shouldConstruct ? 1 : 0)
            || (shouldConstruct && observedOwner != &doll)
            || !damageArgumentsValid || damageCalls != constructorCalls
            || cleanupCalls != damageCalls
            || CompletedExplosions != (shouldConstruct ? 1 : 0)
            || (!shouldConstruct && originalResult == 2 && FailedExplosions != 1)) {
        std::puts("FAIL: delayed callback must construct the visual with the original Doll owner");
        return false;
    }
    CarrierSlot retired{};
    if (FindCarrier(&game, &carrier, retired) != (originalResult != 2)) return false;
    return true;
}

int main() {
    // A witness-only executable page permits the real cleanup admission path.
    // Actual native calls remain stubs; no game process is used.
    ImageSize = native::DestroyDamageRva + native::DestroyDamageExpected.size();
    Base = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, ImageSize,
        MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!Base) return 1;
    std::memcpy(Base + native::DestroyDamageRva, native::DestroyDamageExpected.data(),
        native::DestroyDamageExpected.size());
    if (!RunDelayedCase(true)) return 1;
    if (!RunDelayedCase(false, 10)) return 1;
    if (!RunDelayedCase(false, 1, 0)) return 1;
    lookupOwner = nullptr;
    if (!RunDelayedCase(false)) return 1;
    lookupOwner = &carrier;
    if (!RunDelayedCase(false)) return 1;
    lookupOwner = &doll;
    doll.guid = 99;
    if (!RunDelayedCase(false)) return 1;
    doll.guid = 42;
    doll.classId = 777;
    if (!RunDelayedCase(false)) return 1;
    doll.classId = 216;
    revived = true;
    if (!RunDelayedCase(false)) return 1;
    revived = false;
    void* created{};
    int game{};
    const auto calls = constructorCalls;
    if (TryCreateMissile(&game, 117, &carrier, 10, 20, created)
            != NativeAttempt::Rejected || constructorCalls != calls) return 1;
    std::puts("PASS: delayed callback uses a monster source; original callback runs once");
    std::puts("PASS: missing, missile, wrong GUID/class and revived owners fail closed; missile sources never reach native constructor");
    std::puts("PASS: damage retains carrier source, exact position/radius, once-only native cleanup and dispatcher ownership");
    VirtualFree(Base, 0, MEM_RELEASE);
    Base = nullptr;
}
