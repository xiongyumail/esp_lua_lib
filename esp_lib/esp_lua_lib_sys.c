#include <stdio.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <time.h>
#include <sys/time.h>
#include "nvs_flash.h"
#include "esp_spiffs.h"
#include "esp_log.h"
#include "esp_lua_lib.h"
#include "esp_sntp.h"
#include "esp_task_wdt.h"

static const char *TAG = "esp_lua_lib_sys";

static int sys_init(lua_State *L) 
{
    static int state = 0;
    if (state != 0) {
        lua_pushboolean(L, true);
        return 1;
    }
    esp_log_level_set("*", ESP_LOG_ERROR);
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

    esp_task_wdt_init(5, true);

    linenoiseHistoryLoad(ESP_LUA_HISTORY_PATH);

    state = 1;

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

    if (state != 0) {
        sntp_stop();
    }
    if (lua_gettop(L) >= 1) {
        initialize_sntp(luaL_checklstring(L, 1, NULL));
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

static int sys_restart(lua_State *L) 
{
    linenoiseHistorySave(ESP_LUA_HISTORY_PATH);
    esp_restart();
    return 0;
}

#include "esp32/rom/md5_hash.h"

static int md5_sum(const char *file, char *md5sum)
{
    unsigned char digest[17];
    int i;
    struct MD5Context md5_ctx;

    FILE* f = fopen(file, "r");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for writing");
        return -1;
    }
    char *buf = calloc(sizeof(char), BUFSIZ);
    int read_len = 0;
    MD5Init(&md5_ctx);
    while (!feof(f)) {
        read_len = fread(buf, sizeof(char), BUFSIZ, f);
        MD5Update(&md5_ctx, (const unsigned char *)buf, read_len);
    }
    fclose(f);
    free(buf);
    MD5Final(digest, &md5_ctx);

    for (i = 0; i < 16; i++) {
        sprintf(&(md5sum[i * 2]), "%02x", (unsigned int)digest[i]);
    }
    return 0;
}

static int sys_md5sum(lua_State *L) 
{
    char *file = luaL_checklstring(L, 1, NULL);
    char *md5sum = calloc(sizeof(char), 64);
    if (md5_sum(file, md5sum) == 0) {
        lua_pushstring(L, md5sum);
    } else {
        lua_pushboolean(L, false);
    }
    free(md5sum);
    return 1;
}

#include "esp_ota_ops.h"

static int sys_version(lua_State *L) 
{
    esp_app_desc_t running_app_info;
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (esp_ota_get_partition_description(running, &running_app_info) == ESP_OK) {
        lua_pushstring(L, running_app_info.version);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int sys_spiffs_info(lua_State *L) 
{
    size_t total = 0, used = 0;
    if (esp_spiffs_info(NULL, &total, &used) != ESP_OK) {
        lua_pushboolean(L, false);
        return 1;
    } else {
        lua_pushinteger(L, used);
        lua_pushinteger(L, total);
        return 2;
    }
}

static const luaL_Reg syslib[] = {
    {"init", sys_init},
    {"delay", sys_delay},
    {"sntp", sys_sntp},
    {"heap", sys_heap},
    {"restart", sys_restart},
    {"md5sum", sys_md5sum},
    {"version", sys_version},
    {"spiffs_info", sys_spiffs_info},
    {NULL, NULL}
};

LUAMOD_API int esp_lua_lib_sys(lua_State *L) 
{
    luaL_newlib(L, syslib);
    return 1;
}