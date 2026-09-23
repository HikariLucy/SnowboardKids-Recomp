// Experimental N64Recomp backend. Ordinary instruction emission is delegated
// to the pinned CGenerator; no generated C is parsed or manually reimplemented.
#include "recompiler/generator.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <algorithm>
using namespace N64Recomp;
static uint64_t fid(uint32_t pc) { return (uint64_t{1} << 32) | pc; }
static void require(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }

class ContinuationGenerator final : public Generator {
    std::ostream& out;
    CGenerator c;
    const Context& context;
    mutable uint32_t call_pc = 0;
public:
    mutable std::map<uint32_t, uint64_t> calls;
    ContinuationGenerator(std::ostream& o, const Context& ctx): out(o), c(o), context(ctx) {}
    void call(uint64_t target) const {
        require(call_pc != 0, "Missing call instruction identity");
        require(calls.emplace(call_pc, target).second, "Duplicate call PC unsupported in P1");
        out << "frame.continuation = " << call_pc << "u; return {" << target << "ull};\n"
            << "resume_" << call_pc << ":;\n";
        call_pc = 0;
    }
    void emit_function_start(const std::string&, size_t) const override {}
    void emit_function_end() const override { out << "return {};\n"; }
    void emit_return(const Context&, size_t) const override { out << "return {};\n"; }
    void emit_function_call(const Context& ctx, size_t index) const override {
        call(fid(ctx.functions.at(index).vram));
    }
    void emit_function_call_lookup(uint32_t) const override { throw std::runtime_error("Lookup calls unsupported"); }
    void emit_function_call_by_register(int) const override { throw std::runtime_error("Indirect calls unsupported"); }
    void emit_function_call_reference_symbol(const Context&, uint16_t, size_t, uint32_t) const override {
        throw std::runtime_error("Reference calls unsupported");
    }
    void emit_named_function_call(const std::string& name) const override {
        for (const auto& f : context.functions) if (f.name == name) { call(fid(f.vram)); return; }
        throw std::runtime_error("Unknown named call");
    }
    void emit_comment(const std::string& text) const override {
        // Pinned backend exposes instruction PC only through this callback.
        // This restricted probe accepts direct JAL only; general support needs
        // a typed begin_instruction callback instead of disassembly metadata.
        if (text.find(": jal ") != std::string::npos) call_pc = std::stoul(text.substr(2, 8), nullptr, 16);
        c.emit_comment(text);
    }
    void process_binary_op(const BinaryOp& op, const InstructionContext& ctx) const override { c.process_binary_op(op, ctx); }
    void process_unary_op(const UnaryOp& op, const InstructionContext& ctx) const override { c.process_unary_op(op, ctx); }
    void process_store_op(const StoreOp& op, const InstructionContext& ctx) const override { c.process_store_op(op, ctx); }
    void emit_goto(const std::string& target) const override { c.emit_goto(target); }
    void emit_label(const std::string& label_name) const override { c.emit_label(label_name); }
    void emit_jtbl_addend_declaration(const JumpTable& jtbl, int reg) const override { throw std::runtime_error("emit_jtbl_addend_declaration unsupported in P1"); }
    void emit_branch_condition(const ConditionalBranchOp& op, const InstructionContext& ctx) const override { c.emit_branch_condition(op, ctx); }
    void emit_branch_close() const override { c.emit_branch_close(); }
    void emit_switch(const Context& recompiler_context, const JumpTable& jtbl, int reg) const override { throw std::runtime_error("emit_switch unsupported in P1"); }
    void emit_case(int case_index, const std::string& target_label) const override { c.emit_case(case_index, target_label); }
    void emit_switch_error(uint32_t instr_vram, uint32_t jtbl_vram) const override { c.emit_switch_error(instr_vram, jtbl_vram); }
    void emit_switch_close() const override { c.emit_switch_close(); }
    void emit_check_fr(int fpr) const override { c.emit_check_fr(fpr); }
    void emit_check_nan(int fpr, bool is_double) const override { c.emit_check_nan(fpr, is_double); }
    void emit_cop0_status_read(int reg) const override { c.emit_cop0_status_read(reg); }
    void emit_cop0_status_write(int reg) const override { c.emit_cop0_status_write(reg); }
    void emit_cop1_cs_read(int reg) const override { c.emit_cop1_cs_read(reg); }
    void emit_cop1_cs_write(int reg) const override { c.emit_cop1_cs_write(reg); }
    void emit_muldiv(InstrId instr_id, int reg1, int reg2) const override { c.emit_muldiv(instr_id, reg1, reg2); }
    void emit_syscall(uint32_t instr_vram) const override { throw std::runtime_error("emit_syscall unsupported in P1"); }
    void emit_do_break(uint32_t instr_vram) const override { throw std::runtime_error("emit_do_break unsupported in P1"); }
    void emit_pause_self() const override { throw std::runtime_error("emit_pause_self unsupported in P1"); }
    void emit_trigger_event(uint32_t event_index) const override { throw std::runtime_error("emit_trigger_event unsupported in P1"); }
};
static std::vector<uint32_t> caller(uint32_t target, uint16_t output, uint16_t factor) {
    // Synthetic MIPS test callers, not game patches. hi/lo/c1cs survive a call;
    // MULTU also gives result a nonzero value for conservative frame encoding.
    std::vector<uint32_t> w = {
        0x27BDFFE8, 0xAFBF0014, 0x3C08FFFF, 0x3508ABCD,
        uint32_t(0x24090000 | factor), 0x01090019, 0x46000032,
        uint32_t(0x0C000000 | ((target >> 2) & 0x03FFFFFF)), 0x254A0001,
        0x00005810, 0x00006012, 0x3C088000,
        uint32_t(0xAD0B0000 | output), uint32_t(0xAD0C0000 | (output + 4)),
        0x45010002, 0x240D0001, 0x240D0063,
        uint32_t(0xAD0D0000 | (output + 8)),
        uint32_t(0x8D090000 | (output + 12)), 0x25290001,
        uint32_t(0xAD090000 | (output + 12)),
        0x440E0800, uint32_t(0xAD0E0000 | (output + 16)), // MFC1 t6,f1 and store after resume
        0x8FBF0014, 0x27BD0018, 0x03E00008, 0
    };
    for (auto& value : w) value = byteswap(value);
    return w;
}
int main(int argc, char** argv) try {
    require(argc == 4 || argc == 5, "usage: generate words.txt output.cpp identity [reverse]");
    Context ctx{};
    ctx.sections.resize(1);
    ctx.sections[0].ram_addr = 0x80000000;
    ctx.sections[0].size = 0x800000;
    ctx.sections[0].executable = true;
    ctx.section_functions.resize(1);
    std::ifstream words(argv[1]);
    require(bool(words), "Missing extracted words");
    std::vector<uint32_t> leaf;
    uint32_t value;
    while (words >> std::hex >> value) leaf.push_back(byteswap(value));
    require(leaf.size() == 17, "Unexpected game function size");
    ctx.functions.emplace_back(0x80700000, 0, caller(0x80700100, 0x400, 7), "probe_root", 0);
    ctx.functions.emplace_back(0x80700100, 0, caller(0x80001858, 0x420, 11), "probe_middle", 0);
    ctx.functions.emplace_back(0x80001858, 0, leaf, "requestControllerPakFreeSpaceUpdate", 0);
    ctx.functions.emplace_back(0x800A0D30, 0, std::vector<uint32_t>{0}, "osSendMesg_recomp", 0, false, true);
    ctx.functions.emplace_back(0x800A0990, 0, std::vector<uint32_t>{0}, "osRecvMesg_recomp", 0, false, true);
    if (argc == 5) std::reverse(ctx.functions.begin(), ctx.functions.end());
    for (size_t i = 0; i < ctx.functions.size(); ++i) {
        ctx.functions_by_vram[ctx.functions[i].vram].push_back(i);
        ctx.section_functions[0].push_back(i);
    }
    // Sort emission independently from Context indexing to exercise stable IDs.
    std::vector<size_t> order;
    for (size_t i = 0; i < ctx.functions.size(); ++i) if (!ctx.functions[i].reimplemented) order.push_back(i);
    std::sort(order.begin(), order.end(), [&](auto a, auto b) { return ctx.functions[a].vram < ctx.functions[b].vram; });
    std::ofstream out(argv[2]);
    out << "#include \"runtime.hpp\"\n";
    for (auto& f : ctx.functions) out << "void " << f.name << "(uint8_t*, recomp_context*);\n";
    std::map<uint64_t, std::map<uint32_t, uint64_t>> schemas;
    for (size_t index : order) {
        std::vector<std::vector<uint32_t>> statics(1);
        require(recompile_function(ctx, index, out, statics, false), "Baseline compilation failed");
        std::ostringstream body;
        ContinuationGenerator generator(body, ctx);
        require(recompile_function_custom(generator, ctx, index, body, statics, false), "Continuation compilation failed");
        auto function = fid(ctx.functions[index].vram);
        schemas[function] = generator.calls;
        out << "static p1::Action step_" << function << "(uint8_t* rdram, recomp_context* ctx, p1::Frame& frame) {\n"
            << "auto &hi = frame.hi, &lo = frame.lo, &result = frame.result; auto &c1cs = frame.c1cs;\n"
            << "switch (frame.continuation) { case 0: goto entry;\n";
        for (auto [pc, target] : generator.calls) out << "case " << pc << "u: goto resume_" << pc << ";\n";
        out << "default: throw std::runtime_error(\"Unknown continuation\"); }\nentry:;\n" << body.str() << "}\n";
    }
    out << "namespace p1 {\nconst char* const build_identity = \"" << argv[3] << "\";\n"
        << "void reference_root(uint8_t* r, recomp_context* c) { probe_root(r,c); }\n"
        << "Action dispatch(uint8_t* r, recomp_context* c, Frame& f) { switch(f.function) {\n";
    for (auto& [f, calls] : schemas) out << "case " << f << "ull: return step_" << f << "(r,c,f);\n";
    out << "default: throw std::runtime_error(\"Unknown function\"); }}\n"
        << "uint64_t pending_callee(const Frame& f) { switch(f.function) {\n";
    for (auto& [f, calls] : schemas) {
        out << "case " << f << "ull: switch(f.continuation) { case 0: return 0;\n";
        for (auto [pc, target] : calls) out << "case " << pc << "u: return " << target << "ull;\n";
        out << "default: throw std::runtime_error(\"Unknown continuation\"); }\n";
    }
    out << "default: throw std::runtime_error(\"Unknown function\"); }}\n}\n";
    require(bool(out), "Output failed");
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
