// Reuse the pinned CLI's ELF/configuration/overlay/static-function discovery.
// Only its generation entry is replaced; ordinary generated C is never parsed.
#include "generator.hpp"
#include <fstream>
#include <iostream>
namespace N64Recomp {
bool recompile_continuation(const Context& ctx,size_t index,std::ostream& out,
    std::span<std::vector<uint32_t>> statics,bool refs) {
    return sbk::generator::emit(ctx,index,out,statics,refs);
}
}
#define main pinned_cli_main
#define recompile_function recompile_continuation
#include "main.cpp"
#undef recompile_function
#undef main
int main(int argc,char** argv) try {
    if(argc!=3) {std::cerr<<"usage: generate config.toml manifest.tsv\n";return 2;}
    int result=pinned_cli_main(2,argv);
    if(result) return result;
    std::ofstream manifest(argv[2]);
    manifest<<"kind\tid\tname\tsection_rom\tsection_size\toffset\tcontinuations\tscratch\n";
    for(const auto& [id,f]:sbk::generator::manifest)
        manifest<<"function\t"<<id<<'\t'<<f.name<<'\t'<<f.section_rom<<'\t'<<f.section_size<<'\t'<<f.offset<<'\t'<<f.continuations<<'\t'<<f.scratch<<'\n';
    for(const auto& [id,name]:sbk::generator::hle_manifest)
        manifest<<"hle\t"<<id<<'\t'<<name<<"\t0\t0\t0\t0\t0\n";
    if(!manifest) throw std::runtime_error("Manifest write failed");
    return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
