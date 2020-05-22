#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "esp_lua_lib.h"
/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group = NULL;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static const char *TAG = "esp_lib_net";

static int s_retry_num = 0;
static wifi_ap_config_t wifi_ap_config = {0};
static wifi_sta_config_t wifi_sta_config = {0};

static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "retry to connect to the AP");
        if (s_retry_num < 0) {
           esp_wifi_connect(); 
        } else if (s_retry_num > 0) {
            esp_wifi_connect();
            s_retry_num--;
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG,"connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:%s",
                 ip4addr_ntoa(&event->ip_info.ip));
        s_retry_num = -1;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    } else  if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" join, AID=%d",
                 MAC2STR(event->mac), event->aid);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" leave, AID=%d",
                 MAC2STR(event->mac), event->aid);
    }
}

static void wifi_deinit() 
{
    s_retry_num = 0;
    esp_wifi_stop();
    esp_wifi_deinit();
    if (s_wifi_event_group) {
        vEventGroupDelete(s_wifi_event_group);
    } 
    s_wifi_event_group = NULL;
}

static int wifi_init(wifi_mode_t mode)
{
    int ret = -1;
    s_wifi_event_group = xEventGroupCreate();

    tcpip_adapter_init();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
 
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

    wifi_config_t wifi_config = {0};

    ESP_ERROR_CHECK(esp_wifi_set_mode(mode));
    if (mode == WIFI_MODE_AP) {
        memset(&wifi_config, 0, sizeof(wifi_config_t));
        memcpy(&wifi_config.ap, &wifi_ap_config, sizeof(wifi_ap_config_t));
        ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config));
    } else if (mode == WIFI_MODE_STA) {
        memset(&wifi_config, 0, sizeof(wifi_config_t));
        memcpy(&wifi_config.sta, &wifi_sta_config, sizeof(wifi_sta_config_t));
        ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    } else if (mode == WIFI_MODE_APSTA) {
        memset(&wifi_config, 0, sizeof(wifi_config_t));
        memcpy(&wifi_config.ap, &wifi_ap_config, sizeof(wifi_ap_config_t));
        ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config));
        memset(&wifi_config, 0, sizeof(wifi_config_t));
        memcpy(&wifi_config.sta, &wifi_sta_config, sizeof(wifi_sta_config_t));
        ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    }
    ESP_ERROR_CHECK(esp_wifi_start());

    if (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA) {
        tcpip_adapter_ip_info_t ip_info = {        
            .ip.addr      = ipaddr_addr("192.168.1.1"),        
            .netmask.addr = ipaddr_addr("255.255.255.0"),        
            .gw.addr      = ipaddr_addr("192.168.1.1"),    
        };    
        ESP_ERROR_CHECK(tcpip_adapter_dhcps_stop(TCPIP_ADAPTER_IF_AP));    
        ESP_ERROR_CHECK(tcpip_adapter_set_ip_info(TCPIP_ADAPTER_IF_AP, &ip_info));    
        ESP_ERROR_CHECK(tcpip_adapter_dhcps_start(TCPIP_ADAPTER_IF_AP));
    }

    ESP_LOGI(TAG, "wifi_init finished.");

    if (mode == WIFI_MODE_STA || mode == WIFI_MODE_APSTA) {
        /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
        * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
        EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                pdFALSE,
                pdFALSE,
                portMAX_DELAY);

        /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
        * happened. */
        if (bits & WIFI_CONNECTED_BIT) {
            ret = 0;
        } else if (bits & WIFI_FAIL_BIT) {
        } else {
            ESP_LOGE(TAG, "UNEXPECTED EVENT");
        }

    } else if (mode == WIFI_MODE_AP) {
        ret = 0;
    }
    return ret;
}

static char *get_ip(tcpip_adapter_if_t adapter_if)
{
    tcpip_adapter_ip_info_t ip_info;

    tcpip_adapter_get_ip_info(adapter_if, &ip_info);  

    return ip4addr_ntoa(&ip_info.ip);
}

static int net_ap(lua_State *L) 
{
    char *ssid = luaL_checklstring(L, 1, NULL);
    char *passwd = luaL_checklstring(L, 2, NULL);

    memcpy(wifi_ap_config.ssid, ssid, strlen(ssid)+1);
    memcpy(wifi_ap_config.password, passwd, strlen(passwd)+1);
    wifi_ap_config.max_connection = 4;
    wifi_ap_config.authmode = WIFI_AUTH_WPA_WPA2_PSK;
    if (strlen(passwd) == 0) {
        wifi_ap_config.authmode = WIFI_AUTH_OPEN;
    }

    lua_pushboolean(L, true);
    return 1;
}

static int net_sta(lua_State *L) 
{
    char *ssid = luaL_checklstring(L, 1, NULL);
    char *passwd = luaL_checklstring(L, 2, NULL);

    memcpy(wifi_sta_config.ssid, ssid, strlen(ssid)+1);
    memcpy(wifi_sta_config.password, passwd, strlen(passwd)+1);

    lua_pushboolean(L, true);
    return 1;
}

static int net_start(lua_State *L) 
{
    static int state = 0;
    char *method = luaL_checklstring(L, 1, NULL);

    if (lua_gettop(L) >= 2) {
        s_retry_num = (uint32_t)luaL_checkinteger(L, 2);
    } else {
        s_retry_num = 10;
    }
    
    if (state != 0) {
        wifi_deinit();
    }
    if (strcmp(method, "STA") == 0) {
        if (wifi_init(WIFI_MODE_STA) == 0) {
            lua_pushstring(L, get_ip(TCPIP_ADAPTER_IF_STA));
            state = 1;
            return 1;
        }
    } else if (strcmp(method, "AP") == 0) {
        if (wifi_init(WIFI_MODE_AP) == 0) {
            lua_pushstring(L, get_ip(TCPIP_ADAPTER_IF_AP));
            state = 1;
            return 1;
        }
    } else if (strcmp(method, "APSTA") == 0) {
        if (wifi_init(WIFI_MODE_APSTA) == 0) {
            lua_pushstring(L, get_ip(TCPIP_ADAPTER_IF_STA));
            state = 1;
            return 1;
        }
    }

    wifi_deinit();
    lua_pushboolean(L, false);
    return 1;
}

static int net_info(lua_State *L) 
{
    lua_newtable(L);

    lua_pushstring(L, "ip");
    lua_newtable(L);
    lua_pushstring(L, "sta");
    lua_pushstring(L, get_ip(TCPIP_ADAPTER_IF_STA));
    lua_settable(L,-3);
    lua_pushstring(L, "ap");
    lua_pushstring(L, get_ip(TCPIP_ADAPTER_IF_AP));
    lua_settable(L,-3);
    lua_settable(L,-3);

    char str[128] = "";
    uint8_t mac[8] = {0};

    lua_pushstring(L, "mac");
    lua_newtable(L);
    lua_pushstring(L, "sta");
    esp_wifi_get_mac(ESP_IF_WIFI_STA, mac);
    sprintf(str, ""MACSTR"", MAC2STR(mac));
    lua_pushstring(L, str);
    lua_settable(L,-3);
    lua_pushstring(L, "ap");
    esp_wifi_get_mac(ESP_IF_WIFI_AP, mac);
    sprintf(str, ""MACSTR"", MAC2STR(mac));
    lua_pushstring(L, str);
    lua_settable(L,-3);
    lua_settable(L,-3);
    return 1;
}

static const luaL_Reg net_lib[] = {
    {"ap",   net_ap},
    {"sta",   net_sta},
    {"start",   net_start},
    {"info",   net_info},
    {NULL, NULL}
};

LUAMOD_API int esp_lib_net(lua_State *L) 
{
    luaL_newlib(L, net_lib);
    lua_pushstring(L, "0.1.0");
    lua_setfield(L, -2, "_version");
    return 1;
}