#include "recompui/config.h"
#include "recompui/resolution.h"
#include "recompui/renderer.h"
#include "librecomp/game.hpp"
#include <cstdlib>
#include <iostream>
#include <memory>
static std::unique_ptr<recomp::config::Config> cfg;
static ultramodern::renderer::GraphicsConfig applied;
namespace recomp {
std::filesystem::path get_config_path() { return std::getenv("SBK_CONFIG_TEST_DIR"); }
const Version& get_project_version() { static const Version version{1, 0, 0}; return version; }
}
namespace recompui::config {
recomp::config::Config& create_config_tab(const std::string& name,const std::string& id,bool confirm) { cfg=std::make_unique<recomp::config::Config>(name,id,confirm); return *cfg; }
recomp::config::Config& get_config(const std::string&) { return *cfg; }
}
namespace recompui::renderer { RT64::UserConfiguration::Antialiasing RT64MaxMSAA() { return RT64::UserConfiguration::Antialiasing::MSAA4X; } }
namespace ultramodern::renderer { void set_graphics_config(const GraphicsConfig& c) { applied=c; } }
namespace recompui { bool is_steam_deck() { return false; } }
int main(int argc,char** argv) {
 auto& c=recompui::config::create_graphics_tab();
 c.load_config();
 if(argc>1 && std::string(argv[1])=="save") {
  c.set_option_value("res_option",static_cast<uint32_t>(std::atoi(argv[2])));
  c.set_option_value("ar_option",static_cast<uint32_t>(std::atoi(argv[3])));
  c.set_option_value("ds_option",static_cast<uint32_t>(std::atoi(argv[4])));
  c.set_option_value("wm_option",static_cast<uint32_t>(std::atoi(argv[5])));
  if(argc>6) c.set_option_value("vsync",static_cast<uint32_t>(std::atoi(argv[6])));
  if(!c.save_config()) return 2;
 }
 if(argc>1 && std::string(argv[1])=="toggle") recompui::config::graphics::toggle_fullscreen();
 const size_t vsync_index=c.get_config_schema().options_by_id.at("vsync");
 if(argc>1 && std::string(argv[1])=="caps") {
  recompui::config::graphics::update_vsync_off_supported(false);
  std::cout<<"{\"off_disabled\":"<<c.get_enum_option_disabled(vsync_index,1)<<",\"on_disabled\":"<<c.get_enum_option_disabled(vsync_index,0)
   <<",\"details\":\""<<c.get_enum_option_details(vsync_index)<<"\",";
  recompui::config::graphics::update_vsync_off_supported(true);
  std::cout<<"\"off_disabled_after\":"<<c.get_enum_option_disabled(vsync_index,1)<<",\"details_after\":\""<<c.get_enum_option_details(vsync_index)<<"\"}\n";
  return 0;
 }
 auto j=c.get_json_config();
 j["applied_rr"]=static_cast<int>(applied.rr_option);
 j["applied_ds"]=applied.ds_option;
 j["applied_wm"]=static_cast<int>(applied.wm_option);
 j["applied_rr_manual"]=applied.rr_manual_value;
 j["applied_vsync"]=static_cast<int>(applied.vsync_option);
 j["applied_ar"]=static_cast<int>(applied.ar_option);
 j["applied_res"]=static_cast<int>(applied.res_option);
 j["backend_ds_bad"]=recompui::resolution::scale(ultramodern::renderer::Resolution::Original,65535).downsample;
 for(const auto& o:c.get_config_schema().options) { j["hidden"][o.id]=o.hidden; }
 std::cout<<j.dump()<<'\n';
}
