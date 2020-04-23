set(COMPONENT_SRCS esp/esp_lib_sys.c 
                   esp/esp_lib_net.c
                   esp/esp_lib_web.c
                   esp/esp_lib_mqtt.c
                   esp/esp_lib_httpd.c
                   esp/esp_lib_ramf.c)

set(COMPONENT_ADD_INCLUDEDIRS include)

set(COMPONENT_REQUIRES esp_lua nvs_flash spiffs esp_http_client esp-tls mqtt app_update esp_https_ota esp_http_server mdns)

register_component()