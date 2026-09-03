#include "overlay_scene.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <numbers>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace RuffnecKk::MapSense {
namespace {

constexpr auto Pi = std::numbers::pi_v<float>;
constexpr auto TwoPi = 2.0F * Pi;
constexpr auto CrossInnerRadiusRatio = 0.35F;
constexpr auto PlayerCrossHeightToWidthRatio = 0.50F;
constexpr auto PlayerCrossShoulderRatio = 0.40F;

template <class>
inline constexpr bool AlwaysFalse = false;

[[nodiscard]] auto IsFinite(float value) -> bool {
    return std::isfinite(value);
}

[[nodiscard]] auto IsFinite(Vec2 value) -> bool {
    return IsFinite(value.x) && IsFinite(value.y);
}

[[nodiscard]] constexpr auto IsKnown(Element element) -> bool {
    switch (element) {
        case Element::Physical:
        case Element::Magic:
        case Element::Fire:
        case Element::Lightning:
        case Element::Cold:
        case Element::Poison:
            return true;
    }
    return false;
}

[[nodiscard]] constexpr auto IsKnown(MonsterRank rank) -> bool {
    switch (rank) {
        case MonsterRank::Normal:
        case MonsterRank::Minion:
        case MonsterRank::Champion:
        case MonsterRank::Unique:
        case MonsterRank::SuperUnique:
            return true;
    }
    return false;
}

[[nodiscard]] constexpr auto IsKnown(TextAnchor anchor) -> bool {
    switch (anchor) {
        case TextAnchor::TopLeft:
        case TextAnchor::TopCenter:
        case TextAnchor::Center:
            return true;
    }
    return false;
}

void Require(bool condition, const char* message) {
    if (!condition) {
        throw std::invalid_argument(message);
    }
}

void ValidatePositive(float value, const char* message) {
    Require(IsFinite(value) && value > 0.0F, message);
}

[[nodiscard]] auto PointOnCircle(Vec2 center, float radius, float radians) -> Vec2 {
    return Vec2{
        center.x + (std::cos(radians) * radius),
        center.y + (std::sin(radians) * radius),
    };
}

[[nodiscard]] auto BuildRegularPolygon(
    Vec2 center,
    float radius,
    std::size_t pointCount,
    float startRadians = -Pi / 2.0F) -> std::vector<Vec2> {
    std::vector<Vec2> points;
    points.reserve(pointCount);
    const auto step = TwoPi / static_cast<float>(pointCount);
    for (std::size_t index = 0; index < pointCount; ++index) {
        points.push_back(PointOnCircle(
            center,
            radius,
            startRadians + (static_cast<float>(index) * step)));
    }
    return points;
}

void ValidateLine(const LinePrimitive& line) {
    Require(IsFinite(line.start) && IsFinite(line.end), "line coordinates must be finite");
    ValidatePositive(line.thickness, "line thickness must be positive and finite");
    Require(line.start != line.end, "line endpoints must differ");
}

void ValidateCircle(const CirclePrimitive& circle) {
    Require(IsFinite(circle.center), "circle center must be finite");
    ValidatePositive(circle.radius, "circle radius must be positive and finite");
    ValidatePositive(circle.thickness, "circle thickness must be positive and finite");
}

void ValidatePolygon(const PolygonPrimitive& polygon) {
    Require(polygon.points.size() >= 3U, "polygon needs at least three points");
    Require(
        std::all_of(polygon.points.begin(), polygon.points.end(), [](Vec2 point) {
            return IsFinite(point);
        }),
        "polygon coordinates must be finite");
    ValidatePositive(polygon.thickness, "polygon thickness must be positive and finite");
}

void ValidateMonster(const MonsterMarker& marker) {
    Require(IsFinite(marker.center), "monster center must be finite");
    ValidatePositive(marker.radius, "monster radius must be positive and finite");
    ValidatePositive(marker.thickness, "monster thickness must be positive and finite");
    Require(IsKnown(marker.rank), "monster rank is invalid");
}

void ValidateImmunityRing(const ImmunityRing& ring) {
    Require(IsFinite(ring.center), "immunity ring center must be finite");
    ValidatePositive(ring.innerRadius, "immunity ring inner radius must be positive and finite");
    ValidatePositive(ring.outerRadius, "immunity ring outer radius must be positive and finite");
    Require(ring.outerRadius > ring.innerRadius, "immunity ring outer radius must exceed inner radius");
    Require(!ring.arcs.empty(), "immunity ring needs at least one arc");
    Require(ring.arcs.size() <= 6U, "immunity ring supports at most six elements");

    std::vector<Element> seen;
    seen.reserve(ring.arcs.size());
    for (const auto& arc : ring.arcs) {
        Require(IsKnown(arc.element), "immunity arc element is invalid");
        Require(
            IsFinite(arc.startRadians) && IsFinite(arc.endRadians),
            "immunity arc angles must be finite");
        Require(arc.endRadians > arc.startRadians, "immunity arc must have a positive sweep");
        Require(
            std::find(seen.begin(), seen.end(), arc.element) == seen.end(),
            "immunity ring elements must be unique");
        seen.push_back(arc.element);
    }
}

void ValidateMissile(const MissileMarker& missile) {
    Require(IsKnown(missile.element), "missile element is invalid");
    Require(
        IsFinite(missile.center) && IsFinite(missile.direction),
        "missile coordinates must be finite");
    const auto magnitudeSquared =
        (missile.direction.x * missile.direction.x) +
        (missile.direction.y * missile.direction.y);
    Require(
        IsFinite(magnitudeSquared) && magnitudeSquared > 0.0F,
        "missile direction must be non-zero and finite");
    ValidatePositive(missile.length, "missile length must be positive and finite");
    ValidatePositive(missile.radius, "missile radius must be positive and finite");
}

void ValidateText(const TextPrimitive& text) {
    Require(IsFinite(text.position), "text position must be finite");
    Require(!text.text.empty(), "text must not be empty");
    ValidatePositive(text.size, "text size must be positive and finite");
    Require(IsKnown(text.anchor), "text anchor is invalid");
}

} // namespace

auto SceneExchange::Publish(SceneSnapshot snapshot) -> SceneSnapshotPtr {
    Validate(snapshot);
    auto published = std::make_shared<const SceneSnapshot>(std::move(snapshot));
    current_.store(published, std::memory_order_release);
    return published;
}

auto SceneExchange::Acquire() const -> SceneSnapshotPtr {
    return current_.load(std::memory_order_acquire);
}

void SceneExchange::Clear() {
    current_.store(nullptr, std::memory_order_release);
}

void Validate(const Primitive& primitive) {
    std::visit(
        [](const auto& value) {
            using Value = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Value, LinePrimitive>) {
                ValidateLine(value);
            } else if constexpr (std::is_same_v<Value, CirclePrimitive>) {
                ValidateCircle(value);
            } else if constexpr (std::is_same_v<Value, PolygonPrimitive>) {
                ValidatePolygon(value);
            } else if constexpr (std::is_same_v<Value, MonsterMarker>) {
                ValidateMonster(value);
            } else if constexpr (std::is_same_v<Value, ImmunityRing>) {
                ValidateImmunityRing(value);
            } else if constexpr (std::is_same_v<Value, MissileMarker>) {
                ValidateMissile(value);
            } else if constexpr (std::is_same_v<Value, TextPrimitive>) {
                ValidateText(value);
            } else {
                static_assert(AlwaysFalse<Value>, "unhandled MapSense primitive");
            }
        },
        primitive);
}

void Validate(const SceneSnapshot& snapshot) {
    Require(snapshot.viewportWidth > 0U, "scene viewport width must be positive");
    Require(snapshot.viewportHeight > 0U, "scene viewport height must be positive");
    for (const auto& primitive : snapshot.primitives) {
        Validate(primitive);
    }
}

auto ComputeColoredImmunityIndicatorAdvance(float fontSize) -> float {
    ValidatePositive(
        fontSize,
        "colored immunity indicator font size must be positive and finite");
    return std::max(1.5F, fontSize * 0.12F);
}

auto BuildCrossOutline(Vec2 center, float radius) -> CrossOutline {
    Require(IsFinite(center), "cross center must be finite");
    ValidatePositive(radius, "cross radius must be positive and finite");

    const auto inner = radius * CrossInnerRadiusRatio;
    const auto left = center.x - radius;
    const auto right = center.x + radius;
    const auto top = center.y - radius;
    const auto bottom = center.y + radius;
    const auto innerLeft = center.x - inner;
    const auto innerRight = center.x + inner;
    const auto innerTop = center.y - inner;
    const auto innerBottom = center.y + inner;

    const Vec2 first{left, top + inner};
    return CrossOutline{{
        first,
        Vec2{left + inner, top},
        Vec2{center.x, innerTop},
        Vec2{right - inner, top},
        Vec2{right, top + inner},
        Vec2{innerRight, center.y},
        Vec2{right, bottom - inner},
        Vec2{right - inner, bottom},
        Vec2{center.x, innerBottom},
        Vec2{left + inner, bottom},
        Vec2{left, bottom - inner},
        Vec2{innerLeft, center.y},
        first,
    }};
}

auto BuildPlayerCrossOutline(Vec2 center, float width) -> CrossOutline {
    Require(IsFinite(center), "player cross center must be finite");
    ValidatePositive(width, "player cross width must be positive and finite");

    const auto halfWidth = width * 0.5F;
    const auto halfHeight = halfWidth * PlayerCrossHeightToWidthRatio;
    const auto shoulderX = halfWidth * PlayerCrossShoulderRatio;
    const auto shoulderY = halfHeight * PlayerCrossShoulderRatio;
    const auto left = center.x - halfWidth;
    const auto right = center.x + halfWidth;
    const auto top = center.y - halfHeight;
    const auto bottom = center.y + halfHeight;

    const Vec2 first{left, center.y - shoulderY};
    return CrossOutline{{
        first,
        Vec2{center.x - shoulderX, top},
        Vec2{center.x, center.y - shoulderY},
        Vec2{center.x + shoulderX, top},
        Vec2{right, center.y - shoulderY},
        Vec2{center.x + shoulderX, center.y},
        Vec2{right, center.y + shoulderY},
        Vec2{center.x + shoulderX, bottom},
        Vec2{center.x, center.y + shoulderY},
        Vec2{center.x - shoulderX, bottom},
        Vec2{left, center.y + shoulderY},
        Vec2{center.x - shoulderX, center.y},
        first,
    }};
}

auto BuildMonsterOutline(const MonsterMarker& marker) -> std::vector<Vec2> {
    ValidateMonster(marker);
    switch (ShapeFor(marker.rank)) {
        case MonsterShape::Circle:
            return BuildRegularPolygon(marker.center, marker.radius, 24U);
        case MonsterShape::Triangle:
            return BuildRegularPolygon(marker.center, marker.radius, 3U);
        case MonsterShape::Diamond:
            return BuildRegularPolygon(marker.center, marker.radius, 4U);
        case MonsterShape::Star: {
            constexpr std::size_t PointCount = 10U;
            std::vector<Vec2> points;
            points.reserve(PointCount);
            constexpr auto step = TwoPi / static_cast<float>(PointCount);
            for (std::size_t index = 0; index < PointCount; ++index) {
                const auto radius = (index % 2U) == 0U ? marker.radius : marker.radius * 0.45F;
                points.push_back(PointOnCircle(
                    marker.center,
                    radius,
                    (-Pi / 2.0F) + (static_cast<float>(index) * step)));
            }
            return points;
        }
        case MonsterShape::Hexagon:
            return BuildRegularPolygon(marker.center, marker.radius, 6U);
    }
    throw std::invalid_argument("unsupported monster shape");
}

auto MakeMonsterMarker(MonsterRank rank, Vec2 center, float radius) -> MonsterMarker {
    const auto color = ColorFor(rank);
    MonsterMarker marker{
        .center = center,
        .radius = radius,
        .rank = rank,
        .stroke = color,
        .fill = WithAlpha(color, 64U),
        .thickness = 1.5F,
    };
    ValidateMonster(marker);
    return marker;
}

auto MakeImmunityRing(
    Vec2 center,
    float innerRadius,
    float outerRadius,
    std::span<const Element> immunities,
    float gapRadians) -> ImmunityRing {
    Require(!immunities.empty(), "immunity ring needs at least one element");
    Require(immunities.size() <= 6U, "immunity ring supports at most six elements");
    Require(IsFinite(gapRadians) && gapRadians >= 0.0F, "immunity ring gap must be finite and non-negative");

    const auto sweep = TwoPi / static_cast<float>(immunities.size());
    Require(gapRadians < sweep, "immunity ring gap must be smaller than an arc slot");

    ImmunityRing ring{
        .center = center,
        .innerRadius = innerRadius,
        .outerRadius = outerRadius,
    };
    ring.arcs.reserve(immunities.size());
    for (std::size_t index = 0; index < immunities.size(); ++index) {
        const auto slotStart = (-Pi / 2.0F) + (static_cast<float>(index) * sweep);
        const auto element = immunities[index];
        ring.arcs.push_back(ImmunityArc{
            .element = element,
            .startRadians = slotStart + (gapRadians / 2.0F),
            .endRadians = slotStart + sweep - (gapRadians / 2.0F),
            .color = ColorFor(element),
        });
    }
    ValidateImmunityRing(ring);
    return ring;
}

auto MakeMissileMarker(
    Element element,
    Vec2 center,
    Vec2 direction,
    float length,
    float radius) -> MissileMarker {
    MissileMarker missile{
        .center = center,
        .direction = direction,
        .length = length,
        .radius = radius,
        .element = element,
        .color = ColorFor(element),
    };
    ValidateMissile(missile);
    return missile;
}

auto BuildDiagnosticPreview(
    std::uint32_t viewportWidth,
    std::uint32_t viewportHeight,
    std::uint64_t sequence) -> SceneSnapshot {
    Require(viewportWidth > 0U && viewportHeight > 0U, "diagnostic viewport must be positive");

    const auto width = static_cast<float>(viewportWidth);
    const auto height = static_cast<float>(viewportHeight);
    const Vec2 center{width * 0.5F, height * 0.5F};

    SceneSnapshot snapshot{
        .sequence = sequence,
        .viewportWidth = viewportWidth,
        .viewportHeight = viewportHeight,
    };
    snapshot.primitives.reserve(15U);
    snapshot.primitives.emplace_back(LinePrimitive{
        .start = Vec2{center.x - 110.0F, center.y},
        .end = Vec2{center.x + 110.0F, center.y},
        .color = ScenePalette::MapLine,
        .thickness = 1.5F,
    });
    snapshot.primitives.emplace_back(CirclePrimitive{
        .center = center,
        .radius = 22.0F,
        .stroke = ScenePalette::MapLine,
        .thickness = 1.5F,
        .fill = WithAlpha(ScenePalette::PanelBackground, 96U),
    });
    snapshot.primitives.emplace_back(PolygonPrimitive{
        .points = BuildRegularPolygon(Vec2{center.x, center.y - 52.0F}, 10.0F, 3U),
        .stroke = ScenePalette::MapLine,
        .thickness = 1.5F,
        .fill = WithAlpha(ScenePalette::MapLine, 48U),
    });

    constexpr MonsterRank ranks[]{
        MonsterRank::Normal,
        MonsterRank::Minion,
        MonsterRank::Champion,
        MonsterRank::Unique,
        MonsterRank::SuperUnique,
    };
    constexpr auto spacing = 34.0F;
    const auto firstX = center.x - (spacing * 2.0F);
    for (std::size_t index = 0; index < std::size(ranks); ++index) {
        snapshot.primitives.emplace_back(MakeMonsterMarker(
            ranks[index],
            Vec2{firstX + (static_cast<float>(index) * spacing), center.y + 50.0F},
            8.0F));
    }

    constexpr Element immunities[]{Element::Fire, Element::Cold, Element::Lightning};
    snapshot.primitives.emplace_back(MakeImmunityRing(
        center,
        26.0F,
        30.0F,
        immunities));
    snapshot.primitives.emplace_back(MakeMissileMarker(
        Element::Fire,
        Vec2{center.x - 85.0F, center.y - 35.0F},
        Vec2{1.0F, 0.35F},
        18.0F,
        3.0F));
    snapshot.primitives.emplace_back(MakeMissileMarker(
        Element::Cold,
        Vec2{center.x + 85.0F, center.y - 35.0F},
        Vec2{-1.0F, 0.35F},
        18.0F,
        3.0F));
    snapshot.primitives.emplace_back(TextPrimitive{
        .position = Vec2{center.x, center.y - 92.0F},
        .text = "MapSense renderer witness",
        .color = ScenePalette::TextPrimary,
        .size = 16.0F,
        .anchor = TextAnchor::TopCenter,
    });
    snapshot.primitives.emplace_back(TextPrimitive{
        .position = Vec2{center.x, center.y + 76.0F},
        .text = "Diagnostic preview - not automap aligned",
        .color = ScenePalette::TextMuted,
        .size = 13.0F,
        .anchor = TextAnchor::TopCenter,
    });

    Validate(snapshot);
    return snapshot;
}

} // namespace RuffnecKk::MapSense
