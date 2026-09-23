#ifndef SBK_UI_FUNCS_H
#define SBK_UI_FUNCS_H

#include <stdint.h>

#include "recomp.h"
#include "recompui_event_structs.h"

#ifdef __cplusplus
extern "C" {
#endif

void recomp_run_ui_callbacks(uint8_t* rdram, recomp_context* ctx);

#ifdef __cplusplus
}
#endif

#endif
