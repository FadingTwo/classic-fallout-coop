#ifndef FALLOUT_COOP_COMPAT_H_
#define FALLOUT_COOP_COMPAT_H_

// CE (co-op): the co-op modules shared with Classic Fallout Co-op for
// Fallout 1 call the engine by Fallout 1 CE's names; this maps them to
// Fallout 2 CE's. Only co-op files include it.

#include "autorun.h"
#include "color.h"
#include "config.h"
#include "critter.h"
#include "cycle.h"
#include "dbox.h"
#include "debug.h"
#include "display_monitor.h"
#include "game.h"
#include "game_config.h"
#include "game_mouse.h"
#include "game_sound.h"
#include "input.h"
#include "interface.h"
#include "map.h"
#include "mouse.h"
#include "object.h"
#include "palette.h"
#include "proto_instance.h"
#include "scripts.h"
#include "text_font.h"
#include "tile.h"
#include "window_manager.h"

namespace fallout {

#define debug_printf debugPrint
#define game_config gGameConfig
#define colorTable _colorTable
#define map_elevation gElevation
#define tile_center_tile gCenterTile
#define obj_dude gDude
#define cmap _cmap

inline bool config_get_string(Config* config, const char* section, const char* key, char** value)
{
    return configGetString(config, section, key, value);
}

inline bool config_get_value(Config* config, const char* section, const char* key, int* value)
{
    return configGetInt(config, section, key, value);
}

inline bool config_set_value(Config* config, const char* section, const char* key, int value)
{
    return configSetInt(config, section, key, value);
}

inline bool config_set_string(Config* config, const char* section, const char* key, const char* value)
{
    return configSetString(config, section, key, value);
}

inline void display_print(char* text) { displayMonitorAddMessage(text); }
inline bool gconfig_save() { return gameConfigSave(); }
inline bool config_init(Config* config) { return configInit(config); }
inline void config_exit(Config* config) { configFree(config); }
inline bool config_load(Config* config, const char* path, bool isDb) { return configRead(config, path, isDb); }
inline bool config_save(Config* config, const char* path, bool isDb) { return configWrite(config, path, isDb); }
inline void autorun_mutex_destroy() { autorunMutexClose(); }
inline bool loadColorTable(const char* path) { return colorPaletteLoad(path); }
inline void palette_fade_to(unsigned char* palette) { paletteFadeTo(palette); }
inline int dialog_out(const char* title, const char** body, int bodyLength, int x, int y, int titleColor, const char* a8, int bodyColor, int flags)
{
    return showDialogBox(title, body, bodyLength, x, y, titleColor, a8, bodyColor, flags);
}
inline unsigned int get_time() { return getTicks(); }
inline unsigned int elapsed_time(unsigned int start) { return getTicksSince(start); }
inline int get_input() { return inputGetInput(); }
inline int game_handle_input(int eventCode, bool isInCombatMode) { return gameHandleKey(eventCode, isInCombatMode); }

inline void cycle_disable() { colorCycleDisable(); }
inline void cycle_enable() { colorCycleEnable(); }
inline bool cycle_is_enabled() { return colorCycleEnabled(); }

inline int gmouse_get_cursor() { return gameMouseGetCursor(); }
inline int gmouse_set_cursor(int cursor) { return gameMouseSetCursor(cursor); }
inline int gmouse_3d_get_mode() { return gameMouseGetMode(); }
inline void gmouse_3d_set_mode(int mode) { gameMouseSetMode(mode); }
inline void gmouse_bk_process() { gameMouseRefresh(); }

inline int gsound_background_play(const char* fileName, int a2, int a3, int a4) { return backgroundSoundLoad(fileName, a2, a3, a4); }
inline void gsound_background_stop() { backgroundSoundDelete(); }
inline int gsound_play_sfx_file_volume(const char* name, int volume) { return _gsound_play_sfx_file_volume(name, volume); }
inline int gsound_speech_play(const char* fileName, int a2, int a3, int a4)
{
    if (speechLoad(fileName, a2, a3, a4) == -1) {
        return -1;
    }
    return _gsound_speech_play_preloaded();
}
inline void gsound_speech_stop() { speechDelete(); }

inline void intface_redraw() { interfaceBarRefresh(); }
inline void intface_update_items(bool animated) { interfaceUpdateItems(animated, INTERFACE_ITEM_ACTION_DEFAULT, INTERFACE_ITEM_ACTION_DEFAULT); }

inline int mouse_get_buttons() { return mouseGetEvent(); }
inline void mouse_get_position(int* x, int* y) { mouseGetPosition(x, y); }
inline void mouse_hide() { mouseHideCursor(); }
inline void mouse_show() { mouseShowCursor(); }
inline int mouse_set_shape(unsigned char* buf, int width, int length, int full, int hotx, int hoty, char trans)
{
    return mouseSetFrame(buf, width, length, full, hotx, hoty, trans);
}

inline int obj_attempt_placement(Object* obj, int tile, int elevation, int radius) { return _obj_attempt_placement(obj, tile, elevation, radius); }
inline int obj_turn_off(Object* obj, Rect* rect) { return objectHide(obj, rect); }
inline int obj_turn_on(Object* obj, Rect* rect) { return objectShow(obj, rect); }
inline char* critter_name(Object* obj) { return critterGetName(obj); }
inline bool critter_is_dead(Object* obj) { return critterIsDead(obj); }
inline int critter_get_hits(Object* obj) { return critterGetHitPoints(obj); }
inline int critter_adjust_hits(Object* obj, int hp) { return critterAdjustHitPoints(obj, hp); }

inline void setSystemPalette(unsigned char* palette) { _setSystemPalette(palette); }
inline void dump_screen() { takeScreenshot(); }
inline int scripts_check_state() { return scriptsHandleRequests(); }
inline int map_check_state() { return mapHandleTransition(); }
inline int tile_dist(int tile1, int tile2) { return tileDistanceBetween(tile1, tile2); }

inline int text_curr() { return fontGetCurrent(); }
inline void text_font(int font) { fontSetCurrent(font); }
#define text_to_buf fontDrawText

inline int tile_coord(int tile, int* x, int* y, int elevation) { return tileToScreenXY(tile, x, y, elevation); }
inline int tile_num(int x, int y, int elevation, bool ignoreBounds = false) { return tileFromScreenXY(x, y, elevation, ignoreBounds); }
inline void tile_refresh_display() { tileWindowRefresh(); }
inline void tile_refresh_rect(Rect* rect, int elevation) { tileWindowRefreshRect(rect, elevation); }
inline int tile_set_center(int tile, int flags) { return tileSetCenter(tile, flags); }

inline int win_add(int x, int y, int width, int height, int color, int flags) { return windowCreate(x, y, width, height, color, flags); }
inline void win_delete(int win) { windowDestroy(win); }
inline void win_draw(int win) { windowRefresh(win); }
inline unsigned char* win_get_buf(int win) { return windowGetBuffer(win); }

} // namespace fallout

#endif /* FALLOUT_COOP_COMPAT_H_ */
