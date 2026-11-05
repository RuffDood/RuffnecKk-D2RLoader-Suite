#include "esr_startup_diagnostic.hpp"
#include <string>
#include <cstring>
#include <cstdio>
namespace {
std::string Logs;
bool Match = true;
void Log(const D2RL::PluginContext*, const char* text) noexcept { Logs += text; Logs += '\n'; }
bool Check(const D2RL::PluginContext*, uint64_t, const void*, uint32_t) noexcept { return Match; }
int Failures{};
#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); ++Failures; } } while(false)
}
int main() {
    D2RL::PluginApi api{};
    api.apiSize = sizeof(api); api.logInfo = Log; api.logWarn = Log; api.checkExpectedBytes = Check;
    std::array<std::uint8_t, 4> bytes{0x90,0xCC,0x11,0x22};
    constexpr std::array<std::uint8_t, 4> expected{0x90,0x90,0x11,0x22};
    D2RL::PluginContext context{};
    context.contextSize = sizeof(context); context.apiVersion = D2RL_PLUGIN_API_VERSION;
    context.api = &api; context.exeBase = reinterpret_cast<std::uintptr_t>(bytes.data());
    CHECK(RuffnecKk::MapSense::Detail::DiagnosticWitness(&context, "fixture", 0, expected));
    CHECK(Logs.find("admission=PASS") != std::string::npos);
    CHECK(Logs.find("observed=") == std::string::npos);
    Logs.clear(); Match = false;
    CHECK(!RuffnecKk::MapSense::Detail::DiagnosticWitness(&context, "fixture", 0, expected));
    CHECK(Logs.find("expected=90901122 observed=90CC1122 readable=yes") != std::string::npos);
    CHECK(Logs.find("ownership service unavailable") != std::string::npos);
    Logs.clear(); context.exeBase = 1;
    CHECK(!RuffnecKk::MapSense::Detail::DiagnosticWitness(&context, "unreadable", 0, expected));
    CHECK(Logs.find("observed=???????? readable=no") != std::string::npos);
    Logs.clear(); context.exeBase = UINTPTR_MAX;
    CHECK(!RuffnecKk::MapSense::Detail::DiagnosticWitness(&context, "overflow", 1, expected));
    CHECK(Logs.find("readable=no") != std::string::npos);
    std::printf("ESR diagnostic checks: %s\n", Failures == 0 ? "PASS" : "FAIL");
    return Failures == 0 ? 0 : 1;
}
