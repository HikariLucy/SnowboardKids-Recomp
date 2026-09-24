#pragma once

#include <cstdint>
#include <vector>

namespace sbk::quiescence {

#pragma pack(push, 1)

struct SemanticTile {
    uint8_t fmt;
    uint8_t siz;
    uint16_t line;
    uint16_t tmem;
    uint8_t palette;
    uint8_t cmt;
    uint8_t cms;
    uint8_t maskt;
    uint8_t masks;
    uint8_t shiftt;
    uint8_t shifts;
    uint16_t uls;
    uint16_t ult;
    uint16_t lrs;
    uint16_t lrt;
    uint64_t replacement_hash;
};

struct SemanticRdpState {
    uint8_t tmem[4096];
    SemanticTile tiles[8];
    // Texture image state
    uint8_t texture_fmt;
    uint8_t texture_siz;
    uint16_t texture_width;
    uint32_t texture_address;
    // Color image state
    uint32_t color_image_address;
    uint8_t color_image_fmt;
    uint8_t color_image_siz;
    uint16_t color_image_width;
    // Depth image state
    uint32_t depth_image_address;
    // Other mode
    uint32_t other_mode_h;
    uint32_t other_mode_l;
    // Color combiner
    uint64_t combiner_cycle1;
    uint64_t combiner_cycle2;
    // Colors
    float env_color[4];
    float prim_color[4];
    float prim_lod[2];
    float prim_depth[2];
    float blend_color[4];
    float fog_color[4];
    uint32_t fill_color;
    // Scissor
    int32_t scissor_ulx;
    int32_t scissor_uly;
    int32_t scissor_lrx;
    int32_t scissor_lry;
    uint8_t scissor_mode;
    // Convert & Key
    int32_t convert_k[6];
    float key_center[3];
    float key_scale[3];
};

struct SemanticViState {
    uint32_t status;
    uint32_t origin;
    uint32_t width;
    uint32_t v_intr;
    uint32_t v_current;
    uint32_t burst;
    uint32_t v_sync;
    uint32_t h_sync;
    uint32_t leap;
    uint32_t h_sync_leap;
    uint32_t h_video;
    uint32_t v_video;
    uint32_t v_burst;
    uint32_t x_scale;
    uint32_t y_scale;
};

struct SemanticFramebufferHeader {
    uint32_t address;
    uint32_t width;
    uint32_t height;
    uint8_t siz;
    uint8_t fmt;
    uint8_t type; // 1 = Color, 2 = Depth
    uint32_t pixel_bytes;
};

struct SemanticStateHeader {
    uint32_t magic;      // 0x53424B33 ('S','B','K','3')
    uint32_t version;    // 1
    uint64_t generation;
    SemanticRdpState rdp;
    SemanticViState vi;
    uint32_t framebuffer_count;
};

#pragma pack(pop)

constexpr uint32_t SEMANTIC_STATE_MAGIC = 0x53424B33;
constexpr uint32_t SEMANTIC_STATE_VERSION = 1;

} // namespace sbk::quiescence
