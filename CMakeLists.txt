set(COMPONENT_SRCS esp_lua_lib_sys.c 
                   esp_lua_lib_wifi.c)

set(COMPONENT_ADD_INCLUDEDIRS include)

set(COMPONENT_REQUIRES esp_lua nvs_flash spiffs)

register_component()