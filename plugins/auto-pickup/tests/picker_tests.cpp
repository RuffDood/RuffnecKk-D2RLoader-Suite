#include "item_picker.hpp"
#include "picker_catalog.hpp"
#include "picker_layout.hpp"
#include "picker_sdk.hpp"
#include <fstream>
#include <iostream>

namespace {
int failures{};
#define CHECK(x) do { if (!(x)) { std::cerr << "Failed line " << __LINE__ << ": " #x "\n"; ++failures; } } while (false)
}
int main(int argc, char** argv) {
    using namespace RuffnecKk::AutoPickup;
    using namespace RuffnecKk::AutoPickup::Picker;
    Config initial, draft;
    const auto jewel = PackItemCode("jew"), gem = PackItemCode("gmo"), el = PackItemCode("r01");
    CHECK(ValidCode(jewel) && ValidCode(gem));
    CHECK(!ValidCode(0) && !ValidCode(PackItemCode("JEW")) && !ValidCode(0x61306200));
    CHECK(CodeText(jewel) == "jew");
    CHECK(PlainName("\xc3\xbf" "cUChaos Onyx") == "Chaos Onyx");
    CHECK(PlainName("\xef\xbf\xbd" "c8Orb\nof Corruption") == "Orb of Corruption");
    SetChoice(draft, jewel, Choice::Include); SetChoice(draft, jewel, Choice::Include);
    CHECK(draft.includeCodes.size() == 1 && draft.Selects(jewel));
    SetChoice(draft, jewel, Choice::Exclude);
    CHECK(draft.includeCodes.empty() && !draft.Selects(jewel));
    SetChoice(draft, jewel, Choice::Default); CHECK(draft.excludeCodes.empty());
    SetChoice(draft, jewel, Choice::Include); SetChoice(draft, el, Choice::Exclude);
    Config bulk;
    const std::vector<std::uint32_t> seven{PackItemCode("ka3"), PackItemCode("1ka"), jewel, el, PackItemCode("r02"), PackItemCode("r03"), gem};
    SetChoices(bulk, seven, Choice::Include);
    CHECK(std::all_of(seven.begin(), seven.end(), [&](auto code) { return bulk.Selects(code); }));
    SetChoices(bulk, seven, Choice::Exclude);
    CHECK(std::none_of(seven.begin(), seven.end(), [&](auto code) { return bulk.Selects(code); }));
    SetChoice(bulk, PackItemCode("cm1"), Choice::Include);
    SetChoices(bulk, seven, Choice::Default);
    CHECK(!bulk.Selects(el) && !bulk.Selects(gem) && !bulk.Selects(jewel) && bulk.Selects(PackItemCode("cm1")));
    const std::vector<Entry> catalog{{jewel, "Jewel"}, {gem, "Chaos Onyx"}, {PackItemCode("gcw"), "Diamond"},
        {PackItemCode("gmw"), "Diamond"}, {PackItemCode("gld"), "Gold"}};
    auto found = Search(catalog, draft, "dIaMoNd", false);
    CHECK(found.size() == 2 && found[0].code != found[1].code); // names are not identities
    found = Search(catalog, draft, "gmo", false); CHECK(found.size() == 1 && found[0].code == gem);
    found = Search(catalog, draft, "", true);
    CHECK(found.size() == 2 && std::any_of(found.begin(), found.end(), [el](const auto& row) { return row.code == el && !row.available; }));
    CHECK(Search(catalog, draft, "Gold", false).empty());
    std::vector<Entry> large;
    for (unsigned i = 0; i < 1000; ++i) large.push_back({i + 1, "Mod item " + std::to_string(i)});
    CHECK(Search(large, initial, "", false).size() == 1000);
    static_assert((1000 + PageSize - 1) / PageSize == 125);

    std::string output, error;
    const std::string current = "# retain my comment\r\n[vanilla]\r\ngems = false\r\n[item_codes]\r\ninclude = [ # ] inside a comment\r\n] # include note\r\nexclude = [] # exclude note\r\n[d2rl]\r\nmatch = [\"enabled\"]\r\ncustom = \"retain me\"\r\n";
    CHECK(SaveSelection(current, initial, draft, output, error));
    CHECK(output.find("# retain my comment") != std::string::npos);
    CHECK(output.find("# include note") != std::string::npos && output.find("# exclude note") != std::string::npos);
    CHECK(output.find("gems = false") != std::string::npos && output.find("custom = \"retain me\"") != std::string::npos);
    Config reread;
    CHECK(ParseConfig(output, reread, error) && SameSelection(reread, draft) && !reread.vanilla[0]);
    const auto saved = output;
    CHECK(!SaveSelection(saved, initial, draft, output, error)); // external selection conflict
    CHECK(error.find("outside") != std::string::npos);
    CHECK(SaveSelection("[d2rl]\nmatch=[]\n", initial, draft, output, error));
    CHECK(ParseConfig(output, reread, error) && SameSelection(reread, draft));
    CHECK(SaveSelection("[item_codes]\n# no arrays yet\n[vanilla]\ngems=true\n", initial, draft, output, error));
    CHECK(ParseConfig(output, reread, error) && SameSelection(reread, draft));
    CHECK(SaveSelection("enabled=true", initial, draft, output, error));
    CHECK(ParseConfig(output, reread, error) && SameSelection(reread, draft));
    CHECK(!SaveSelection("[item_codes]\ninclude = [\"bad code\"]", initial, draft, output, error));
    Config common;
    common.enabled = false; common.vanilla[1] = false; common.reimagined[2] = false;
    common.walkingRange = 9; common.teleportRange = 12; common.teleportGold = false;
    CHECK(SaveSettings("# my settings\nvalidate_only = true # keep diagnostic mode\n[d2rl]\nmatch=[]\ncustom=42\n", initial, common, output, error));
    CHECK(ParseConfig(output, reread, error) && SameSettings(reread, common) && reread.validateOnly);
    CHECK(output.find("# my settings") != std::string::npos && output.find("# keep diagnostic mode") != std::string::npos && output.find("custom=42") != std::string::npos);
    CHECK(!SaveSettings(output, initial, common, output, error) && error.find("outside") != std::string::npos);
    auto enabledInitial=initial;enabledInitial.enabled=true;enabledInitial.vanilla[1]=true;
    CHECK(SaveSettings("enabled = true # master\r\n[vanilla]\r\nrunes = true # runes\r\n[teleport]\r\npickup_range = 4\r\n[d2rl]\r\nmatch=[]\r\n", enabledInitial, common, output, error));
    CHECK(ParseConfig(output, reread, error) && SameSettings(reread, common));
    CHECK(output.find("enabled = false # master\r\n") != std::string::npos && output.find("runes = false # runes\r\n") != std::string::npos);
    common.walkingRange = 0;
    CHECK(!SaveSettings("", initial, common, output, error));

    std::array<std::array<std::uint32_t, 5>, 12> rows{};
    for (std::uint32_t i = 0; i < rows.size(); ++i) {
        // Distinct, format-valid decoys are rejected by semantic lookup.
        rows[i][0] = PackItemCode("bad");
        rows[i][3] = PackItemCode("r" + std::to_string(i + 1));
    }
    D2RL::DataTables::TableView table{};
    table.structSize = sizeof(table); table.rows = rows.data(); table.rowCount = static_cast<std::uint32_t>(rows.size()); table.rowSize = sizeof(rows[0]);
    auto lookup = [&](std::uint32_t code, std::uint32_t index) { return rows[index][3] == code; };
    CHECK(DiscoverCodeColumn(table, lookup) == 12);
    for (unsigned i = 0; i < rows.size(); ++i) rows[i][2] = 100 + i;
    const auto keyName = [](std::uint32_t code) { return CodeText(code); };
    const auto idName = [&](std::uint16_t id) { return id >= 100 && id < 112 ? CodeText(rows[id - 100][3]) : std::string{}; };
    CHECK(DiscoverNameColumn(table, 12, keyName, idName) == 8);
    CHECK(!DiscoverNameColumn(table, 12, keyName, [](std::uint16_t) { return std::string{}; }));
    CHECK(!DiscoverNameColumn(table, 12, [](std::uint32_t) { return std::string("same name"); }, idName));
    CHECK(!DiscoverCodeColumn(table, [](std::uint32_t, std::uint32_t) { return false; }));
    table.rowSize = 3; CHECK(!DiscoverCodeColumn(table, lookup));
    table.rowSize = sizeof(rows[0]); table.rowCount = 999'999; CHECK(!DiscoverCodeColumn(table, lookup));
    WidgetExtensions shortService{}; shortService.base.serviceVersion = 1; shortService.base.serviceSize = sizeof(D2RL::WidgetServiceV1);
    CHECK(!HasPickerWidgets(&shortService)); CHECK(!HasPickerWidgets(nullptr));
    if (argc > 1) { std::ofstream file(argv[1]); file << PanelLayout(); }
    {
        Config before;std::string why;CHECK(ParseConfig(current,before,why));
        auto after=before;
        RuffnecKk::PotionRouting::SetToggle(after.potionRouting,0,false);
        RuffnecKk::PotionRouting::SetToggle(after.potionRouting,3,true);
        RuffnecKk::PotionRouting::SetToggle(after.potionRouting,4,false);
        CHECK(!SameSettings(before,after));
        std::string changed;CHECK(SaveSettings(current,before,after,changed,why));
        Config readback;CHECK(ParseConfig(changed,readback,why));
        CHECK(readback.potionRouting==after.potionRouting);
        CHECK(changed.find("retain my comment")!=std::string::npos);
        CHECK(SameSelection(before,readback));
        CHECK(!SaveSettings(changed,before,after,output,why));
    }
    std::cout << "Picker policy, mod identities, paging, config preservation and semantic table witnesses; failures: " << failures << '\n';
    return failures ? 1 : 0;
}
