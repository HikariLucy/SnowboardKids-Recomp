# Snowboard Kids original save data map

Evidence: decomp include/game/save_data.h and
src/menu/main_menu/controller_main_menu_flow.c. The game writes the first
0x78e0 bytes of GameSaveData into one 0x7900-byte Controller Pak note.
This is a source-level map, not a live byte-by-byte decode.

| Offset | Size | Type | Semantic | Confidence and evidence |
| --- | ---: | --- | --- | --- |
| 0x0000 | 4 | s32 | Additive checksum over bytes 0x0004..0x78df | CONFIRMED: writeControllerPakSave |
| 0x0004 | 4 | s32 | Money | CONFIRMED: controller_pak_race_record_save_flow.c:49 |
| 0x0008 | 0x2c | bytes | Unknown high-score overlap | UNKNOWN |
| 0x0034 | 0x0b | bytes | Cup placements | STRONG: race_flow.c:1886, race_start_transition.c:221 |
| 0x003f | 0x0c | s8[12] | Course unlock/purchase states | CONFIRMED: course_select_menu.c:409, course_select_shop_ui.c:1302 |
| 0x004b | 1 | u8 | Character flags, exact bit meanings unknown | TENTATIVE: save_data.h |
| 0x004c | 1 | u8 | Progression level | CONFIRMED: race_start_transition.c:221-241 |
| 0x004d | 1 | u8 | Unknown | UNKNOWN |
| 0x004e | 0x00dc | record[11][5] | Time trial records | CONFIRMED: race_flow.c:1737,1954 |
| 0x012a | 0x002c | record[11] | Best laps | CONFIRMED: race_flow.c:1978 |
| 0x0156 | 0x00dc | record[11][5] | Race records | CONFIRMED: race_flow.c:1805,2087 |
| 0x0232 | 0x0024 | slot[9] | Replay length/offset | CONFIRMED: main_menu_scene_model.c:262-275 |
| 0x0256 | 0x7500 | u16[] | Compressed replay data | CONFIRMED: main_menu_scene_model.c:256-275 |
| 0x7756 | 0x006e | u16[11][5] | Trick attack scores | CONFIRMED: race_flow.c:2000-2019 |
| 0x77c4 | 0x0037 | u8[11][5] | Trick attack character IDs | STRONG: save_data.h |
| 0x77fb | 0x0037 | u8[11][5] | Time trial character IDs | STRONG: save_data.h |
| 0x7832 | 0x0037 | u8[11][5] | Score attack scores | CONFIRMED: race_flow.c:2043-2062 |
| 0x7869 | 0x0037 | u8[11][5] | Score attack character IDs | STRONG: save_data.h |
| 0x78a0 | 0x0037 | u8[11][5] | Race record character IDs | STRONG: save_data.h |
| 0x78d7 | 1 | u8 | Extra course unlock flags | CONFIRMED: race_flow.c:1903,2025; course_select_menu.c:114 |
| 0x78d8 | 8 written | bytes | Reserved/unknown | UNKNOWN |
| 0x78e0 | 0x20 allocated | bytes | Note tail outside observed PFS write | UNKNOWN |

The replay/ghost material is inside the same note, not a second PFS
file. The relation of all nine slots to named game modes still needs
audit. No settings field has been confirmed.

race_start_transition.c:221-241 changes progressionLevel 0→1 after
first place on courses 0..4 and 9, 1→2 at course 5, and 2→3 at
course 6; the last transition queues credits. The complete character
unlock bit map is still unknown. No progression logic was changed.
