#pragma once
#include "recompiler/generator.h"
#include <map>
#include <sstream>
#include <stdexcept>
#include <iomanip>
#include <cstdio>
namespace sbk::generator {
using namespace N64Recomp;
inline uint64_t function_id(const Context& ctx, const Function& f) {
    const auto& s=ctx.sections.at(f.section_index);
    if(s.rom_addr==UINT32_MAX || f.vram<s.ram_addr) throw std::runtime_error("Invalid section identity");
    return (uint64_t{s.rom_addr+1}<<32) | (f.vram-s.ram_addr);
}
inline uint64_t hle_id(const std::string& name) {
    uint64_t id=14695981039346656037ull;
    for(unsigned char ch:name) {id^=ch;id*=1099511628211ull;}
    return id;
}
struct ManifestEntry {
    std::string name;
    uint32_t section_rom, section_size, offset;
    size_t continuations, scratch;
};
inline std::map<uint64_t, ManifestEntry> manifest;
inline std::map<uint64_t, std::string> hle_manifest;
class Backend final : public Generator {
    std::ostream& out;
    CGenerator c;
    const Context& context;
    const Function& function;
    mutable std::vector<EmissionLocation> locations;
    mutable bool tail_call = false;
public:
    mutable std::map<uint64_t, std::string> resumes;
    mutable std::map<uint32_t, size_t> scratch;
    mutable std::map<uint64_t, std::string> hle;
    Backend(std::ostream& o,const Context& ctx,size_t index):out(o),c(o),context(ctx),function(ctx.functions.at(index)){}
    void begin_instruction(const EmissionLocation& loc) const override { locations.push_back(loc); }
    void prepare_call(bool tail) const override {tail_call=tail;}
    void end_instruction() const override { locations.pop_back(); }
    uint64_t identity(unsigned phase) const {
        if(locations.empty()) throw std::runtime_error("Missing typed instruction location");
        const auto& loc=locations.back();
        if(loc.delay_slot) throw std::runtime_error("Control transfer in delay slot is unsupported");
        uint32_t offset=loc.pc-function.vram;
        return (uint64_t{offset}<<32) | (uint64_t{loc.transfer_pc-function.vram}<<2) | phase;
    }
    void suspend(const std::string& kind,const std::string& target,uint64_t id) const {
        auto label="resume_"+std::to_string(id);
        if(!resumes.emplace(id,label).second) throw std::runtime_error("Duplicate continuation identity");
        out<<"frame.continuation="<<id<<"ull; return {sbk::continuation::ActionKind::"<<kind<<","<<target<<","<<((kind=="Call" || kind=="Lookup" || kind=="Hle") && tail_call ? "true":"false")<<"};\n"<<label<<":;\n";
    }
    void emit_function_start(const std::string&,size_t) const override {}
    void emit_function_end() const override {out<<"return {};\n";}
    void emit_return(const Context&,size_t) const override {out<<"return {};\n";}
    void emit_function_call(const Context& ctx,size_t index) const override {
        const auto& f=ctx.functions.at(index);
        if(f.reimplemented || f.name=="__osPfsSelectBank_recomp" ||
           f.name=="__osContRamRead_recomp" || f.name=="__osContRamWrite_recomp" ||
           f.name=="rmonPrintf_recomp") {emit_hle(f.name);return;}
        // External boundaries become explicit HLE actions, never native calls.
        // Runtime execution must reject them until their behavior is classified.
        if(f.ignored || f.words.empty()) {emit_hle(f.name);return;}
        suspend("Call",std::to_string(function_id(ctx,f))+"ull",identity(1));
    }
    void emit_hle(const std::string& name) const {
        const auto id=hle_id(name);
        auto [it,inserted]=hle.emplace(id,name);
        if(!inserted && it->second!=name) throw std::runtime_error("HLE identity collision");
        suspend("Hle",std::to_string(id)+"ull",identity(1));
    }
    void emit_function_call_lookup(uint32_t addr) const override {suspend("Lookup",std::to_string(addr)+"u",identity(1));}
    void latch_indirect_target(int reg) const override {out<<"frame.indirect_target=uint32_t("<<(reg?"ctx->r"+std::to_string(reg):"0")<<");\n";}
    void emit_function_call_by_register(int) const override {suspend("Lookup","frame.indirect_target",identity(1));}
    void emit_named_function_call(const std::string& name) const override {
        for(size_t i=0;i<context.functions.size();++i) if(context.functions[i].name==name) {emit_function_call(context,i);return;}
        // The pinned emitter creates static names from a known section and guest PC.
        unsigned section,pc; int consumed=0;
        if(sscanf(name.c_str(),"static_%u_%x%n",&section,&pc,&consumed)==2 && size_t(consumed)==name.size()) {
            Function f{};f.section_index=section;f.vram=pc;
            suspend("Call",std::to_string(function_id(context,f))+"ull",identity(1));return;
        }
        throw std::runtime_error("Unclassified named call: "+name);
    }
    void emit_function_call_reference_symbol(const Context&,uint16_t,size_t,uint32_t) const override {throw std::runtime_error("Reference symbol requires explicit classification");}
    void emit_goto(const std::string& target) const override {c.emit_goto(target);}
    void emit_label(const std::string& label) const override {
        c.emit_label(label);
        // L_ labels are guest branch destinations generated by the pinned emitter.
        // Other labels (after_/skip_) are local glue, never yield boundaries.
        if(label.starts_with("L_")) {
            uint32_t pc=std::stoul(label.substr(2),nullptr,16);
            uint64_t offset=pc-function.vram;
            suspend("Yield","0",(offset<<32)|(offset<<2)|3);
        }
    }
    void emit_jtbl_addend_declaration(const JumpTable& table,int reg) const override {
        auto [it,_]=scratch.emplace(table.jr_vram,scratch.size());
        out<<"frame.scratch.at("<<it->second<<")="<<(reg?"ctx->r"+std::to_string(reg):"0")<<";\n";
    }
    void emit_switch(const Context&,const JumpTable& table,int) const override {
        out<<"switch(frame.scratch.at("<<scratch.at(table.jr_vram)<<") >> 2) {\n";
    }
    void process_binary_op(const BinaryOp& op, const InstructionContext& ctx) const override { c.process_binary_op(op, ctx); }
    void process_unary_op(const UnaryOp& op, const InstructionContext& ctx) const override { c.process_unary_op(op, ctx); }
    void process_store_op(const StoreOp& op, const InstructionContext& ctx) const override { c.process_store_op(op, ctx); }
    void emit_branch_condition(const ConditionalBranchOp& op, const InstructionContext& ctx) const override { c.emit_branch_condition(op, ctx); }
    void emit_branch_close() const override { c.emit_branch_close(); }
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
    void emit_syscall(uint32_t) const override {throw std::runtime_error("Unclassified syscall");}
    void emit_do_break(uint32_t pc) const override {out<<"throw std::runtime_error(\"Guest BREAK at "<<pc<<"\");\n";}
    void emit_pause_self() const override {suspend("Pause","0",identity(2));out<<"return {sbk::continuation::ActionKind::Pause,0};\n";}
    void emit_trigger_event(uint32_t) const override {throw std::runtime_error("Unclassified event");}
    void emit_comment(const std::string& text) const override {c.emit_comment(text);}
};
inline bool emit(const Context& ctx,size_t index,std::ostream& out,std::span<std::vector<uint32_t>> statics,bool refs) {
    const auto& f=ctx.functions.at(index);
    if(ctx.trace_mode || !f.function_hooks.empty()) throw std::runtime_error("Native trace/hooks forbidden in continuation generation");
    std::ostringstream body;
    Backend gen(body,ctx,index);
    if(!recompile_function_custom(gen,ctx,index,body,statics,refs)) return false;
    const auto id=function_id(ctx,f);
    const auto& section=ctx.sections.at(f.section_index);
    if(!manifest.emplace(id,ManifestEntry{f.name,section.rom_addr,section.size,f.vram-section.ram_addr,gen.resumes.size(),gen.scratch.size()}).second)
        throw std::runtime_error("Duplicate stable function identity: "+f.name);
    for(const auto& [key,name]:gen.hle) {
        auto [it,added]=hle_manifest.emplace(key,name);
        if(!added && it->second!=name) throw std::runtime_error("Corpus HLE identity collision");
    }
    out<<"\n#include \"continuation/dispatch.hpp\"\n";
    out<<"static sbk::continuation::Action step_"<<f.name<<"(uint8_t* rdram,recomp_context* ctx,sbk::continuation::Frame& frame) {\n"
       <<"auto &hi=frame.hi,&lo=frame.lo,&result=frame.result; auto &c1cs=frame.c1cs;\n"
       <<"switch(frame.continuation) { case 0: goto entry;\n";
    for(const auto& [value,label]:gen.resumes) out<<"case "<<value<<"ull:goto "<<label<<";\n";
    out<<"default:throw std::runtime_error(\"Unknown continuation\");}\nentry:;\n"<<body.str()<<"}\n";
    out<<"extern \"C\" void "<<f.name<<"(uint8_t* r,recomp_context* c) {sbk::continuation::enter("<<id<<"ull,r,c); }\n"
       <<"static const bool registered_"<<f.name<<"=sbk::continuation::register_function({"<<id<<"ull,"<<f.vram<<"u,"<<gen.scratch.size()<<",step_"<<f.name<<","<<f.name<<",\""<<f.name<<"\"});\n";
    return true;
}
}
