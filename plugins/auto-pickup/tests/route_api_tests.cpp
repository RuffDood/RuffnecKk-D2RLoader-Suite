#include "auto_pickup_route_api.hpp"

#include <Windows.h>

#include <cstring>
#include <iostream>

using namespace RuffnecKk::AutoPickupRoute;

namespace {
int Failures{};
#define CHECK(expression) do { \
    if (!(expression)) { \
        std::cerr << __LINE__ << ": " #expression "\n"; \
        ++Failures; \
    } \
} while (false)

bool __cdecl Plan(const RequestV1*, PlanV1*) noexcept { return false; }
bool __cdecl Begin(const RequestV1*, const PlanV1*) noexcept { return false; }
void __cdecl End() noexcept {}
ProviderState __cdecl State() noexcept { return ProviderState::Ready; }
} // namespace

int main(int argc, char** argv) {
    int inventory{};
    int item{};
    RequestV1 request{
        sizeof(RequestV1), Version, &inventory, &item, 42,
        {1, 2, 0, 0}, 2, 1, 0,
    };
    CHECK(ValidRequest(&request));
    CHECK(!ValidRequest(nullptr));
    --request.structSize;
    CHECK(!ValidRequest(&request));
    ++request.structSize;
    request.beltColumns[1] = 1;
    CHECK(!ValidRequest(&request));
    request.beltColumns[1] = 2;
    request.allowInventory = 2;
    CHECK(!ValidRequest(&request));
    request.allowInventory = 1;

    PlanV1 plan{sizeof(PlanV1), Version, Destination::Belt, 3};
    CHECK(ValidPlan(&plan));
    plan.beltSlot = 16;
    CHECK(!ValidPlan(&plan));
    plan = {sizeof(PlanV1), Version, Destination::Inventory, -1};
    CHECK(ValidPlan(&plan));
    plan.beltSlot = 0;
    CHECK(!ValidPlan(&plan));

    ApiV1 api{sizeof(ApiV1), Version, State, Plan, Begin, End};
    CHECK(ValidApi(&api));
    ++api.version;
    CHECK(!ValidApi(&api));
    --api.version;
    api.begin = nullptr;
    CHECK(!ValidApi(&api));
    api.begin = Begin;
    api.state = nullptr;
    CHECK(!ValidApi(&api));

    if (argc == 2) {
        const auto module = LoadLibraryExA(argv[1], nullptr,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR
                | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        CHECK(module != nullptr);
        if (module) {
            const auto symbol = GetProcAddress(module, ExportName);
            CHECK(symbol != nullptr);
            if (symbol) {
                GetApiFn getApi{};
                static_assert(sizeof(symbol) == sizeof(getApi));
                std::memcpy(&getApi, &symbol, sizeof(symbol));
                CHECK(getApi(Version + 1, sizeof(ApiV1)) == nullptr);
                CHECK(getApi(Version, sizeof(ApiV1) - 1) == nullptr);
                // The ABI remains discoverable while inactive so consumers
                // can distinguish a disabled provider from an incompatible one.
                const auto* provider = getApi(Version, sizeof(ApiV1));
                CHECK(ValidApi(provider));
                if (provider) {
                    CHECK(provider->state() == ProviderState::Inactive);
                }
            }
            FreeLibrary(module);
        }
    }

    std::cout << "route advisor ABI tests: " << Failures << " failures\n";
    return Failures == 0 ? 0 : 1;
}
