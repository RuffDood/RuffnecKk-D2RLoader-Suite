#include "../src/plugin.cpp"
#include "loader_fixture.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
namespace {
int failures{};
#define CHECK(x) do { if (!(x)) { std::cerr << "Failed line " << __LINE__ << ": " #x "\n"; ++failures; } } while (false)
}
namespace {
namespace Stat = RuffnecKk::NativeStatCompat;
std::vector<std::uint8_t> Image;
// Independent live capture, build 93847: RVA 0x34B320..0x34B33B,
// unwind RVA 0x20E92A0. Keep separate from production admission constants.
constexpr auto CapturedGoldCap = std::to_array<std::uint8_t>({
    0x48, 0x83, 0xEC, 0x28, 0x45, 0x33, 0xC0, 0x41, 0x8D,
    0x50, 0x0C, 0xE8, 0xF0, 0x9C, 0xFA, 0xFF, 0x69, 0xC0,
    0x10, 0x27, 0x00, 0x00, 0x48, 0x83, 0xC4, 0x28, 0xC3,
});
auto Nibble(char c) -> std::uint8_t { return static_cast<std::uint8_t>(c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10); }
void ResetImage(std::uint32_t multiplier = 10000) {
    Image.assign(0x350000, 0);
    IMAGE_DOS_HEADER dos{}; dos.e_magic = IMAGE_DOS_SIGNATURE; dos.e_lfanew = 0x80;
    std::memcpy(Image.data(), &dos, sizeof(dos));
    IMAGE_NT_HEADERS64 nt{}; nt.Signature = IMAGE_NT_SIGNATURE;
    nt.OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
    nt.OptionalHeader.SizeOfImage = static_cast<DWORD>(Image.size());
    std::memcpy(Image.data() + dos.e_lfanew, &nt, sizeof(nt));
    const auto& helper = Stat::Loader130StatAdmissionContract().helpers[0];
    for (std::size_t i = 0; i < helper.canonicalExpectedHex.size() / 2; ++i)
        Image[helper.nativeRva + i] = static_cast<std::uint8_t>((Nibble(helper.canonicalExpectedHex[i * 2]) << 4) | Nibble(helper.canonicalExpectedHex[i * 2 + 1]));
    std::memcpy(Image.data() + 0x34B320, CapturedGoldCap.data(), CapturedGoldCap.size());
    std::memcpy(Image.data() + 0x34B332, &multiplier, sizeof(multiplier));
}
auto __cdecl CheckImage(const D2RL::PluginContext* context, std::uint64_t rva, const void* expected, std::uint32_t size) noexcept -> bool {
    if (!LoaderFixture::Check(context, rva, expected, size)) return false;
    if (rva == Reimagined::EmbeddedTeleportPickup::GetInventoryGoldLimitRva || rva == Reimagined::EmbeddedTeleportPickup::GetInventoryGoldLimitRva + Reimagined::EmbeddedTeleportPickup::GoldLimitSuffixOffset)
        return rva + size <= Image.size() && std::memcmp(Image.data() + rva, expected, size) == 0;
    return true;
}
auto Setup(const std::string& text, bool compatible) -> const D2RL::PluginContext* {
    ResetImage();
    LoaderFixture::Setup(text, compatible);
    LoaderFixture::Context.exeBase = reinterpret_cast<std::uintptr_t>(Image.data());
    LoaderFixture::Api.checkExpectedBytes = CheckImage;
    return &LoaderFixture::Context;
}
std::uint64_t rejectedPotionWitness{};
bool __cdecl CheckPotionWitness(const D2RL::PluginContext* c,std::uint64_t rva,const void* bytes,std::uint32_t size) noexcept {
    return rva!=rejectedPotionWitness && CheckImage(c,rva,bytes,size);
}
std::map<std::uint64_t, std::uint64_t> slots;
std::map<std::uint64_t, std::string> owners;
bool __cdecl CheckOwned(const D2RL::PluginContext* context, std::uint64_t rva, const void* expected, std::uint32_t size) noexcept {
    const auto it = slots.find(rva);
    if (it != slots.end()) return size == 8 && std::memcmp(&it->second, expected, 8) == 0;
    return CheckImage(context, rva, expected, size);
}
bool __cdecl PatchOwned(const D2RL::PluginContext* context, std::uint64_t rva, const void* expected, std::uint32_t size, std::uint64_t value, std::uint32_t width) noexcept {
    if (!CheckOwned(context, rva, expected, size) || width != 8 || !owners[rva].empty()) return false;
    slots[rva] = value; owners[rva] = context->pluginId;
    return LoaderFixture::Patch(context, rva, expected, size, value, width);
}
bool __cdecl ReadOwned(const D2RL::PluginContext* context, char* out, std::uint32_t size, std::uint32_t* needed) noexcept {
    if (std::string_view(context->pluginId) != "teleport-auto-pickup") return LoaderFixture::Read(context, out, size, needed);
    constexpr auto text = "enabled=true\nvalidate_only=false\ngold=true\ninclude_item_plugin=true\n";
    *needed = static_cast<std::uint32_t>(std::strlen(text) + 1);
    if (size < *needed) return false;
    std::memcpy(out, text, *needed); return true;
}
void Coexist(const std::string& text, const char* legacyDll, const char* pickitDll) {
    using namespace RuffnecKk::AutoPickup;
    namespace Post = Reimagined::EmbeddedTeleportPickup;
    const auto directory = std::filesystem::temp_directory_path() / ("d2r-pickit-coexist-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
    CHECK(std::filesystem::create_directory(directory));
    const auto sibling = directory / "d2rl-teleport-auto-pickup.dll";
    if (legacyDll) std::filesystem::copy_file(std::filesystem::absolute(legacyDll), sibling);
    else { std::ofstream reserve(sibling); reserve << "standalone reservation fixture"; }
    const auto pickitSibling = directory / "d2rl-auto-pickup-anything.dll";
    if (pickitDll) std::filesystem::copy_file(std::filesystem::absolute(pickitDll), pickitSibling);
    const auto image = LoadLibraryExW(sibling.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    const bool cooperative = image && Reimagined::PickupCoexist::Supports(image, Reimagined::PickupCoexist::Role::YieldingTeleport);
    if (image) FreeLibrary(image);
    for (bool standaloneFirst : {false, true}) {
        auto* context = Setup(text, true);
        const auto folder = directory.wstring();
        LoaderFixture::Context.pluginDirectory = folder.c_str(); LoaderFixture::Context.pluginId = "auto-pickup-anything";
        LoaderFixture::Api.checkExpectedBytes = CheckOwned; LoaderFixture::Api.patchWrite = PatchOwned; LoaderFixture::Api.readConfig = ReadOwned;
        slots.clear(); owners.clear();
        for (unsigned opcode = 1; opcode <= 18; ++opcode)
            slots[ServerPacketTableRva + opcode * 8] = context->exeBase + Post::TriggerHandlerRvas[opcode];
        auto standalone = *context; standalone.pluginId = "teleport-auto-pickup";
        HMODULE module{};
        using Load = bool(__cdecl*)(const D2RL::PluginContext*) noexcept;
        using Unload = void(__cdecl*)() noexcept;
        Unload unload{};
        auto startStandalone = [&] {
            if (legacyDll) {
                module = LoadLibraryW(sibling.c_str()); CHECK(module);
                const auto load = module ? reinterpret_cast<Load>(GetProcAddress(module, "D2RLoaderLoadPlugin")) : nullptr;
                unload = module ? reinterpret_cast<Unload>(GetProcAddress(module, "D2RLoaderUnloadPlugin")) : nullptr;
                CHECK(load && unload); if (load) CHECK(load(&standalone));
            } else for (unsigned opcode = 5; opcode <= 18; ++opcode) {
                const auto rva = ServerPacketTableRva + opcode * 8;
                const auto expected = context->exeBase + Post::TriggerHandlerRvas[opcode];
                CHECK(PatchOwned(&standalone, rva, &expected, 8, 0x12340000 + opcode, 8));
            }
        };
        if (standaloneFirst) startStandalone();
        CHECK(D2RLoaderLoadPlugin(context));
        CHECK(StandaloneTeleport == !cooperative && EmbeddedTeleportReady == cooperative);
        if (!standaloneFirst) startStandalone();
        CHECK(LoaderFixture::Patches == 18);
        if (cooperative) {
            auto config = SnapshotSettings(); config.teleportRange = 9; config.teleportGold = false; ApplyItemSelection(config);
            CHECK(Post::Settings.pickupRange == 9 && !Post::Settings.gold);
            config.enabled = false; ApplyItemSelection(config); CHECK(!Post::Settings.enabled);
        }
        for (unsigned opcode = 1; opcode <= 18; ++opcode)
            CHECK(owners[ServerPacketTableRva + opcode * 8] == (cooperative || opcode <= 4 ? "auto-pickup-anything" : "teleport-auto-pickup"));
        D2RLoaderUnloadPlugin(); if (unload) unload(); if (module) FreeLibrary(module);
    }
    if (pickitDll) CHECK(std::filesystem::remove(pickitSibling));
    CHECK(std::filesystem::remove(sibling)); CHECK(std::filesystem::remove(directory));
}
}
int main(int argc, char** argv) {
    using namespace RuffnecKk::AutoPickup;
    if (argc != 2 && argc != 3 && argc != 4) return 2;
    std::ifstream input(argv[1]);
    const std::string text{std::istreambuf_iterator<char>(input), {}};
    auto* context = Setup(text, true);
    CHECK(D2RLoaderLoadPlugin(context));
    CHECK(!Settings.validateOnly && Settings.enabled);
    CHECK(LoaderFixture::Patches == 18 && LoaderFixture::Checks > 10);
    CHECK(LoaderFixture::InlineHooks == 1); // One scoped potion belt hook.
    CHECK(LoaderFixture::Listeners == 6);
    CHECK(!D2RL::HasFlag(D2RLoaderGetPluginInfo()->flags, D2RL::PluginFlags::ModScopedOnly));
    D2RLoaderUnloadPlugin();
    CHECK(LoaderFixture::Unsubscribed == 6 && !RetryLifecycle);
    auto disabled = text;
    const auto enabledAt = disabled.find("enabled = true");
    CHECK(enabledAt != std::string::npos);
    if (enabledAt != std::string::npos) disabled.replace(enabledAt, 14, "enabled = false");
    context = Setup(disabled, true); CHECK(D2RLoaderLoadPlugin(context));
    CHECK(!Settings.enabled && EmbeddedTeleportReady && LoaderFixture::Patches == 18);
    auto enabledConfig = SnapshotSettings(); enabledConfig.enabled = true; ApplyItemSelection(enabledConfig);
    CHECK(Settings.enabled && Reimagined::EmbeddedTeleportPickup::Settings.enabled);
    enabledConfig.enabled = false; ApplyItemSelection(enabledConfig);
    CHECK(!Settings.enabled && !Reimagined::EmbeddedTeleportPickup::Settings.enabled);
    D2RLoaderUnloadPlugin();
    Coexist(text, argc >= 3 ? argv[2] : nullptr, argc >= 4 ? argv[3] : nullptr);
    auto custom = text;
    const auto includeAt = custom.find("\ninclude = []"), excludeAt = custom.find("\nexclude = []");
    CHECK(includeAt != std::string::npos && excludeAt != std::string::npos);
    // Replace from the end to preserve the preceding offset.
    if (excludeAt != std::string::npos) custom.replace(excludeAt, 13, "\nexclude = [\"r01\"]");
    if (includeAt != std::string::npos) custom.replace(includeAt, 13, "\ninclude = [\"jew\"]");
    context = Setup(custom, true);
    CHECK(D2RLoaderLoadPlugin(context));
    CHECK(Settings.Selects(PackItemCode("jew")) && !Settings.Selects(PackItemCode("r01")));
    CHECK(LoaderFixture::Patches == 18);
    D2RLoaderUnloadPlugin();
    // Out-of-box activation must not bypass build/signature admission.
    context = Setup(text, false);
    CHECK(!D2RLoaderLoadPlugin(context));
    CHECK(LoaderFixture::Patches == 0 && LoaderFixture::InlineHooks == 0);
    D2RLoaderUnloadPlugin();
    auto diagnostic = text;
    const auto p = diagnostic.find("validate_only = false");
    CHECK(p != std::string::npos);
    if (p != std::string::npos) diagnostic.replace(p, 21, "validate_only = true");
    context = Setup(diagnostic, true);
    CHECK(D2RLoaderLoadPlugin(context));
    CHECK(Settings.validateOnly && LoaderFixture::Patches == 0 && LoaderFixture::Checks > 10);
    CHECK(LoaderFixture::Listeners == 0 && !ScanApiReady);
    D2RLoaderUnloadPlugin();
    context = Setup(text, true); LoaderFixture::LifecycleAvailable = false;
    CHECK(!D2RLoaderLoadPlugin(context));
    CHECK(LoaderFixture::Patches == 0 && !ScanApiReady);
    D2RLoaderUnloadPlugin();
    context = Setup(text, true); LoaderFixture::FailListener = 2;
    CHECK(!D2RLoaderLoadPlugin(context));
    CHECK(LoaderFixture::Patches == 0 && !ScanApiReady && LoaderFixture::Unsubscribed == 1);
    D2RLoaderUnloadPlugin();
    for (const auto offset : {std::uintptr_t{0x34B320}, std::uintptr_t{0x34B336}, std::uintptr_t{0x2F5020}}) {
        context = Setup(text, true); Image[offset] ^= 0x01;
        CHECK(!D2RLoaderLoadPlugin(context) && LoaderFixture::Patches == 0);
        D2RLoaderUnloadPlugin();
    }
    for(const auto rva:{std::uint64_t{0x3862D0},std::uint64_t{0x38B070},std::uint64_t{0x38B108},std::uint64_t{0x38643D},std::uint64_t{0x3864A7},std::uint64_t{0x34A360},std::uint64_t{0x349720}}) {
        context=Setup(text,true);rejectedPotionWitness=rva;
        LoaderFixture::Api.checkExpectedBytes=CheckPotionWitness;
        CHECK(!D2RLoaderLoadPlugin(context));
        CHECK(LoaderFixture::Patches==0 && LoaderFixture::InlineHooks==0);
        D2RLoaderUnloadPlugin();
    }
    rejectedPotionWitness=0;
    std::cout << "Default config activates unchanged; incompatible native fingerprint blocks patches; diagnostic-only mode remains optional; failures: " << failures << '\n';
    return failures ? 1 : 0;
}
