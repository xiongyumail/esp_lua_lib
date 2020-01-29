#pragma once

#include "esp_lua.h"

#ifdef __cplusplus
extern "C" {
#endif

int esp_lua_lib_sys(lua_State *L);

int esp_lua_lib_wifi(lua_State *L);

#ifdef __cplusplus
}
#endif