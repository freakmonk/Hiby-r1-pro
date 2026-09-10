#ifndef GB_MENU_H
#define GB_MENU_H

#include "types.h"
#include "platform.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GB_MENU_MAX_ROMS 128
#define GB_MENU_PATH_MAX 512
#define GB_MENU_NAME_MAX 128

typedef enum {
    GB_MENU_QUIT = 0,
    GB_MENU_PLAYER,
    GB_MENU_ROM,
    GB_MENU_SHUTDOWN,
    GB_MENU_FIRMWARE_UPDATE,
    GB_MENU_FACTORY_RESET
} gb_menu_action_t;

typedef struct {
    gb_menu_action_t action;
    char rom_path[GB_MENU_PATH_MAX];
} gb_menu_result_t;

typedef struct {
    char name[GB_MENU_NAME_MAX];
    char path[GB_MENU_PATH_MAX];
} gb_menu_rom_t;

int gb_menu_scan_roms(gb_menu_rom_t *roms, int max_roms);
gb_menu_result_t gb_menu_run(gb_platform_t *platform, int *start_index);

#ifdef __cplusplus
}
#endif

#endif
