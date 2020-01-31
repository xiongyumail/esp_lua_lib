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

```lua
local json = require('json')
local dumpTable = require('dumpTable')

assert(wifi.init('CMCC-GPcG', 'etb3rzz7')) 
assert(sys.sntp('ntp1.aliyun.com'))

local base_url  = 'https://api.seniverse.com/v3/weather/now.json?key=lsawo7f7smtdljg9&language=zh-Hans&unit=c'

local mqtt_connected = false
web.mqtt('START', 'mqtt://mqtt.emake.run', 100)
local last_clock = os.time()
while (1) do
    event, data = web.mqtt('HANDLE', 10)
    if (event) then
        if (event == 'MQTT_EVENT_DATA') then
            printTable(json.decode(data))
        elseif (event == 'MQTT_EVENT_CONNECTED') then
            mqtt_connected = true
            web.mqtt('SUB', 'nc_temp', 0)
        elseif (event == 'MQTT_EVENT_DISCONNECTED') then
            mqtt_connected = false
        end
    end
    if (mqtt_connected and os.difftime (os.time(), last_clock) >= 30) then
        nc_temp = web.rest('GET', base_url..'&location=nanchang')
        web.mqtt('PUB', 'nc_temp', nc_temp, 0)
        last_clock = os.time()
        print('clock: '..os.clock()..', heap: '..sys.heap())
    end
end
```

```bash
./mkspiffs -c lua -b 4096 -p 256 -s 0x1f0000 lua.bin
esptool.py -p /dev/ttyUSB0 -b 921600 write_flash 0x210000 lua.bin
```