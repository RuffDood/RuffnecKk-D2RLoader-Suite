#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace RuffnecKk::BulkSkillPointAllocation {

inline constexpr char PrimaryLocaleProbeKey[] =
    "AssignAllStatPointsConfirmation";
inline constexpr char SecondaryLocaleProbeKey[] =
    "ControllerPromptAssignSkillPoint";

struct LocaleDefinition {
    std::string_view code;
    std::string_view primaryProbe;
    std::string_view secondaryProbe;
    std::string_view defaultConfirmation;
};

inline constexpr std::array LocaleDefinitions{
    LocaleDefinition{
        "enUS",
        "Are you sure you want to assign all available stat points?",
        "Assign Skill Point(s)",
        "Invest all currently usable skill points in this skill?"},
    LocaleDefinition{
        "zhTW",
        "你確定要分配全部可用的屬性點數嗎？",
        "分配技能點數",
        "要將目前可用的所有技能點數投入此技能嗎？"},
    LocaleDefinition{
        "deDE",
        "Seid Ihr sicher, dass Ihr alle verfügbaren Wertpunkte zuweisen wollt?",
        "Fertigkeitspunkt(e) zuweisen",
        "Möchtest du alle derzeit verfügbaren Fertigkeitspunkte in diese Fertigkeit investieren?"},
    LocaleDefinition{
        "esES",
        "¿Seguro que quieres asignar todos los puntos de estadísticas disponibles?",
        "Asignar punto(s) de habilidad",
        "¿Quieres invertir en esta habilidad todos los puntos de habilidad disponibles?"},
    LocaleDefinition{
        "frFR",
        "Voulez-vous vraiment assigner tous les points de stat. disponibles ?",
        "Assigner des pts de comp.",
        "Investir tous les points de compétence actuellement utilisables dans cette compétence ?"},
    LocaleDefinition{
        "itIT",
        "Vuoi davvero assegnare tutti i punti caratteristica disponibili?",
        "Assegna punto/i abilità",
        "Vuoi investire in questa abilità tutti i punti abilità attualmente disponibili?"},
    LocaleDefinition{
        "koKR",
        "정말 남은 능력치 포인트 모두를 할당하시겠습니까?",
        "기술 포인트 할당",
        "이 스킬에 현재 사용할 수 있는 모든 스킬 포인트를 투자하시겠습니까?"},
    LocaleDefinition{
        "plPL",
        "Chcesz przypisać wszystkie punkty atrybutów?",
        "Przypisz punkty umiejętności",
        "Czy zainwestować wszystkie obecnie dostępne punkty umiejętności w tę umiejętność?"},
    LocaleDefinition{
        "esMX",
        "¿Realmente quieres asignar todos los puntos de atributo disponibles?",
        "Asignar punto(s) de habilidad",
        "¿Quieres invertir todos los puntos de habilidad disponibles en esta habilidad?"},
    LocaleDefinition{
        "jaJP",
        "使用可能な全ステータス・ポイントを割り振りますか？",
        "スキル・ポイントを振る",
        "現在使用可能なすべてのスキルポイントをこのスキルに割り振りますか？"},
    LocaleDefinition{
        "ptBR",
        "Tem certeza de que deseja atribuir todos os pontos de atributos disponíveis?",
        "Atribuir pontos de habilidades",
        "Deseja investir nesta habilidade todos os pontos de habilidade disponíveis?"},
    LocaleDefinition{
        "ruRU",
        "Вы уверены, что хотите распределить все полученные очки характеристик?",
        "Потратьте очки умений",
        "Вложить все доступные сейчас очки навыков в этот навык?"},
    LocaleDefinition{
        "zhCN",
        "你确定要分配所有可用的属性点吗？",
        "分配技能点",
        "要将当前可用的所有技能点投入此技能吗？"},
};

inline constexpr std::size_t SupportedLocaleCount =
    LocaleDefinitions.size();

constexpr auto HasUniquePrimaryProbes() noexcept -> bool {
    for (std::size_t left = 0; left < SupportedLocaleCount; ++left) {
        for (std::size_t right = left + 1;
             right < SupportedLocaleCount;
             ++right) {
            if (LocaleDefinitions[left].primaryProbe
                == LocaleDefinitions[right].primaryProbe) {
                return false;
            }
        }
    }
    return true;
}

static_assert(SupportedLocaleCount == 13);
static_assert(HasUniquePrimaryProbes());

inline auto FindLocaleByCode(
    std::string_view code
) noexcept -> std::optional<std::size_t> {
    for (std::size_t index = 0; index < SupportedLocaleCount; ++index) {
        if (LocaleDefinitions[index].code == code) return index;
    }
    return std::nullopt;
}

inline auto DetectLocale(
    std::string_view primaryProbe,
    std::string_view secondaryProbe
) noexcept -> std::optional<std::size_t> {
    std::optional<std::size_t> primaryMatch;
    if (!primaryProbe.empty()) {
        for (std::size_t index = 0; index < SupportedLocaleCount; ++index) {
            if (LocaleDefinitions[index].primaryProbe != primaryProbe) continue;
            if (primaryMatch) return std::nullopt;
            primaryMatch = index;
        }
    }

    std::array<bool, SupportedLocaleCount> secondaryMatches{};
    std::size_t secondaryMatchCount{};
    std::optional<std::size_t> uniqueSecondaryMatch;
    if (!secondaryProbe.empty()) {
        for (std::size_t index = 0; index < SupportedLocaleCount; ++index) {
            if (LocaleDefinitions[index].secondaryProbe
                != secondaryProbe) {
                continue;
            }
            secondaryMatches[index] = true;
            uniqueSecondaryMatch = index;
            ++secondaryMatchCount;
        }
    }

    if (primaryMatch) {
        if (secondaryMatchCount != 0
            && !secondaryMatches[*primaryMatch]) {
            return std::nullopt;
        }
        return primaryMatch;
    }
    if (secondaryMatchCount == 1) return uniqueSecondaryMatch;
    return std::nullopt;
}

inline auto DefaultShiftConfirmations()
    -> std::array<std::string, SupportedLocaleCount> {
    std::array<std::string, SupportedLocaleCount> confirmations;
    for (std::size_t index = 0; index < SupportedLocaleCount; ++index) {
        confirmations[index] = LocaleDefinitions[index].defaultConfirmation;
    }
    return confirmations;
}

} // namespace RuffnecKk::BulkSkillPointAllocation
