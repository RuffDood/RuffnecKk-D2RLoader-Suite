#pragma once
#include "../config_edit.hpp"
#include "../interact_settings.hpp"
#include <unordered_set>

namespace AutoInteract {
namespace S = Reimagined::InteractSettings;
inline auto ValuesFor(const Config& c) noexcept -> S::Values {
    S::Values v;
    v.flags = (c.enabled ? S::Enabled : 0U) | (c.chests ? S::Chests : 0U) | (c.corpses ? S::Corpses : 0U)
        | (c.rocks ? S::Rocks : 0U) | (c.weaponRacks ? S::WeaponRacks : 0U) | (c.armorRacks ? S::ArmorRacks : 0U)
        | (c.lockedChests ? S::LockedChests : 0U);
    v.range = c.interactRange; v.intervalMs = c.intervalMs; v.retryMs = c.retryMs; return v;
}
inline auto WithValues(Config c, const S::Values& v) noexcept -> Config {
    c.enabled = (v.flags & S::Enabled) != 0; c.chests = (v.flags & S::Chests) != 0; c.corpses = (v.flags & S::Corpses) != 0;
    c.rocks = (v.flags & S::Rocks) != 0; c.weaponRacks = (v.flags & S::WeaponRacks) != 0; c.armorRacks = (v.flags & S::ArmorRacks) != 0;
    c.lockedChests = (v.flags & S::LockedChests) != 0; c.interactRange = v.range; c.intervalMs = v.intervalMs; c.retryMs = v.retryMs;
    return c;
}

inline constexpr std::array<std::string_view, 12> Keys{"enabled", "validate_only", "chests", "corpses", "rocks", "weapon_racks", "armor_racks", "locked_chests", "diagnostics_enabled", "interval_ms", "retry_ms", "interact_range"};
inline auto FieldText(const Config& c, std::size_t k) -> std::string {
    if (k < 9) { const std::array values{c.enabled,c.validateOnly,c.chests,c.corpses,c.rocks,c.weaponRacks,c.armorRacks,c.lockedChests,c.diagnostics}; return values[k] ? "true" : "false"; }
    return std::to_string(k == 9 ? c.intervalMs : k == 10 ? c.retryMs : c.interactRange);
}
inline auto MatchSettings(std::string& text) -> bool {
    bool metadata{};
    for (std::size_t start = 0; start < text.size();) {
        auto end = text.find('\n',start); if (end == std::string::npos) end = text.size();
        auto line = RuffnecKk::AutoPickup::Trim(RuffnecKk::AutoPickup::WithoutComment(std::string_view(text).substr(start,end-start)));
        if (!line.empty() && line.front() == '[') metadata = line == "[d2rl]";
        else if (metadata) {
            const auto eq = line.find('=');
            if (eq != std::string_view::npos && RuffnecKk::AutoPickup::Trim(line.substr(0,eq)) == "match") {
                const auto value = RuffnecKk::AutoPickup::Trim(line.substr(eq+1)); if (value.empty() || value.front() != '[') return false;
                const auto bracket = static_cast<std::size_t>(value.data()-text.data());
                bool comment{}; char quote{}, previous='['; std::string token; std::unordered_set<std::string> found;
                for (auto pos=bracket+1;pos<text.size();++pos) {
                    const auto ch=text[pos];
                    if (comment) { if(ch=='\n') comment=false; continue; }
                    if (quote) { if(ch==quote) { found.insert(token);quote=0;previous='s'; } else if(ch=='\\' && quote=='"' && pos+1<text.size()) token+=text[++pos]; else token+=ch; }
                    else if(ch=='#') comment=true;
                    else if(ch=='"' || ch=='\'') {quote=ch;token.clear();}
                    else if(ch==']') {
                        std::string extra;
                        for(std::size_t k=0;k<Keys.size();++k) if(k!=8) {const auto name="auto_interact."+std::string(Keys[k]); if(!found.contains(name)) { if(!extra.empty() || (previous!='[' && previous!=',')) extra+=", "; extra+='"'+name+'"'; }}
                        text.insert(pos,extra);return true;
                    } else if(ch!=' ' && ch!='\t' && ch!='\r' && ch!='\n') previous=ch;
                }
                return false;
            }
        }
        start=end==text.size()?end:end+1;
    }
    std::string list="[";for(std::size_t k=0;k<Keys.size();++k) if(k!=8) {if(list.size()>1) list+=", ";list+='"'+std::string("auto_interact.")+std::string(Keys[k])+'"';}list+=']';
    RuffnecKk::AutoPickup::PatchScalar(text,"d2rl","match",list);return true;
}
inline auto WriteSection(std::string text, const Config& config, std::string& output, std::string& error) -> bool {
    for(std::size_t i=0;i<Keys.size();++i) RuffnecKk::AutoPickup::PatchScalar(text,"auto_interact",Keys[i],FieldText(config,i));
    if(!MatchSettings(text)) {error="Invalid loader match list.";return false;}
    RuffnecKk::AutoPickup::Config check;
    if(!RuffnecKk::AutoPickup::ParseConfig(text,check,error)) return false;
    output=std::move(text);return true;
}
inline auto SaveSettings(std::string text, const S::Values& expected, const S::Values& draft, std::string& output, Config& applied, std::string& error) -> bool {
    RuffnecKk::AutoPickup::Config root;
    if(!S::Valid(draft) || !RuffnecKk::AutoPickup::ParseConfig(text,root,error)) {if(error.empty())error="Invalid settings.";return false;}
    if(!S::Same(ValuesFor(root.interact),expected)) {error="Auto Interact settings changed outside this window. Close and reopen before saving.";return false;}
    applied=WithValues(root.interact,draft);
    return WriteSection(std::move(text),applied,output,error);
}
inline auto MigrateLegacy(std::string text, std::string_view legacy, std::string& output, std::string& error) -> bool {
    RuffnecKk::AutoPickup::Config root;
    if(!RuffnecKk::AutoPickup::ParseConfig(text,root,error)) return false;
    if(root.interactConfigured) {output=std::move(text);return true;}
    Config config;if(!ParseConfig(legacy,config,error)) return false;
    return WriteSection(std::move(text),config,output,error);
}
}
