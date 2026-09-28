#pragma once

#include "removal_policy.hpp"
#include "policy.hpp"
#include <toml++/toml.hpp>

namespace RuffnecKk::BulkSkillPointAllocation::Removal {

struct Configuration {
    BulkSkillPointAllocation::Settings allocation;
    Removal::Settings removal;
    std::optional<std::filesystem::path> source;
};

inline std::string ReadConfigurationFile(const std::filesystem::path& path) {
    if (!std::filesystem::is_regular_file(path))
        throw std::invalid_argument("configuration must be a regular file");
    const auto size = std::filesystem::file_size(path);
    if (size > MaximumConfigBytes)
        throw std::invalid_argument("configuration exceeds 64 KiB");
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("could not open configuration");
    std::string text(static_cast<std::size_t>(size), '\0');
    if (size) input.read(text.data(), static_cast<std::streamsize>(size));
    if (!input || input.peek() != std::ifstream::traits_type::eof())
        throw std::runtime_error("could not read configuration exactly");
    return text;
}

inline Configuration ParseToml(std::string_view text) {
    const auto table = toml::parse(text);
    Configuration configuration;
    Json::Object legacy;
    for (const auto& [key, node] : table) {
        if (key.str() == "refund") continue;
        Json::Scalar scalar{};
        if (node.is_boolean()) {
            scalar.kind = Json::ScalarKind::Boolean;
            scalar.boolean = *node.value<bool>();
        } else if (node.is_integer()) {
            scalar.kind = Json::ScalarKind::Integer;
            scalar.integer = *node.value<std::int64_t>();
        } else if (node.is_string()) {
            scalar.kind = Json::ScalarKind::String;
            scalar.string = *node.value<std::string>();
        } else throw std::invalid_argument("invalid allocation setting type");
        legacy.emplace(std::string(key.str()), std::move(scalar));
    }
    ApplyGameplayConfig(legacy, configuration.allocation);
    if (const auto* refund = table.get("refund")) {
        if (!refund->is_table()) throw std::invalid_argument("refund must be a table");
        for (const auto& [key, node] : *refund->as_table()) {
            auto& settings = configuration.removal;
            if (key.str() == "batch_points") {
                if (!node.is_integer())
                    throw std::invalid_argument("refund.batch_points must be an integer");
                const auto value = *node.value<std::int64_t>();
                if (value < 1 || value > 1000)
                    throw std::invalid_argument("refund.batch_points must be 1 through 1000");
                settings.batchPoints = static_cast<int>(value);
                continue;
            }
            if (key.str() != "single_removal" && key.str() != "batch_removal"
                && key.str() != "all_removal")
                throw std::invalid_argument("unknown refund setting: " + std::string(key.str()));
            if (!node.is_string())
                throw std::invalid_argument("removal controls must be strings");
            const auto binding = ParseBinding(*node.value<std::string>());
            if (key.str() == "single_removal") settings.single = binding;
            else if (key.str() == "batch_removal") settings.batch = binding;
            else settings.all = binding;
        }
    }
    Validate(configuration.removal);
    return configuration;
}

// Candidate paths retain the existing active-mod -> scope -> global order.
// A present invalid TOML is an error, never permission to load another file.
inline Configuration LoadConfiguration(
    const std::vector<std::filesystem::path>& tomlCandidates,
    const std::vector<std::filesystem::path>& jsonCandidates) {
    for (const auto& path : tomlCandidates) {
        if (!std::filesystem::exists(path)) continue;
        auto result = ParseToml(ReadConfigurationFile(path));
        result.source = path;
        return result;
    }
    Configuration result;
    if (const auto document = LoadFirstDocument(jsonCandidates)) {
        ApplyGameplayConfig(document->object, result.allocation);
        result.source = document->source;
    }
    return result;
}

} // namespace RuffnecKk::BulkSkillPointAllocation::Removal
