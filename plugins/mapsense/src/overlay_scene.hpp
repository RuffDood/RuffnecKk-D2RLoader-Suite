#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace RuffnecKk::MapSense {

struct Vec2 final {
    float x{};
    float y{};

    friend constexpr auto operator==(const Vec2&, const Vec2&) -> bool = default;
};

struct Rgba8 final {
    std::uint8_t red{};
    std::uint8_t green{};
    std::uint8_t blue{};
    std::uint8_t alpha{255U};

    friend constexpr auto operator==(const Rgba8&, const Rgba8&) -> bool = default;
};

enum class Element : std::uint8_t {
    Physical,
    Magic,
    Fire,
    Lightning,
    Cold,
    Poison,
};

enum class MonsterRank : std::uint8_t {
    Normal,
    Minion,
    Champion,
    Unique,
    SuperUnique,
};

enum class MonsterShape : std::uint8_t {
    Circle,
    Triangle,
    Diamond,
    Star,
    Hexagon,
};

enum class TextAnchor : std::uint8_t {
    TopLeft,
    TopCenter,
    Center,
};

namespace ScenePalette {

inline constexpr Rgba8 MonsterNormal{220U, 70U, 70U, 255U};
inline constexpr Rgba8 MonsterMinion{235U, 135U, 45U, 255U};
inline constexpr Rgba8 MonsterChampion{75U, 145U, 255U, 255U};
inline constexpr Rgba8 MonsterUnique{255U, 202U, 55U, 255U};
inline constexpr Rgba8 MonsterSuperUnique{255U, 95U, 215U, 255U};

inline constexpr Rgba8 Physical{205U, 205U, 205U, 255U};
inline constexpr Rgba8 Magic{202U, 105U, 255U, 255U};
inline constexpr Rgba8 Fire{255U, 78U, 50U, 255U};
inline constexpr Rgba8 Lightning{255U, 224U, 55U, 255U};
inline constexpr Rgba8 Cold{65U, 165U, 255U, 255U};
inline constexpr Rgba8 Poison{85U, 220U, 105U, 255U};

inline constexpr Rgba8 MapLine{211U, 177U, 115U, 230U};
inline constexpr Rgba8 PanelBackground{18U, 15U, 12U, 215U};
inline constexpr Rgba8 TextPrimary{236U, 220U, 184U, 255U};
inline constexpr Rgba8 TextMuted{171U, 151U, 117U, 255U};

} // namespace ScenePalette

[[nodiscard]] constexpr auto WithAlpha(Rgba8 color, std::uint8_t alpha) -> Rgba8 {
    color.alpha = alpha;
    return color;
}

[[nodiscard]] constexpr auto ColorFor(Element element) -> Rgba8 {
    switch (element) {
        case Element::Physical: return ScenePalette::Physical;
        case Element::Magic: return ScenePalette::Magic;
        case Element::Fire: return ScenePalette::Fire;
        case Element::Lightning: return ScenePalette::Lightning;
        case Element::Cold: return ScenePalette::Cold;
        case Element::Poison: return ScenePalette::Poison;
    }
    return ScenePalette::Physical;
}

[[nodiscard]] constexpr auto ColorFor(MonsterRank rank) -> Rgba8 {
    switch (rank) {
        case MonsterRank::Normal: return ScenePalette::MonsterNormal;
        case MonsterRank::Minion: return ScenePalette::MonsterMinion;
        case MonsterRank::Champion: return ScenePalette::MonsterChampion;
        case MonsterRank::Unique: return ScenePalette::MonsterUnique;
        case MonsterRank::SuperUnique: return ScenePalette::MonsterSuperUnique;
    }
    return ScenePalette::MonsterNormal;
}

[[nodiscard]] constexpr auto ShapeFor(MonsterRank rank) -> MonsterShape {
    switch (rank) {
        case MonsterRank::Normal: return MonsterShape::Circle;
        case MonsterRank::Minion: return MonsterShape::Triangle;
        case MonsterRank::Champion: return MonsterShape::Diamond;
        case MonsterRank::Unique: return MonsterShape::Star;
        case MonsterRank::SuperUnique: return MonsterShape::Hexagon;
    }
    return MonsterShape::Circle;
}

struct LinePrimitive final {
    Vec2 start{};
    Vec2 end{};
    Rgba8 color{ScenePalette::MapLine};
    float thickness{1.0F};
};

struct CirclePrimitive final {
    Vec2 center{};
    float radius{1.0F};
    Rgba8 stroke{ScenePalette::MapLine};
    float thickness{1.0F};
    std::optional<Rgba8> fill{};
};

struct PolygonPrimitive final {
    std::vector<Vec2> points{};
    Rgba8 stroke{ScenePalette::MapLine};
    float thickness{1.0F};
    std::optional<Rgba8> fill{};
};

struct MonsterMarker final {
    Vec2 center{};
    float radius{6.0F};
    MonsterRank rank{MonsterRank::Normal};
    Rgba8 stroke{ScenePalette::MonsterNormal};
    Rgba8 fill{WithAlpha(ScenePalette::MonsterNormal, 64U)};
    float thickness{1.5F};
};

struct ImmunityArc final {
    Element element{Element::Physical};
    float startRadians{};
    float endRadians{};
    Rgba8 color{ScenePalette::Physical};
};

struct ImmunityRing final {
    Vec2 center{};
    float innerRadius{8.0F};
    float outerRadius{10.0F};
    std::vector<ImmunityArc> arcs{};
};

struct MissileMarker final {
    Vec2 center{};
    Vec2 direction{1.0F, 0.0F};
    float length{14.0F};
    float radius{2.0F};
    Element element{Element::Physical};
    Rgba8 color{ScenePalette::Physical};
};

struct TextPrimitive final {
    Vec2 position{};
    std::string text{};
    Rgba8 color{ScenePalette::TextPrimary};
    float size{15.0F};
    TextAnchor anchor{TextAnchor::TopLeft};
};

using Primitive = std::variant<
    LinePrimitive,
    CirclePrimitive,
    PolygonPrimitive,
    MonsterMarker,
    ImmunityRing,
    MissileMarker,
    TextPrimitive>;

struct SceneSnapshot final {
    std::uint64_t sequence{};
    std::uint32_t viewportWidth{};
    std::uint32_t viewportHeight{};
    std::vector<Primitive> primitives{};
};

using SceneSnapshotPtr = std::shared_ptr<const SceneSnapshot>;

inline constexpr std::size_t CrossOutlinePointCount = 13U;
using CrossOutline = std::array<Vec2, CrossOutlinePointCount>;

class SceneExchange final {
public:
    SceneExchange() = default;
    SceneExchange(const SceneExchange&) = delete;
    auto operator=(const SceneExchange&) -> SceneExchange& = delete;

    auto Publish(SceneSnapshot snapshot) -> SceneSnapshotPtr;
    [[nodiscard]] auto Acquire() const -> SceneSnapshotPtr;
    void Clear();

private:
    std::atomic<SceneSnapshotPtr> current_{};
};

void Validate(const Primitive& primitive);
void Validate(const SceneSnapshot& snapshot);

// Returns the center-to-center advance for adjacent colored immunity glyphs.
// It deliberately ignores the font's typographic advance because the narrow
// lowercase i otherwise inherits a large invisible side-bearing at big sizes.
[[nodiscard]] auto ComputeColoredImmunityIndicatorAdvance(float fontSize)
    -> float;

// Returns a saltire/X outline with twelve unique vertices. The thirteenth
// point repeats the first so renderers can submit the polyline as closed.
[[nodiscard]] auto BuildCrossOutline(Vec2 center, float radius) -> CrossOutline;
// Reproduces the native player automap icon silhouette as a 2:1 saltire.
// `width` is the complete horizontal extent; the returned outline is closed.
[[nodiscard]] auto BuildPlayerCrossOutline(Vec2 center, float width) -> CrossOutline;
[[nodiscard]] auto BuildMonsterOutline(const MonsterMarker& marker) -> std::vector<Vec2>;
[[nodiscard]] auto MakeMonsterMarker(MonsterRank rank, Vec2 center, float radius = 6.0F)
    -> MonsterMarker;
[[nodiscard]] auto MakeImmunityRing(
    Vec2 center,
    float innerRadius,
    float outerRadius,
    std::span<const Element> immunities,
    float gapRadians = 0.08F) -> ImmunityRing;
[[nodiscard]] auto MakeMissileMarker(
    Element element,
    Vec2 center,
    Vec2 direction,
    float length = 14.0F,
    float radius = 2.0F) -> MissileMarker;
[[nodiscard]] auto BuildDiagnosticPreview(
    std::uint32_t viewportWidth,
    std::uint32_t viewportHeight,
    std::uint64_t sequence = 1U) -> SceneSnapshot;

} // namespace RuffnecKk::MapSense
