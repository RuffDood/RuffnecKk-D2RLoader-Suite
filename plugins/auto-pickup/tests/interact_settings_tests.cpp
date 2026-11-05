#include "../src/auto_interact/settings.hpp"
#include "../src/item_picker.hpp"
#include <iostream>
#include <cstdlib>
#define CHECK(x) do {if(!(x)){std::cerr<<__LINE__<<": " #x "\n";std::exit(1);}}while(0)
namespace P=RuffnecKk::AutoPickup;namespace A=AutoInteract;namespace S=Reimagined::InteractSettings;
int main() {
    const std::string pickup="# existing pickup\r\nenabled = false # pickup master\r\n[item_codes]\r\ninclude=[\"jew\"] # keep list\r\nexclude=[]\r\n[d2rl]\r\nmatch=['enabled', # keep metadata\r\n'item_codes.include',]\r\ncustom=42\r\n";
    P::Config root;std::string error,output,second;
    CHECK(P::ParseConfig(pickup,root,error) && !root.interact.enabled && !root.interactConfigured);
    CHECK(A::MigrateLegacy(pickup,"enabled=true\ninteract_range=20\nlocked_chests=true\ncorpses=false\ninterval_ms=250\nretry_ms=2000\ndiagnostics_enabled=false\n",output,error));
    CHECK(P::ParseConfig(output,root,error) && root.interactConfigured && root.interact.enabled && root.interact.interactRange==20 && root.interact.lockedChests && !root.interact.corpses && !root.interact.diagnostics);
    CHECK(!root.enabled && root.includeCodes==std::vector{P::PackItemCode("jew")});
    CHECK(output.find("# pickup master\r\n")!=std::string::npos && output.find("# keep metadata")!=std::string::npos && output.find("custom=42")!=std::string::npos);
    CHECK(output.find("auto_interact.interact_range")!=std::string::npos && output.find("auto_interact.locked_chests")!=std::string::npos);
    CHECK(A::MigrateLegacy(output,"bad garbage",second,error) && second==output); // already migrated: never overwrite
    const auto before=A::ValuesFor(root.interact);auto draft=before;draft.range=17;draft.flags&=~S::Enabled;
    A::Config applied;CHECK(A::SaveSettings(output,before,draft,second,applied,error));
    CHECK(P::ParseConfig(second,root,error) && !root.enabled && !root.interact.enabled && root.interact.interactRange==17 && root.interact.intervalMs==250 && !root.interact.diagnostics);
    CHECK(!A::SaveSettings(second,before,draft,output,applied,error));
    // Pickup saves must retain newer Auto Interact edits even from an old pickup draft.
    P::Config original;CHECK(P::ParseConfig(pickup,original,error));auto pickDraft=original;pickDraft.enabled=true;
    CHECK(P::Picker::SaveSettings(second,original,pickDraft,output,error));
    CHECK(P::ParseConfig(output,root,error) && root.enabled && !root.interact.enabled && root.interact.interactRange==17);
    CHECK(!P::ParseConfig("[auto_interact]\ninteract_range=21",root,error));
    CHECK(!P::ParseConfig("[auto_interact]\nenabled=true\nenabled=false",root,error));
    CHECK(!P::ParseConfig("[auto_interact]\n[auto_interact]",root,error));
    CHECK(!A::MigrateLegacy(pickup,"unknown=true",output,error));
    CHECK(A::MigrateLegacy("enabled=false", "interact_range=20",output,error));
    CHECK(P::ParseConfig(output,root,error) && root.interact.interactRange==20);
    std::cout<<"Unified section, migration idempotence, range bounds, independent saves and comment/match preservation passed\n";
}
