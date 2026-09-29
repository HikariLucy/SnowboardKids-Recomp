// Synthetic, ROM-free overlay table for the engine probe module. It mixes a
// module-local function with engine runtime functions, like the generated
// table does, so the Windows import rebinding path is exercised.
#include "recomp.h"
#include "librecomp/sections.h"

extern "C" void sbk_probe_local_function(uint8_t* rdram, recomp_context* ctx);
extern "C" void osSendMesg_recomp(uint8_t* rdram, recomp_context* ctx);
extern "C" void osRecvMesg_recomp(uint8_t* rdram, recomp_context* ctx);

static FuncEntry section_probe_funcs[] = {
    { .func = sbk_probe_local_function, .offset = 0x00000000, .rom_size = 0x00000008 },
    { .func = osSendMesg_recomp, .offset = 0x00000008, .rom_size = 0x00000008 },
    { .func = osRecvMesg_recomp, .offset = 0x00000010, .rom_size = 0x00000008 },
};

static SectionTableEntry section_table[] = {
    { .rom_addr = 0x1000, .ram_addr = 0x80000400, .size = 0x18,
      .funcs = section_probe_funcs, .num_funcs = ARRLEN(section_probe_funcs),
      .relocs = NULL, .num_relocs = 0, .index = 0 },
};
const size_t num_sections = 1;
static int overlay_sections_by_index[] = {
    -1,
};
