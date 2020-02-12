#pragma once

#include "esp_lua.h"

#ifdef __cplusplus
extern "C" {
#endif

LUAMOD_API int esp_lib_sys(lua_State *L);

LUAMOD_API int esp_lib_net(lua_State *L);

LUAMOD_API int esp_lib_web(lua_State *L);

LUAMOD_API int esp_lib_mqtt(lua_State *L);

LUAMOD_API int esp_lib_httpd(lua_State *L);

#ifdef __cplusplus
}
#endif