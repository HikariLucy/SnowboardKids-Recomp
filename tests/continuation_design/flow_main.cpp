#include "runtime.hpp"
#include <cstring>
#include <iostream>
void flow_root(uint8_t*,recomp_context*);
p1::Action flow_dispatch(uint8_t*,recomp_context*,p1::Frame&);
static void check(bool b,const char*m){if(!b)throw std::runtime_error(m);}
int main() try {
    for(uint32_t selector: {0u,1u}) {
        recomp_context native{}, resumed{};
        native.r17=selector; resumed.r17=selector;
        native.f_odd=&native.f0.u32h; resumed.f_odd=&resumed.f0.u32h;
        std::vector<uint8_t> a(4096),b(4096);
        flow_root(a.data(),&native);
        std::vector<p1::Frame> frames{{p1::root_id}};
        unsigned calls=0, steps=0;
        while(!frames.empty()) {
            check(++steps<20,"loop did not terminate");
            auto action=flow_dispatch(b.data(),&resumed,frames.back());
            if(action.callee) {
                check(frames.size()==1,"unexpected call depth");
                check(frames.back().hi==10 && frames.back().lo==uint64_t(int64_t(-11)) && frames.back().c1cs==1,"live locals not retained");
                ++calls; frames.push_back({action.callee});
            } else frames.pop_back();
            // Destroy the old frame container at every boundary, including each
            // returned call action. No generated C activation spans this move.
            auto reconstructed=frames;
            std::vector<p1::Frame>().swap(frames);
            frames=std::move(reconstructed);
        }
        check(calls==3 && steps==7,"repeated call/resume counts");
        check(resumed.r10==3 && resumed.r11==3,"call or loop delay slot repeated/skipped");
        check(resumed.r12==1 && resumed.r13==selector,"ordinary branch slot/path");
        check(resumed.r14==(selector==0?1:0) && resumed.r15==selector,"likely branch annulment/path");
        check(resumed.r18==10 && resumed.r19==uint64_t(int64_t(-11)) && resumed.r20==1,"HI/LO/FP condition changed across calls");
        check(resumed.r21==3,"callee execution count");
        // Compare all context bytes after normalizing the sole host pointer.
        native.f_odd=nullptr; resumed.f_odd=nullptr;
        check(std::memcmp(&native,&resumed,sizeof(native))==0,"native/continuation context mismatch");
        check(a==b,"native/continuation memory mismatch");
    }
    std::cout<<"PASS generated direct-call loop, ordinary/likely branch slots, live HI/LO/c1cs; two paths\n";
} catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
