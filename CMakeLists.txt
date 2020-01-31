set(COMPONENT_SRCS esp_lib/esp_lua_lib_sys.c 
                   esp_lib/esp_lua_lib_wifi.c
                   esp_lib/esp_lua_lib_web.c)

set(COMPONENT_ADD_INCLUDEDIRS include)

set(COMPONENT_REQUIRES esp_lua nvs_flash spiffs esp_http_client esp-tls mqtt)

register_component()