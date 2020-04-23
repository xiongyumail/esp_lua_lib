#include <stdio.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_lua_lib.h"

static const char *TAG = "esp_lib_ramf";

static int ramf_malloc(lua_State *L) 
{
    char addr[32] = {0};
    int size = luaL_checkinteger(L, 1);
    esp_lua_ramf_t *ramf = (esp_lua_ramf_t *)malloc(sizeof(esp_lua_ramf_t));
    ramf->data = (uint8_t *)malloc(size);
    ramf->size = 0;
    sprintf(addr, ""ESP_LUA_RAM_FILE_PATH"%d", (int)ramf);
    if (ramf != NULL) {
        lua_pushstring(L, addr);
    } else {
        lua_pushboolean(L, false);
    }

    return 1;
}

static int ramf_free(lua_State *L) 
{
    char *addr = luaL_checklstring(L, 1, NULL);
    if (strncmp(addr, ESP_LUA_RAM_FILE_PATH, strlen(ESP_LUA_RAM_FILE_PATH)) != 0) {
        lua_pushboolean(L, false);
        return 1;
    }

    esp_lua_ramf_t *ramf = (esp_lua_ramf_t *)atoi(&addr[strlen(ESP_LUA_RAM_FILE_PATH)]);
    free(ramf->data);
    free(ramf);
    lua_pushboolean(L, true);

    return 1;
}

static const luaL_Reg ramf_lib[] = {
    {"malloc", ramf_malloc},
    {"free", ramf_free},
    {NULL, NULL}
};

LUAMOD_API int esp_lib_ramf(lua_State *L) 
{
    luaL_newlib(L, ramf_lib);
    lua_pushstring(L, "0.1.0");
    lua_setfield(L, -2, "_version");
    return 1;
}