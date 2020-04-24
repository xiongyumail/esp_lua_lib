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
#include "esp_event.h"

static const char *TAG = "esp_lib_sys";

#define SYS_NAMESPACE "sys"

static char *nvs_read(char *space, char *key)
{
    nvs_handle_t my_handle;
    esp_err_t err;
    char* value = NULL;

    if (strcmp(space, SYS_NAMESPACE) == 0) {
        return NULL;
    }

    // Open
    err = nvs_open(space, NVS_READONLY, &my_handle);
    if (err != ESP_OK) return NULL;

    // Read the size of memory space required for blob
    size_t required_size = 0;  // value will default to 0, if not set yet in NVS
    err = nvs_get_blob(my_handle, key, NULL, &required_size);
    if (err != ESP_OK) {
        nvs_close(my_handle);
        return NULL;
    }

    if (required_size > 0) {
        // Read previously saved blob if available
        value = calloc(sizeof(char), required_size + sizeof(uint32_t));
        err = nvs_get_blob(my_handle, key, value, &required_size);
        if (err != ESP_OK) {
            free(value);
            nvs_close(my_handle);
            return NULL;
        }
    }

    // Close
    nvs_close(my_handle);
    return value;
}

static char *nvs_write(char *space, char *key, char *value)
{
    nvs_handle_t my_handle;
    esp_err_t err;

    if (strcmp(space, SYS_NAMESPACE) == 0) {
        return NULL;
    }
    // Open
    err = nvs_open(space, NVS_READWRITE, &my_handle);
    if (err != ESP_OK) return NULL;

    // Write
    err = nvs_set_blob(my_handle, key, value, strlen(value) + 1);
    if (err != ESP_OK) {
        nvs_close(my_handle);
        return NULL;
    }

    // Commit written value.
    // After setting any values, nvs_commit() must be called to ensure changes are written
    // to flash storage. Implementations may write to storage at other times,
    // but this is not guaranteed.
    err = nvs_commit(my_handle);
    if (err != ESP_OK) {
        nvs_close(my_handle);
        return NULL;
    }

    // Close
    nvs_close(my_handle);
    return value;
}

static int nvs_erase(char *space, char *key)
{
    nvs_handle_t my_handle;
    esp_err_t err;

    if (strcmp(space, SYS_NAMESPACE) == 0) {
        return -1;
    }
    // Open
    err = nvs_open(space, NVS_READWRITE, &my_handle);
    if (err != ESP_OK) return -1;

    if (key != NULL) {
        err = nvs_erase_key(my_handle, key);
    } else {
        err = nvs_erase_all(my_handle);
    }  
    if (err != ESP_OK) {
        nvs_close(my_handle);
        return -1;
    }

    // Commit written value.
    // After setting any values, nvs_commit() must be called to ensure changes are written
    // to flash storage. Implementations may write to storage at other times,
    // but this is not guaranteed.
    err = nvs_commit(my_handle);
    if (err != ESP_OK) {
        nvs_close(my_handle);
        return -1;
    }
    // Close
    nvs_close(my_handle);
    return 0;
}

static int get_boot_count(void)
{
    nvs_handle_t my_handle;
    esp_err_t err;

    // Open
    err = nvs_open(SYS_NAMESPACE, NVS_READWRITE, &my_handle);
    if (err != ESP_OK) return -1;

    // Read
    int boot_count = 0; // value will default to 0, if not set yet in NVS
    err = nvs_get_i32(my_handle, "boot_count", &boot_count);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(my_handle);
        return -1;
    }

    // Close
    nvs_close(my_handle);
    return boot_count;
}

static int save_boot_count(void)
{
    nvs_handle_t my_handle;
    esp_err_t err;

    // Open
    err = nvs_open(SYS_NAMESPACE, NVS_READWRITE, &my_handle);
    if (err != ESP_OK) return -1;

    // Read
    int boot_count = 0; // value will default to 0, if not set yet in NVS
    err = nvs_get_i32(my_handle, "boot_count", &boot_count);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(my_handle);
        return -1;
    }

    // Write
    boot_count++;
    err = nvs_set_i32(my_handle, "boot_count", boot_count);
    if (err != ESP_OK) {
        nvs_close(my_handle);
        return -1;
    }

    // Commit written value.
    // After setting any values, nvs_commit() must be called to ensure changes are written
    // to flash storage. Implementations may write to storage at other times,
    // but this is not guaranteed.
    err = nvs_commit(my_handle);
    if (err != ESP_OK) {
        nvs_close(my_handle);
        return -1;
    }

    // Close
    nvs_close(my_handle);
    return boot_count;
}

static int sys_init(lua_State *L) 
{
    static int state = 0;
    int boot_count = -1;
    if (state != 0) {
        boot_count = save_boot_count();
        if (boot_count != -1) {
            lua_pushinteger(L, boot_count);
        } else {
            lua_pushboolean(L, false);
        }
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

    ESP_ERROR_CHECK(esp_event_loop_create_default());

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

    boot_count = save_boot_count();

    if (boot_count != -1) {
        lua_pushinteger(L, boot_count);
    } else {
        lua_pushboolean(L, false);
    }
    
    return 1;
}

static int sys_delay(lua_State *L) 
{
    uint32_t delay = (uint32_t)luaL_checknumber(L,1);
    lua_gc(L, LUA_GCCOLLECT, 0); // collect memory
    vTaskDelay(delay / portTICK_RATE_MS);
    lua_pushboolean(L, true);
    return 1;
}

static int sys_yield(lua_State *L) 
{
    // portYIELD();
    lua_gc(L, LUA_GCCOLLECT, 0); // collect memory
    vTaskDelay(10 / portTICK_RATE_MS);
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

static int sys_info(lua_State *L) 
{
    lua_newtable(L);

#if CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS
    char *PA = NULL;
    char *PB = NULL;
    char *cpu_info  = (char *)malloc(1024);
    vTaskGetRunTimeStats(cpu_info);

    PA = strstr(cpu_info, "IDLE0");
    if (PA) {
        PB = strstr(PA, "\t\t");
        if (PB) {
            PB = PB + strlen("\t\t");
            PA = strstr(PB, "\r\n");
            if (PA) {
                PB[PA-PB] = 0;
                lua_pushstring(L, "cpu");
                lua_pushinteger(L, 100 - atoi(PB));
                lua_settable(L,-3);
            }
        }
    }
    
    free(cpu_info);
#endif

    lua_pushstring(L, "total_heap");
    lua_pushinteger(L, esp_get_free_heap_size());
    lua_settable(L,-3);

    lua_pushstring(L, "heap");
    lua_pushinteger(L, heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    lua_settable(L,-3);

    int boot_count = get_boot_count();
    if (boot_count != -1) {
        lua_pushstring(L, "boot");
        lua_pushinteger(L, get_boot_count());
        lua_settable(L,-3);
    }

    esp_app_desc_t running_app_info;
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (esp_ota_get_partition_description(running, &running_app_info) == ESP_OK) {
        lua_pushstring(L, "version");
        lua_pushstring(L, running_app_info.version);
        lua_settable(L,-3);
    }

    return 1;
}

static int sys_spiffs_info(lua_State *L) 
{
    size_t total = 0, used = 0;
    if (esp_spiffs_info(NULL, &total, &used) != ESP_OK) {
        lua_pushboolean(L, false);
    } else {
        lua_newtable(L);
        lua_pushstring(L, "used");
        lua_pushinteger(L, used);
        lua_settable(L,-3);

        lua_pushstring(L, "total");
        lua_pushinteger(L, total);
        lua_settable(L,-3);
    }

    return 1;
}

static int sys_nvs_read(lua_State *L) 
{
    char *space = luaL_checklstring(L, 1, NULL);
    char *key = luaL_checklstring(L, 2, NULL);
    char *value = nvs_read(space, key);

    if (value != NULL) {
        lua_pushstring(L, value);
        free(value);
    } else {
        lua_pushboolean(L, false);
    }

    return 1;
}

static int sys_nvs_write(lua_State *L) 
{
    char *space = luaL_checklstring(L, 1, NULL);
    char *key = luaL_checklstring(L, 2, NULL);
    char *value = luaL_checklstring(L, 3, NULL);
    char *ret = nvs_write(space, key, value);
    if (ret != NULL) {
        lua_pushstring(L, ret);
    } else {
        lua_pushboolean(L, false);
    }

    return 1;
}

static int sys_nvs_erase(lua_State *L) 
{
    char *space = luaL_checklstring(L, 1, NULL);
    char *key = NULL;

    if (lua_tostring(L, 2) != NULL) {
        key = lua_tostring(L, 2);
    }

    if (nvs_erase(space, key) == 0) {
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }

    return 1;
}

static int sys_nvs_info(lua_State *L) 
{
    nvs_stats_t nvs_stats;
    if (nvs_get_stats(NULL, &nvs_stats) != ESP_OK) {
        lua_pushboolean(L, false);
        return 1;
    } else {
        lua_newtable(L);
        lua_pushstring(L, "used");
        lua_pushinteger(L, nvs_stats.used_entries);
        lua_settable(L,-3);
    
        lua_pushstring(L, "free");
        lua_pushinteger(L, nvs_stats.free_entries);
        lua_settable(L,-3);

        lua_pushstring(L, "all");
        lua_pushinteger(L, nvs_stats.total_entries);
        lua_settable(L,-3);

        lua_pushstring(L, "count");
        lua_pushinteger(L, nvs_stats.namespace_count);
        lua_settable(L,-3);
        return 1;
    }
}

static const luaL_Reg syslib[] = {
    {"init", sys_init},
    {"delay", sys_delay},
    {"yield", sys_yield},
    {"sntp", sys_sntp},
    {"restart", sys_restart},
    {"md5sum", sys_md5sum},
    {"info", sys_info},
    {"spiffs_info", sys_spiffs_info},
    {"nvs_read", sys_nvs_read},
    {"nvs_write", sys_nvs_write},
    {"nvs_erase", sys_nvs_erase},
    {"nvs_info", sys_nvs_info},
    {NULL, NULL}
};

LUAMOD_API int esp_lib_sys(lua_State *L) 
{
    luaL_newlib(L, syslib);
    lua_pushstring(L, "0.1.0");
    lua_setfield(L, -2, "_version");
    return 1;
}