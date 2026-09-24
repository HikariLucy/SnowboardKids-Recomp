#include "continuation/dispatch.hpp"
#include <cstring>
#include <iostream>
void reference_flow_root(uint8_t*,recomp_context*);
void reference_switch_root(uint8_t*,recomp_context*);
extern "C" recomp_func_t* get_function(int32_t) {throw std::runtime_error("Unexpected native lookup");}
extern "C" void switch_error(const char*,uint32_t,uint32_t) {throw std::runtime_error("Bad jump table index");}
namespace sbk::continuation {
void enter(uint64_t,uint8_t*,recomp_context*) { throw std::runtime_error("Native thunk called during explicit dispatch"); }
}
static void check(bool ok,const char* why) {if(!ok) throw std::runtime_error(why);}
int main() try {
    using namespace sbk::continuation;
    for(unsigned fr:{0u,1u}) for(unsigned selector:{0u,1u}) {
        recomp_context baseline{},ctx{};
        baseline.r17=ctx.r17=selector;
        baseline.mips3_float_mode=ctx.mips3_float_mode=fr;
        baseline.f_odd=fr?&baseline.f1.u32l:&baseline.f0.u32h;
        ctx.f_odd=fr?&ctx.f1.u32l:&ctx.f0.u32h;
        std::vector<uint8_t> a(4096),b(4096);
        reference_flow_root(a.data(),&baseline);
        std::vector<Frame> frames{make_frame(uint64_t{0x1001}<<32)};
        unsigned calls=0,steps=0,yields=0;
        while(!frames.empty()) {
            check(++steps<100,"Dispatch did not terminate");
            auto action=step(b.data(),&ctx,frames.back());
            switch(action.kind) {
            case ActionKind::Return: frames.pop_back();break;
            case ActionKind::Call:
                check(frames.back().hi==10 && frames.back().lo==uint64_t(int64_t(-11)) && frames.back().c1cs==1,"Live generated locals lost");
                frames.push_back(make_frame(action.target));++calls;break;
            case ActionKind::Yield: ++yields;break;
            default:throw std::runtime_error("Unexpected action");
            }
            // No generated activation spans this destruction/reconstruction.
            auto moved=frames; std::vector<Frame>().swap(frames);frames=std::move(moved);
        }
        check(calls==3 && yields>0,"Call/backedge coverage missing");
        check(ctx.r10==3 && ctx.r11==3,"Delay slot executed wrong count");
        baseline.f_odd=ctx.f_odd=nullptr;
        check(std::memcmp(&baseline,&ctx,sizeof(ctx))==0,"Native CPU divergence");
        check(a==b,"Native memory divergence");
    }
    auto run=[&](uint32_t offset,recomp_context& ctx) {
        std::vector<uint8_t> memory(8192);
        std::vector<Frame> frames{make_frame((uint64_t{0x1001}<<32)|offset)};
        unsigned steps=0;
        while(!frames.empty()) {
            check(++steps<100,"Extra flow did not terminate");
            auto action=step(memory.data(),&ctx,frames.back());
            switch(action.kind) {
            case ActionKind::Return: frames.pop_back();break;
            case ActionKind::Call:
                if(action.tail) frames.back()=make_frame(action.target);
                else frames.push_back(make_frame(action.target));
                break;
            case ActionKind::Lookup:
                check(action.target==0x80700100,"Indirect target was not latched before its delay slot");
                frames.push_back(make_frame((uint64_t{0x1001}<<32)|0x100));break;
            case ActionKind::Yield:break;
            default:throw std::runtime_error("Unexpected extra-flow action");
            }
            auto copy=frames;std::vector<Frame>().swap(frames);frames=std::move(copy);
        }
    };
    recomp_context indirect{};indirect.f_odd=&indirect.f0.u32h;
    run(0x200,indirect);
    check(indirect.r21==1 && indirect.r22==1 && indirect.r20==1,"Indirect/tail side effects incorrect");
    for(unsigned selector:{0u,1u}) {
        recomp_context ctx{},baseline{};ctx.r4=baseline.r4=selector;
        std::vector<uint8_t> memory(8192);
        reference_switch_root(memory.data(),&baseline);
        run(0x400,ctx);
        check(ctx.r23==(selector?22:11),"Jump table selected wrong case");
        check(std::memcmp(&ctx,&baseline,sizeof(ctx))==0,"Jump table CPU divergence");
    }
    std::cout<<"PASS indirect target latch, direct tail, jump-table cases; production generator: direct calls, loops, branches, delay slots, live locals, FR=0/1\n";
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
