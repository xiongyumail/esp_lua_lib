
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "tcpip_adapter.h"
#include "esp_tls.h"

#include "esp_http_client.h"
#include "esp_lua_lib.h"

static const char *TAG = "esp_lib_web";

static char *http_rest_get_with_url(char *url, char * cert_pem)
{
    esp_http_client_config_t config = {
        .url = url,
        .cert_pem = cert_pem,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err;
    if ((err = esp_http_client_open(client, 0)) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return NULL;
    }
    int content_length =  esp_http_client_fetch_headers(client);
    if (content_length <= 0) {
        ESP_LOGE(TAG, "Error content length");
        esp_http_client_cleanup(client);
        return NULL;
    }
    char *buf = calloc(sizeof(char), content_length + 1);
    int read_len = esp_http_client_read(client, buf, content_length);
    if (read_len <= 0) {
        ESP_LOGE(TAG, "Error read data");
        esp_http_client_cleanup(client);
        free(buf);
        return NULL;
    }
    buf[read_len] = 0;

    ESP_LOGI(TAG, "HTTP Stream reader Status = %d, content_length = %d",
                    esp_http_client_get_status_code(client),
                    esp_http_client_get_content_length(client));
    esp_http_client_cleanup(client);

    return buf;
}

static int http_rest_file_with_url(char *url, char *file, char * cert_pem)
{
    esp_http_client_config_t config = {
        .url = url,
        .cert_pem = cert_pem,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err;
    if ((err = esp_http_client_open(client, 0)) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return -1;
    }
    int content_length =  esp_http_client_fetch_headers(client);
    if (content_length <= 0) {
        ESP_LOGE(TAG, "Error content length");
        esp_http_client_cleanup(client);
        return -1;
    }
    FILE* f = fopen(file, "w");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for writing");
        esp_http_client_cleanup(client);
        return -1;
    }
    char *buf = calloc(sizeof(char), BUFSIZ);
    int read_len = 0;
    int write_len = 0;
    for (int x = 0; x < content_length / BUFSIZ; x++) {
        read_len = esp_http_client_read(client, buf, BUFSIZ);
        if (read_len != BUFSIZ) {
            ESP_LOGE(TAG, "Error read data");
            esp_http_client_cleanup(client);
            fclose(f);
            free(buf);
            return -1;
        }
        write_len = fwrite(buf, sizeof(char), read_len, f);
        if (write_len != BUFSIZ) {
            ESP_LOGE(TAG, "File write data");
            esp_http_client_cleanup(client);
            fclose(f);
            free(buf);
            return -1;
        }
    }
    if (content_length % BUFSIZ) {
        read_len = esp_http_client_read(client, buf, content_length % BUFSIZ);
        if (read_len != content_length % BUFSIZ) {
            ESP_LOGE(TAG, "Error read data");
            esp_http_client_cleanup(client);
            fclose(f);
            free(buf);
            return -1;
        }
        write_len = fwrite(buf, sizeof(char), read_len, f);
        if (write_len <= 0) {
            ESP_LOGE(TAG, "File write data");
            esp_http_client_cleanup(client);
            fclose(f);
            free(buf);
            return -1;
        }
    }

    ESP_LOGI(TAG, "HTTP Stream reader Status = %d, content_length = %d",
                    esp_http_client_get_status_code(client),
                    esp_http_client_get_content_length(client));
    esp_http_client_cleanup(client);
    fclose(f);
    free(buf);
    return content_length;
}

static char *http_rest_post_with_url(char *url, char *post, char * cert_pem)
{
    esp_http_client_config_t config = {
        .url = url,
        .cert_pem = cert_pem,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_post_field(client, post, strlen(post));
    esp_err_t err;
    if ((err = esp_http_client_open(client, 0)) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return NULL;
    }
    int content_length =  esp_http_client_fetch_headers(client);
    if (content_length <= 0) {
        ESP_LOGE(TAG, "Error content length");
        esp_http_client_cleanup(client);
        return NULL;
    }
    char *buf = calloc(sizeof(char), content_length + 1);
    int read_len = esp_http_client_read(client, buf, content_length);
    if (read_len <= 0) {
        ESP_LOGE(TAG, "Error read data");
        esp_http_client_cleanup(client);
        free(buf);
        return NULL;
    }
    buf[read_len] = 0;

    ESP_LOGI(TAG, "HTTP Stream reader Status = %d, content_length = %d",
                    esp_http_client_get_status_code(client),
                    esp_http_client_get_content_length(client));
    esp_http_client_cleanup(client);

    return buf;
}

// GET:  web.rest('GET', url[, cert])
// POST: web.rest('POST', url, post[, cert])
static int web_rest(lua_State *L) 
{
    char *buf = NULL;
    char *cert_pem = "";

    char *method = luaL_checklstring(L, 1, NULL);

    if (strcmp(method, "GET") == 0) {
        if (lua_tostring(L, 3) != NULL) {
            cert_pem = lua_tostring(L, 3);
        }

        buf = http_rest_get_with_url(luaL_checklstring(L, 2, NULL), cert_pem);
    } else if (strcmp(method, "POST") == 0) {
        if (lua_tostring(L, 4) != NULL) {
            cert_pem = lua_tostring(L, 4);
        }

        buf = http_rest_post_with_url(luaL_checklstring(L, 2, NULL), luaL_checklstring(L, 3, NULL), cert_pem);
    }

    if (buf != NULL) {
        lua_pushstring(L, buf);
        free(buf);
    } else {
        lua_pushboolean(L, false);
    }

    return 1;
}

static int web_file(lua_State *L) 
{
    int ret = -1;

    char *file = luaL_checklstring(L, 1, NULL);
    char *url = luaL_checklstring(L, 2, NULL);
    char *cert_pem = "";

    if (lua_tostring(L, 3) != NULL) {
        cert_pem = lua_tostring(L, 3);
    }

    ret = http_rest_file_with_url(url, file, cert_pem);

    if (ret >= 0) {
        lua_pushinteger(L, ret);
    } else {
        lua_pushboolean(L, false);
    }
    
    return 1;
}

#include "esp_ota_ops.h"
#include "esp_https_ota.h"

static int simple_ota(char *url, char * cert_pem)
{
    esp_http_client_config_t config = {
        .url = url,
        .cert_pem = cert_pem,
    };

    esp_err_t ret = esp_https_ota(&config);
    if (ret == ESP_OK) {
        return 0;
    } else {
        return -1;
    }
}

static int web_ota(lua_State *L) 
{
    int ret = -1;

    char *url = luaL_checklstring(L, 1, NULL);
    char *cert_pem = "";

    if (lua_tostring(L, 2) != NULL) {
        cert_pem = lua_tostring(L, 2);
    }

    if (simple_ota(url, cert_pem) == 0) {
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    
    return 1;
}

static const luaL_Reg weblib[] = {
    {"rest",   web_rest},
    {"file",   web_file},
    {"ota",    web_ota},
    {NULL, NULL}
};

LUAMOD_API int esp_lib_web(lua_State *L) 
{
    luaL_newlib(L, weblib);
    lua_pushstring(L, "0.1.0");
    lua_setfield(L, -2, "_version");
    return 1;
}
