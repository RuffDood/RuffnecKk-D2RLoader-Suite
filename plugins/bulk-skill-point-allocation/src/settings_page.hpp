#pragma once
#include <RuffnecKk/live_settings.hpp>
#include <array>
#include <string>
#include <sstream>
#include "removal_settings.hpp"
namespace RuffnecKk::BulkSkillPointAllocation::LivePage {
inline std::string BindingText(Removal::Binding b) {
 std::string out;
 if(b.modifier==Removal::Modifier::Control) out="Ctrl+";
 else if(b.modifier==Removal::Modifier::Shift) out="Shift+";
 else if(b.modifier==Removal::Modifier::Alt) out="Alt+";
 switch(b.button){case Removal::Button::Right:return out+"RightClick";case Removal::Button::Middle:return out+"MiddleClick";case Removal::Button::Mouse4:return out+"Mouse4";case Removal::Button::Mouse5:return out+"Mouse5";}
 throw std::invalid_argument("Unknown refund control.");
}
inline bool Known(std::string_view key){
 return key=="enabled" || key=="skillPointsPerCtrlClick" || key=="confirmShiftAllocation" || key=="diagnostics" || key=="refund.single_removal" || key=="refund.batch_removal" || key=="refund.all_removal" || key=="refund.batch_points";
}
inline std::string Value(const Removal::Configuration& c,std::string_view key){
 using RuffnecKk::LiveSettings::Quote;
 if(key=="enabled") return std::string(c.allocation.enabled ? "true" : "false");
 if(key=="skillPointsPerCtrlClick") return std::to_string(c.allocation.skillPointsPerCtrlClick);
 if(key=="confirmShiftAllocation") return std::string(c.allocation.confirmShiftAllocation ? "true" : "false");
 if(key=="diagnostics") return std::string(c.allocation.diagnostics ? "true" : "false");
 if(key=="refund.single_removal") return Quote(BindingText(c.removal.single));
 if(key=="refund.batch_removal") return Quote(BindingText(c.removal.batch));
 if(key=="refund.all_removal") return Quote(BindingText(c.removal.all));
 if(key=="refund.batch_points") return std::to_string(c.removal.batchPoints);
 throw std::invalid_argument("Unknown setting.");
}
inline void Copy(Removal::Configuration& to, const Removal::Configuration& from, std::string_view key){
 if(key=="skillPointsPerCtrlClick") { to.allocation.skillPointsPerCtrlClick=from.allocation.skillPointsPerCtrlClick; return; }
 if(key=="confirmShiftAllocation") { to.allocation.confirmShiftAllocation=from.allocation.confirmShiftAllocation; return; }
 if(key=="diagnostics") { to.allocation.diagnostics=from.allocation.diagnostics; return; }
 if(key=="refund.single_removal") { to.removal.single=from.removal.single; return; }
 if(key=="refund.batch_removal") { to.removal.batch=from.removal.batch; return; }
 if(key=="refund.all_removal") { to.removal.all=from.removal.all; return; }
 if(key=="refund.batch_points") { to.removal.batchPoints=from.removal.batchPoints; return; }
 if(key!="enabled") throw std::invalid_argument("Unknown setting.");
}
inline std::string Schema(bool overridden=false){
 std::string out="title = \"Bulk Skill Point Allocation\"\n";
 out += "[[setting]]\ntype = \"text\"\nlabel = \"Changes save immediately. The master switch applies after restart.\"\n";
 (void)overridden;
 out += "[[setting]]\nkey = \"enabled\"\ntype = \"bool\"\nlabel = \"Enable at next start\"\nrestart = true\n";
 out += "[[setting]]\nkey = \"skillPointsPerCtrlClick\"\ntype = \"int\"\nlabel = \"Allocation batch size\"\nmin = 1\nmax = 1000\nstep = 1\n";
 out += "[[setting]]\nkey = \"confirmShiftAllocation\"\ntype = \"bool\"\nlabel = \"Confirm Shift allocation\"\n";
 out += "[[setting]]\nkey = \"diagnostics\"\ntype = \"bool\"\nlabel = \"Diagnostics\"\n";
 out += "[[setting]]\nkey = \"refund.single_removal\"\ntype = \"string\"\nlabel = \"Refund one point\"\n";
 out += "[[setting]]\nkey = \"refund.batch_removal\"\ntype = \"string\"\nlabel = \"Refund a batch\"\n";
 out += "[[setting]]\nkey = \"refund.all_removal\"\ntype = \"string\"\nlabel = \"Refund all removable points\"\n";
 out += "[[setting]]\nkey = \"refund.batch_points\"\ntype = \"int\"\nlabel = \"Refund batch size\"\nmin = 1\nmax = 1000\nstep = 1\n";
 return out;
}
}
