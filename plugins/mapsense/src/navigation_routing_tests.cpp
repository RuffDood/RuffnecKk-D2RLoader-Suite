#include "navigation_routing.hpp"
#include "navigation_policy.hpp"
#include "mapsense_config.hpp"

#include <array>
#include <iostream>
#include <atomic>
#include <thread>

using namespace RuffnecKk::MapSense;
namespace {
int failures{};
int checks{};
void Check(bool passed, const char* label) {
    ++checks;
    if (!passed) { ++failures; std::cerr << "FAIL: " << label << '\n'; }
}
}

void TestShortcut(int from, int to, NavigationLineKind kind, bool town = false) {
    NavigationRoutingOptions options{};
    options.townShortcuts = town;
    const auto plan = BuildAdaptiveNavigationRoutePlan(from, std::array{to}, options, town);
    std::array<int, 16> preparation{};
    const auto prepared = BuildAdaptiveNavigationPreparationTargets(plan, {}, preparation);
    Check(HasNavigationRouteTarget(std::span(preparation).first(prepared), to), "shortcut prepares actual destination");
    const std::array exits{NavigationExitCandidate{.destinationId=1, .targetLevelId=to, .subtileX=150, .subtileY=250}};
    std::array<NavigationSubtileDestination, 8> output{};
    const auto count = BuildNavigationDestinations({.currentLevelId=from, .inTown=town, .exits=exits, .routePlan=&plan}, output);
    Check(count==1 && output[0].kind==kind, "shortcut renders the intended line family");
    Check(EvaluateNavigationResolutionCompleteness(from, exits, &plan)==NavigationResolutionCompleteness::Complete,
        "resolved shortcut never retries a skipped floor");
    Check(BuildNavigationDestinations({.currentLevelId=from, .inTown=town, .routePlan=&plan}, output)==0,
        "a configured connection without physical coordinates draws no line");
    Check(EvaluateNavigationResolutionCompleteness(from, {}, &plan)==NavigationResolutionCompleteness::PartialRetryable,
        "lazy shortcut exit stays retryable until observed");
}

void TestRoutingPublication() {
    NavigationRoutingOptions options{};
    options.overrides.push_back({129,131,NavigationLineKind::Progression});
    Check(PublishNavigationRoutingOptions(options), "publish initial immutable routing options");
    const auto retained = AcquireNavigationRoutingOptions();
    options.adaptToModConnections=false; options.townShortcuts=true;
    options.overrides[0].toLevelId=300;
    Check(PublishNavigationRoutingOptions(options), "publish changed routing options");
    const auto changed = AcquireNavigationRoutingOptions();
    Check(retained && retained->adaptToModConnections && !retained->townShortcuts
        && retained->overrides[0].toLevelId==131, "in-flight refresh retains its complete original snapshot");
    Check(changed && !changed->adaptToModConnections && changed->townShortcuts
        && changed->overrides[0].toLevelId==300, "next refresh receives all newly published settings");
    std::atomic_bool done{};
    std::atomic_bool consistent{true};
    std::thread reader([&] {
        while (!done.load(std::memory_order_acquire)) {
            const auto snapshot=AcquireNavigationRoutingOptions();
            if (!snapshot || snapshot->overrides.size()!=1
                    || snapshot->adaptToModConnections==snapshot->townShortcuts
                    || snapshot->overrides[0].toLevelId!=(snapshot->townShortcuts?300:131))
                consistent.store(false,std::memory_order_relaxed);
        }
    });
    bool published=true;
    for (int index=0; index<2000; ++index) {
        options.townShortcuts=index%2==0;
        options.adaptToModConnections=!options.townShortcuts;
        options.overrides[0].toLevelId=options.townShortcuts?300:131;
        published=PublishNavigationRoutingOptions(options)&&published;
    }
    done.store(true,std::memory_order_release); reader.join();
    Check(published && consistent.load(), "concurrent refresh reads only coherent published snapshots");
}

void TestConfig() {
    const auto legacy = ParseConfig("schema_version = 20");
    Check(legacy.navigation.routing.adaptToModConnections && !legacy.navigation.routing.townShortcuts,
        "legacy configs migrate to automatic routing with towns disabled");
    const auto config = ParseConfig(R"(schema_version = 21
[navigation.routing]
adapt_to_mod_connections = false
town_shortcuts = true
overrides = [{ from_level_id = 75, to_level_id = 83, kind = "progression" },
             { from_level_id = 43, to_level_id = 64, kind = "quest" }]
)");
    const auto restored = ParseConfig(SerializeConfig(config));
    Check(!restored.navigation.routing.adaptToModConnections && restored.navigation.routing.townShortcuts
        && restored.navigation.routing.overrides.size()==2, "routing config round trip");
    const auto merged = ParseConfig(SerializeMenuSettingsForSave(SerializeConfig(legacy), SerializeConfig(config)));
    Check(merged.navigation.routing.overrides.size()==2 && merged.navigation.routing.adaptToModConnections,
        "menu save preserves manually edited overrides and menu toggles");
    Check(ParseConfig(SerializeMenuSettingsForSave(SerializeConfig(config), SerializeConfig(legacy))).navigation.routing.overrides.empty(),
        "menu save preserves a manually cleared route list");
    for (const auto invalid : {
            "overrides = 1", "overrides = [{}]",
            "overrides = [{from_level_id=75,to_level_id=75,kind='progression'}]",
            "overrides = [{from_level_id=75,to_level_id=83,kind='waypoint'}]",
            "overrides = [{from_level_id=75,to_level_id=83,kind='progression',typo=true}]",
            "overrides = [{from_level_id=75,to_level_id=83,kind='progression'},{from_level_id=75,to_level_id=76,kind='progression'}]",
            "overrides = [{from_level_id=75.0,to_level_id=83,kind='progression'}]",
            "overrides = [{from_level_id=75,to_level_id=65536,kind='progression'}]",
            "town_shortcuts = 1", "adapt_to_mod_connections = 'true'"}) {
        bool rejected{};
        try { (void)ParseConfig(std::string("schema_version=21\n[navigation.routing]\n")+invalid); }
        catch(const std::exception&) { rejected=true; }
        Check(rejected, "invalid route configuration is rejected");
    }
}

auto Project(void*, std::int32_t x, std::int32_t y, NavigationNativePoint& output) noexcept -> bool {
    output={x,y}; return true;
}

void TestTownProjection() {
    InitializeNavigationEngine();
    ResetNavigationSession(1);
    ResetNavigationLevel(1, 75);
    NavigationSubtileDestination destination{.destinationId=1,.subtileX=150,.subtileY=250,.kind=NavigationLineKind::Progression};
    NavigationNativePoint client{};
    Check(ConvertNavigationSubtileToClientCoordinates(10,10,client), "town player origin conversion");
    NavigationAutomapPass pass{.currentLevelId=75,.inTown=true,.playerClientX=client.x,.playerClientY=client.y,
        .hasPlayerSubtile=true,.playerSubtile={10,10},.nativeWidth=4000,.nativeHeight=4000,
        .clipLeft=-2000,.clipTop=-2000,.clipWidth=4000,.clipHeight=4000,.projectClient=&Project,.borrowedAutomapContext=&client};
    Check(PublishNavigationDestinations(1,75,&destination,1,true), "publish supported town shortcut");
    Check(ObserveNavigationAutomapPass(pass)==NavigationAutomapObservationResult::Ignored, "town lines are disabled by default");
    SetNavigationTownShortcutsEnabled(true);
    Check(PublishNavigationDestinations(1,75,&destination,1,true), "republish town shortcut after enabling");
    Check(ObserveNavigationAutomapPass(pass)==NavigationAutomapObservationResult::Projected, "enabled town shortcut reaches renderer projection");
    SetNavigationTownShortcutsEnabled(false);
    Check(ObserveNavigationAutomapPass(pass)==NavigationAutomapObservationResult::Ignored, "disabling towns clears projected shortcut");
    SetNavigationTownShortcutsEnabled(true);
    Check(PublishNavigationDestinations(1,75,&destination,1), "publish ordinary town route");
    Check(ObserveNavigationAutomapPass(pass)==NavigationAutomapObservationResult::Ignored, "ordinary town exits stay suppressed");
    destination.kind=NavigationLineKind::Waypoint;
    Check(!PublishNavigationDestinations(1,75,&destination,1,true), "town shortcut batches reject unrelated line families");
    Check(ShouldRequestNavigationRefresh(NavigationAutomapObservationResult::LevelChanged,true,true), "enabled town entry requests destination discovery");
    Check(!ShouldRequestNavigationRefresh(NavigationAutomapObservationResult::LevelChanged,true,false), "disabled town entry preserves legacy refresh policy");
    ShutdownNavigationEngine();
}

int main() {
    const auto plan = BuildAdaptiveNavigationRoutePlan(129,
        std::array<std::int32_t, 2>{128, 131}, NavigationRoutingOptions{}, false);
    Check(HasNavigationRouteTarget(std::span(plan.progression.data(), plan.progressionCount), 131),
        "WSK2 prepares the mod's direct Throne destination");
    const std::array exits{NavigationExitCandidate{
        .destinationId = 1, .targetLevelId = 131, .subtileX = 150, .subtileY = 250}};
    std::array<NavigationSubtileDestination, 8> destinations{};
    const auto count = BuildNavigationDestinations(NavigationPolicyInput{
        .currentLevelId = 129, .exits = exits, .routePlan = &plan}, destinations);
    Check(count == 1 && destinations[0].kind == NavigationLineKind::Progression,
        "WSK2 draws a green line at the verified Throne entrance");
    Check(count == 1 && destinations[0].subtileX == 150 && destinations[0].subtileY == 250,
        "shortcut line preserves observed exit coordinates");
    // Exact forward links written by QoL Map & Waypoint 9.1 option blocks.
    for (const auto [from,to] : std::array<std::array<int,2>,6>{{{4,5},{28,32},{33,35},{35,37},{83,101},{129,131}}})
        TestShortcut(from,to,NavigationLineKind::Progression);
    for (const auto [from,to] : std::array<std::array<int,2>,3>{{{20,25},{43,64},{78,91}}})
        TestShortcut(from,to,NavigationLineKind::Quest);
    TestShortcut(75,83,NavigationLineKind::Progression,true);
    TestShortcut(40,74,NavigationLineKind::Progression,true);
    TestShortcut(40,49,NavigationLineKind::Quest,true);
    TestShortcut(103,107,NavigationLineKind::Progression,true);
    TestShortcut(113,118,NavigationLineKind::Progression);
    TestShortcut(120,129,NavigationLineKind::Progression);
    auto options=NavigationRoutingOptions{};
    auto normal=BuildAdaptiveNavigationRoutePlan(129,std::array{128,130},options,false);
    Check(normal.progressionCount==1 && normal.progression[0]==130, "vanilla WSK2 remains unchanged");
    auto forest=BuildAdaptiveNavigationRoutePlan(76,std::array{77,78,85},options,false);
    Check(forest.progressionCount==1 && forest.progression[0]==78 && forest.questCount==1 && forest.quests[0]==85,
        "jungle campaign bypass and Spider Cavern remain separate");
    auto marsh=BuildAdaptiveNavigationRoutePlan(77,std::array{76},options,false);
    Check(marsh.progressionCount==1 && marsh.progression[0]==76, "Great Marsh dead end keeps the proven return route");
    auto bazaar=BuildAdaptiveNavigationRoutePlan(80,std::array{81,93,94},options,false);
    Check(bazaar.questCount==2, "both independent Bazaar quest routes survive adaptation");
    auto unrelated=BuildAdaptiveNavigationRoutePlan(3,std::array{4,9,17,300},options,false);
    Check(unrelated.progressionCount==1 && unrelated.progression[0]==4 && unrelated.questCount==1 && unrelated.quests[0]==17,
        "optional Cave and unknown IDs never become green shortcuts");
    auto gated=BuildAdaptiveNavigationRoutePlan(75,std::array{104},options,false);
    Check(gated.progressionCount==0, "inference never crosses a quest-gated Act transition");
    auto ambiguous=BuildAdaptiveNavigationRoutePlan(42,std::array{44,61},options,false);
    Check(!ambiguous.ambiguousProgression && ambiguous.progression[0]==61, "same-chain destinations choose furthest forward");
    options.overrides.push_back({129,300,NavigationLineKind::Progression});
    auto custom=BuildAdaptiveNavigationRoutePlan(129,std::array{300,131},options,false);
    Check(custom.progressionCount==1 && custom.progression[0]==300, "explicit override resolves new mod destination");
    auto disconnected=BuildAdaptiveNavigationRoutePlan(129,std::array{131},options,false);
    Check(disconnected.progressionCount==0, "disconnected override cannot fall back or fabricate an exit");
    options.overrides.clear(); options.adaptToModConnections=false;
    auto disabled=BuildAdaptiveNavigationRoutePlan(129,std::array{131},options,false);
    Check(disabled.progressionCount==1 && disabled.progression[0]==130, "automatic adaptation is configurable");
    options=NavigationRoutingOptions{};
    auto town=BuildAdaptiveNavigationRoutePlan(75,std::array{76,83},options,true);
    Check(!town.allowTownShortcut, "town shortcut is opt in");
    options.townShortcuts=true;
    town=BuildAdaptiveNavigationRoutePlan(75,std::array{76,83},options,true);
    Check(town.allowTownShortcut && town.progression[0]==83, "Docks shortcut wins over regular Forest exit");
    auto ordinaryTown=BuildAdaptiveNavigationRoutePlan(75,std::array{76},options,true);
    Check(!ordinaryTown.allowTownShortcut, "town option permits only supported shortcuts");
    auto portal=BuildAdaptiveNavigationRoutePlan(131,{},options,false);
    Check(portal.progressionCount==1 && portal.progression[0]==132 && !portal.progressionRequiresExit,
        "Baal portal keeps opportunistic native ownership");
    Check(EvaluateNavigationResolutionCompleteness(131,{},&portal)==NavigationResolutionCompleteness::Complete,
        "missing dynamic portal does not create endless retries");
    TestRoutingPublication();
    TestConfig();
    TestTownProjection();
    std::cout << checks << " checks; ";
    std::cout << "Adaptive routing: " << (failures ? "FAIL" : "PASS") << '\n';
    return failures ? 1 : 0;
}
