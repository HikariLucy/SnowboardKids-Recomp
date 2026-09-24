#include <cstddef>

#include "librecomp/overlays.hpp"
#ifdef SBK_CONTINUATIONS
#include "recomp_overlays.inl"
#else
#include "../../RecompiledFuncs/recomp_overlays.inl"
#endif

namespace sbk {

template <typename T, std::size_t N>
constexpr std::size_t array_count(const T (&)[N]) {
    return N;
}

void register_overlays() {
    recomp::overlays::overlay_section_table_data_t sections{
        .code_sections = section_table,
        .num_code_sections = array_count(section_table),
        .total_num_sections = num_sections,
    };

    recomp::overlays::overlays_by_index_t overlays{
        .table = overlay_sections_by_index,
        .len = array_count(overlay_sections_by_index),
    };

    recomp::overlays::register_overlays(sections, overlays);
}

} // namespace sbk
