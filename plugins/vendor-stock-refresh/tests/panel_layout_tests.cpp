#include "compact_layout.hpp"
#include <array>

using namespace RuffnecKk::VendorStockRefresh;
#define CHECK(expression) do { if (!(expression)) return __LINE__; } while (false)

int main() {
    const WidgetGeometry button{{877, 1277, 116, 116}, 1.0F};
    const WidgetRect slot{520, 1342, 116, 116};
    const auto framed = CenterInPanelSlot(slot, button);
    CHECK(framed.valid);
    CHECK(framed.geometry.rect.x == 520 && framed.geometry.rect.y == 1342);
    CHECK(framed.geometry.scale == 1.0F); // Full-size artwork and native hit area.
    const WidgetRect originalGold{421, 1305, 313, 58};
    const WidgetRect targetGold{421, 1260, 313, 58};
    const std::array<WidgetGeometry, 3> gold{{
        {originalGold, 1.0F}, {{427, 1304, 57, 57}, 1.0F}, {{487, 1309, 249, 48}, 1.0F},
    }};
    std::array<RefreshLayoutState, 3> state{};
    RefreshLayoutState refresh;
    refresh.Observe(button);
    for (int round = 0; round < 20; ++round) {
        // Shop -> repeat configure -> gambling -> shop. Observe the applied state
        // exactly as native readback does; the baseline must never accumulate offsets.
        for (std::size_t i = 0; i < gold.size(); ++i) {
            state[i].Observe(gold[i]);
            const auto moved = TranslateGoldWidget(state[i].original, originalGold, targetGold);
            CHECK(moved.valid);
            CHECK(moved.geometry.rect.x == gold[i].rect.x);
            CHECK(moved.geometry.rect.y == gold[i].rect.y - 45);
            CHECK(moved.geometry.scale == gold[i].scale);
            state[i].Applied(moved.geometry);
            state[i].Observe(moved.geometry);
            CHECK(SameGeometry(state[i].original, gold[i]));
            const auto repeated = TranslateGoldWidget(state[i].original, originalGold, targetGold);
            CHECK(SameGeometry(repeated.geometry, moved.geometry));
            // Gambling restores the captured original, not a hardcoded vanilla rect.
            state[i].Restored();
            state[i].Observe(gold[i]);
            CHECK(SameGeometry(state[i].original, gold[i]));
        }
        refresh.Applied(framed.geometry);
        refresh.Observe(framed.geometry);
        CHECK(SameGeometry(refresh.original, button));
        refresh.Restored();
        refresh.Observe(button);
    }
    // The controller inheritance guard deliberately has no usable slot.
    CHECK(!CenterInPanelSlot({}, button).valid);
    CHECK(CompactBelow(originalGold, button).valid);
    CHECK(!CenterInPanelSlot({520, 1342, 80, 80}, button).valid);
    const auto smaller = CenterInPanelSlot(slot, {{877, 1277, 112, 112}, 1.0F});
    CHECK(smaller.valid && smaller.geometry.rect.x == 522 && smaller.geometry.rect.y == 1344);
    CHECK(!TranslateGoldWidget(gold[1], originalGold,
        {(std::numeric_limits<int>::max)(), 1260, 313, 58}).valid);
    const WidgetGeometry full{{0,0,116,116},1.0F};
    const auto roomy = FitButtonInArea({421,1371,313,120}, full);
    CHECK(roomy.valid && roomy.geometry.scale == 1.0F && roomy.geometry.rect.y == 1371);
    const auto tight = FitButtonInArea({421,1371,313,90}, full);
    CHECK(tight.valid && tight.geometry.scale > 0.77F && tight.geometry.scale < 0.78F);
    CHECK(tight.geometry.scale * 116 <= 90);
    CHECK(!FitButtonInArea({0,0,313,7}, full).valid);
    CHECK(!FitButtonInArea({0,0,0,120}, full).valid);
    return 0;
}
