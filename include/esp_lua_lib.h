#pragma once

#include "esp_lua.h"

#ifdef __cplusplus
extern "C" {
#endif

LUAMOD_API int esp_lua_lib_sys(lua_State *L);

LUAMOD_API int esp_lua_lib_wifi(lua_State *L);

LUAMOD_API int esp_lua_lib_web(lua_State *L);

#ifdef __cplusplus
}
#endif