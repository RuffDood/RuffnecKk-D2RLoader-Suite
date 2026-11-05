#pragma once
#include "../src/policy.hpp"
namespace TestPreferences {
inline auto Pickup() -> RuffnecKk::AutoPickup::Config {
    RuffnecKk::AutoPickup::Config c;c.enabled=c.walkingEnabled=c.teleportEnabled=c.teleportGold=c.teleportItems=true;
    c.vanilla[0]=c.vanilla[1]=true;c.reimagined.fill(true);
    c.potionRouting.families={RuffnecKk::PotionRouting::Route{{1,2,0,0},2,true},RuffnecKk::PotionRouting::Route{{3,0,0,0},1,true},RuffnecKk::PotionRouting::Route{{4,0,0,0},1,true}};
    return c;
}
inline auto Interact() -> AutoInteract::Config {
    AutoInteract::Config c;c.enabled=c.chests=c.corpses=c.rocks=c.weaponRacks=c.armorRacks=c.diagnostics=true;return c;
}
}
