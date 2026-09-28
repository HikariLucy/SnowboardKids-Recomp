#include "generator.hpp"
#include <fstream>
#include <iostream>
using namespace N64Recomp;
int main(int argc, char** argv) try {
    if (argc != 2) return 2;
    RabbitizerConfig_Cfg.pseudos.pseudoMove = false;
    RabbitizerConfig_Cfg.pseudos.pseudoBeqz = false;
    RabbitizerConfig_Cfg.pseudos.pseudoBnez = false;
    RabbitizerConfig_Cfg.pseudos.pseudoNot = false;
    RabbitizerConfig_Cfg.pseudos.pseudoBal = false;
    Context ctx{};
    ctx.sections.resize(1); ctx.section_functions.resize(1);
    ctx.sections[0].ram_addr=0x80700000; ctx.sections[0].rom_addr=0x1000;
    ctx.sections[0].size=0x1000; ctx.sections[0].executable=true;
    std::vector<uint32_t> root={
        0x3C08FFFF,0x3508FFFF,0x2409000B,0x01090019,0x46000032,
        0x24100003,0x0C1C0040,0x254A0001,
        0x2610FFFF,0x1E00FFFC,0x256B0001,
        0x12200002,0x258C0001,0x25AD0001,
        0x52200002,0x25CE0001,0x25EF0001,
        0x00009010,0x00009812,0x45010002,0x24140001,0x24140063,
        0x03E00008,0};
    std::vector<uint32_t> leaf={0x24080002,0x24090003,0x01090019,0x4600003C,0x26B50001,0x03E00008,0};
    for(auto&w:root) w=byteswap(w);
    for(auto&w:leaf) w=byteswap(w);
    ctx.functions.emplace_back(0x80700000,0x1000,root,"flow_root",0);
    ctx.functions.emplace_back(0x80700100,0x1100,leaf,"flow_leaf",0);
    auto add=[&](uint32_t pc,std::vector<uint32_t> words,const char* name) {
        for(auto& w:words) w=byteswap(w);
        ctx.functions.emplace_back(pc,0x1000+pc-0x80700000,std::move(words),name,0);
    };
    // The delay slot changes t9: the indirect target must already be latched.
    add(0x80700200,{0x3C198070,0x37390100,0x0320F809,0x27390200,
                   0x081C00C0,0x26940001},"indirect_root");
    add(0x80700300,{0x26D60001,0x03E00008,0},"tail_leaf");
    add(0x80700400,{0x00044080,0x3C198070,0x0328C821,0x8F390600,
                   0x03200008,0,0x2417000B,0x03E00008,0,
                   0x24170016,0x03E00008,0},"switch_root");
    ctx.rom.resize(0x2000);
    for(unsigned i=0;i<2;++i) {
        uint32_t target=0x80700418+i*12;
        for(unsigned b=0;b<4;++b) ctx.rom[0x1600+i*4+b]=uint8_t(target>>(24-8*b));
    }
    for(size_t i=0;i<ctx.functions.size();++i) {
        ctx.functions_by_vram[ctx.functions[i].vram].push_back(i);
        ctx.section_functions[0].push_back(i);
    }
    std::ofstream out(argv[1]);
    out << "#include \"continuation/dispatch.hpp\"\n";
    for(size_t i=0;i<ctx.functions.size();++i) {
        std::vector<std::vector<uint32_t>> statics(1);
        if(!sbk::generator::emit(ctx,i,out,statics,false)) return 1;
    }
    for(auto& f:ctx.functions) f.name="reference_"+f.name;
    for(auto& f:ctx.functions) out << "void " << f.name << "(uint8_t*,recomp_context*);\n";
    for(size_t i=0;i<ctx.functions.size();++i) {
        std::vector<std::vector<uint32_t>> statics(1);
        if(!recompile_function(ctx,i,out,statics,false)) return 1;
    }
    return out.good()?0:1;
} catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
