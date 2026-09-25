// Reuse the deliberately limited P1 backend; no production generator changes.
#define main p1_original_generator_main
#include "../continuation/generate.cpp"
#undef main

int main(int argc, char** argv) try {
    require(argc == 2, "flow_generate output.cpp");
    // Match the pinned N64Recomp CLI's decoder configuration.
    RabbitizerConfig_Cfg.pseudos.pseudoMove = false;
    RabbitizerConfig_Cfg.pseudos.pseudoBeqz = false;
    RabbitizerConfig_Cfg.pseudos.pseudoBnez = false;
    RabbitizerConfig_Cfg.pseudos.pseudoNot = false;
    RabbitizerConfig_Cfg.pseudos.pseudoBal = false;
    Context ctx{};
    ctx.sections.resize(1); ctx.section_functions.resize(1);
    ctx.sections[0].ram_addr = 0x80000000;
    ctx.sections[0].size = 0x800000; ctx.sections[0].executable = true;
    // root keeps local multiply/FP compare values live across three calls.
    // r17 selects taken/not-taken paths for ordinary and likely branches.
    std::vector<uint32_t> root = {
        0x3C08FFFF, 0x3508FFFF, 0x2409000B, 0x01090019, 0x46000032,
        0x24100003,
        0x0C1C0040, 0x254A0001, // loop: jal leaf; addiu t2,t2,1
        0x2610FFFF, 0x1E00FFFC, 0x256B0001, // --s0; bgtz s0,loop; ++t3
        0x12200002, 0x258C0001, 0x25AD0001, // beq s1,zero,+2; ++t4; ++t5
        0x52200002, 0x25CE0001, 0x25EF0001, // beql s1,zero,+2; ++t6; ++t7
        0x00009010, 0x00009812, // mfhi s2; mflo s3
        0x45010002, 0x24140001, 0x24140063, // bc1t; s4=1; s4=99
        0x03E00008, 0
    };
    std::vector<uint32_t> leaf = {
        0x24080002, 0x24090003, 0x01090019, 0x4600003C, // different HI/LO/c1cs
        0x26B50001, 0x03E00008, 0
    };
    for (auto& w: root) w = byteswap(w);
    for (auto& w: leaf) w = byteswap(w);
    ctx.functions.emplace_back(0x80700000, 0, root, "flow_root", 0);
    ctx.functions.emplace_back(0x80700100, 0, leaf, "flow_leaf", 0);
    for (size_t i=0; i<ctx.functions.size(); ++i) {
        ctx.functions_by_vram[ctx.functions[i].vram].push_back(i);
        ctx.section_functions[0].push_back(i);
    }
    std::ofstream out(argv[1]);
    out << "#include \"runtime.hpp\"\nvoid flow_leaf(uint8_t*, recomp_context*);\n";
    for (size_t i=0; i<ctx.functions.size(); ++i) {
        std::vector<std::vector<uint32_t>> statics(1);
        require(recompile_function(ctx, i, out, statics, false), "native generation failed");
        std::ostringstream body;
        ContinuationGenerator gen(body, ctx);
        require(recompile_function_custom(gen, ctx, i, body, statics, false), "P1 generation failed");
        out << "static p1::Action flow_step_" << i << "(uint8_t* rdram,recomp_context* ctx,p1::Frame& frame) {\n"
            << "auto &hi=frame.hi,&lo=frame.lo,&result=frame.result; auto &c1cs=frame.c1cs;\n"
            << "switch(frame.continuation) {case 0:goto entry;\n";
        for(auto [pc,target]:gen.calls) out << "case " << pc << "u:goto resume_" << pc << ";\n";
        out << "default:throw std::runtime_error(\"bad continuation\");}\nentry:;\n" << body.str() << "}\n";
    }
    out << "p1::Action flow_dispatch(uint8_t*r,recomp_context*c,p1::Frame&f) {switch(f.function){\n";
    for(size_t i=0;i<ctx.functions.size();++i) out << "case " << fid(ctx.functions[i].vram) << "ull:return flow_step_" << i << "(r,c,f);\n";
    out << "default:throw std::runtime_error(\"bad function\");}}\n";
    // Assert the reused backend still fails closed on unsupported transfers.
    std::ostringstream rejected; ContinuationGenerator gen(rejected,ctx);
    auto rejects = [](auto op) { bool failed=false; try {op();} catch(const std::runtime_error&) {failed=true;} require(failed,"unsupported transfer accepted"); };
    rejects([&]{gen.emit_function_call_by_register(25);});
    rejects([&]{gen.emit_function_call_lookup(0x80700100);});
    require(bool(out),"output failed");
} catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
