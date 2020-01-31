#include <stdio.h>
#include <stdlib.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <time.h>
#include <sys/time.h>
#include "nvs_flash.h"
#include "esp_spiffs.h"
#include "esp_log.h"
#include "esp_lua_lib.h"
#include "esp_sntp.h"

static const char *TAG = "esp_lua_lib_sys";

static int sys_init(lua_State *L) 
{
    static int state = 0;
    if (state != 0) {
        lua_pushboolean(L, true);
        return 1;
    }
    esp_log_level_set("*", ESP_LOG_NONE);
    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "Initializing SPIFFS");
    
    esp_vfs_spiffs_conf_t conf = {
      .base_path = "/lua",
      .partition_label = NULL,
      .max_files = 5,
      .format_if_mount_failed = true
    };
    
    // Use settings defined above to initialize and mount SPIFFS filesystem.
    // Note: esp_vfs_spiffs_register is an all-in-one convenience function.
    ret = esp_vfs_spiffs_register(&conf);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find SPIFFS partition");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
    }
    
    size_t total = 0, used = 0;
    ret = esp_spiffs_info(NULL, &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
    }

    linenoiseHistoryLoad(ESP_LUA_HISTORY_PATH);

    lua_pushboolean(L, true);
    return 1;
}

static int sys_delay(lua_State *L) 
{
    uint32_t delay = (uint32_t)luaL_checknumber(L,1);
    vTaskDelay(delay / portTICK_RATE_MS);
    lua_pushboolean(L, true);
    return 1;
}

static void time_sync_notification_cb(struct timeval *tv)
{
    ESP_LOGI(TAG, "Notification of a time synchronization event");
}

static void initialize_sntp(char *url)
{
    ESP_LOGI(TAG, "Initializing SNTP");
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, url);
    sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    sntp_set_sync_mode(SNTP_SYNC_MODE_SMOOTH);
    sntp_init();
}

// sys.sntp(url [, retry])
static int sys_sntp(lua_State *L) 
{
    static int state = 0;
    size_t len;

    if (state != 0) {
        sntp_stop();
    }
    if (lua_gettop(L) >= 1) {
        initialize_sntp(luaL_checklstring(L, 1, &len));
    } else {
        initialize_sntp("pool.ntp.org");
    }

    // wait for time to be set
    time_t now = 0;
    struct tm timeinfo = { 0 };
    int retry = 0;
    int retry_count = 10;

    if (lua_tointeger(L, 2) != 0) {
        retry_count = lua_tointeger(L, 2);
    }
    while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry < retry_count) {
        ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }
    time(&now);
    localtime_r(&now, &timeinfo);
    // Set timezone to China Standard Time
    setenv("TZ", "CST-8", 1);
    tzset();
    state = 1;
    // Is time set? If not, tm_year will be (1970 - 1900).
    if (timeinfo.tm_year < (2020 - 1900)) {
        lua_pushboolean(L, false);
    } else {
        lua_pushboolean(L, true);
    }
    return 1;
}

static int sys_heap(lua_State *L) 
{
    lua_pushinteger(L, esp_get_free_heap_size());
    return 1;
}

static int sys_test(lua_State *L) 
{
    char *func = lua_tostring(L, 1);
    lua_getglobal(L, func);
	lua_pushinteger(L, 10);
    lua_pushinteger(L, 21);
    lua_pcall(L, 2, 0, 0);
    // lua_pushinteger(L, esp_get_free_heap_size());
    return 0;
}

static const luaL_Reg syslib[] = {
    {"init", sys_init},
    {"delay", sys_delay},
    {"sntp", sys_sntp},
    {"heap", sys_heap},
    {"test", sys_test},
    {NULL, NULL}
};

LUAMOD_API int esp_lua_lib_sys(lua_State *L) 
{
    luaL_newlib(L, syslib);
    return 1;
}