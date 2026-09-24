#include "dispatch.hpp"
#include <map>
namespace sbk::continuation {
namespace {
auto& functions() { static std::map<uint64_t, Descriptor> table; return table; }
auto& tokens() { static std::map<recomp_func_t*, uint64_t> table; return table; }
}
bool register_function(Descriptor d) {
    if (!d.id || !d.step || !d.token || functions().contains(d.id) || tokens().contains(d.token))
        throw std::logic_error("Duplicate or invalid continuation function descriptor");
    functions().emplace(d.id, d);
    tokens().emplace(d.token, d.id);
    return true;
}
const Descriptor& descriptor(uint64_t id) {
    auto it=functions().find(id);
    if(it==functions().end()) throw std::runtime_error("Unknown continuation function identity");
    return it->second;
}
const Descriptor& descriptor_for_token(recomp_func_t* token) {
    auto it=tokens().find(token);
    if(it==tokens().end()) throw std::runtime_error("Unclassified indirect continuation target");
    return descriptor(it->second);
}
Frame make_frame(uint64_t id) {
    Frame result;
    result.function=id;
    result.scratch.resize(descriptor(id).scratch_count);
    return result;
}
Action step(uint8_t* rdram, recomp_context* context, Frame& frame) {
    const auto& d=descriptor(frame.function);
    if(frame.scratch.size()!=d.scratch_count) throw std::runtime_error("Invalid continuation scratch shape");
    return d.step(rdram,context,frame);
}
}
