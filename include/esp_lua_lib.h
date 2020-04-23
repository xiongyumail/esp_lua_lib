#pragma once

#include "esp_lua.h"

#ifdef __cplusplus
extern "C" {
#endif

#if CONFIG_SPIRAM
#define ESP_LUA_MAX_STR_SIZE    (200*1024) // 200 KB
#else
#define ESP_LUA_MAX_STR_SIZE    (20*1024) // 20 KB
#endif
#define ESP_LUA_MAX_FILE_SIZE   (200*1024) // 200 KB

#define ESP_LUA_RAM_FILE_PATH "/lua/ramf/"

typedef struct {
    uint8_t *data;
    size_t size;
} esp_lua_ramf_t;

LUAMOD_API int esp_lib_sys(lua_State *L);

LUAMOD_API int esp_lib_net(lua_State *L);

LUAMOD_API int esp_lib_web(lua_State *L);

LUAMOD_API int esp_lib_mqtt(lua_State *L);

LUAMOD_API int esp_lib_httpd(lua_State *L);

LUAMOD_API int esp_lib_ramf(lua_State *L);

#ifdef __cplusplus
}
#endif