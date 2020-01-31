## _ESP_LUA_LIB_

## How to use

```bash
cd [project path]/components
git submodule add https://github.com/xiongyumail/esp_lua
git submodule add https://github.com/xiongyumail/esp_lua_lib
```

* partitions.csv

```csv
# Name,   Type, SubType, Offset,  Size, Flags
# Note: if you change the phy_init or app partition offset, make sure to change the offset in Kconfig.projbuild
nvs,      data, nvs,     0x9000,  0x4000,
otadata,  data, ota,     0xd000,  0x2000,
phy_init, data, phy,     0xf000,  0x1000,
ota_0,    app,  ota_0,   0x10000, 0x100000,
ota_1,    app,  ota_1,   0x110000,0x100000,
storage,  data, spiffs,  0x210000,0x1f0000, 
```

* main.c

```c
#include "esp_lua.h"
#include "esp_lua_lib.h"

static const char *TAG = "main";

static const luaL_Reg mylibs[] = {
    {"sys", esp_lua_lib_sys},
    {"wifi", esp_lua_lib_wifi},
    {"web", esp_lua_lib_web},
    {NULL, NULL}
};

const char LUA_SCRIPT_INIT[] = " \
assert(sys.init()) \
dofile(\'/lua/init.lua\') \
";

void lua_task(void *arg)
{
    char *ESP_LUA_ARGV[5] = {"./lua", "-i", "-e", LUA_SCRIPT_INIT, NULL}; // enter interactive mode after executing 'script'

    esp_lua_init(NULL, NULL, mylibs);

    while (1) {
        esp_lua_main(4, ESP_LUA_ARGV);
        printf("lua exit\n");
        vTaskDelay(1000 / portTICK_RATE_MS);
    }

    vTaskDelete(NULL);
}

void app_main()
{  
    xTaskCreate(lua_task, "lua_task", 10240, NULL, 5, NULL);
}
```

* Generate spiffs bin and flash

```bash
./components/esp_lua_lib/lua_flash.sh
```