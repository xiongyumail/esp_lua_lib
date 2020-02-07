
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
#include "mqtt_client.h"
#include "esp_lua_lib.h"

static const char *TAG = "esp_lua_lib_web";

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

static esp_mqtt_client_handle_t client = NULL;
static QueueHandle_t mqtt_event_queue = NULL;

typedef struct {
    char *event;
    char *data;
} mqtt_event_t;

static int mqtt_event_send(char *event, char *data)
{
    mqtt_event_t e;
    e.event = calloc(sizeof(char), strlen(event) + 1);
    e.data = calloc(sizeof(char), strlen(data) + 1);
    strcpy(e.event, event);
    strcpy(e.data, data);
    if (xQueueSend(mqtt_event_queue, (void *)&e, 0) != pdTRUE) {
        free(e.event);
        free(e.data);
        return -1;
    }
    return 0;
}

static esp_err_t mqtt_event_handler_cb(esp_mqtt_event_handle_t event)
{
    esp_mqtt_client_handle_t client = event->client;
    char msg_id_str[16] = "";
    sprintf(msg_id_str, "%d", event->msg_id);
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
            mqtt_event_send("MQTT_EVENT_CONNECTED", "");
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
            mqtt_event_send("MQTT_EVENT_DISCONNECTED", "");
            break;
        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
            
            mqtt_event_send("MQTT_EVENT_SUBSCRIBED", msg_id_str);
            break;
        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
            mqtt_event_send("MQTT_EVENT_UNSUBSCRIBED", msg_id_str);
            break;
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            mqtt_event_send("MQTT_EVENT_PUBLISHED", msg_id_str);
            break;
        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "MQTT_EVENT_DATA");
            char *data = calloc(sizeof(char), event->topic_len + event->data_len + 100);
            sprintf(data, "{\"topic\":\"%.*s\",\"data\":%.*s}", event->topic_len, event->topic, event->data_len, event->data);
            mqtt_event_send("MQTT_EVENT_DATA", data);
            free(data);
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
            mqtt_event_send("MQTT_EVENT_ERROR", "");
            break;
        default:
            ESP_LOGI(TAG, "Other event id:%d", event->event_id);
            break;
    }
    return ESP_OK;
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%d", base, event_id);
    mqtt_event_handler_cb(event_data);
}

static int mqtt_app_start(char *url, void *context)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .uri = url,
        .user_context = context,
    };

    client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, client);
    esp_mqtt_client_start(client);

    return 0;
}

/*
function mqtt_event_callback(event, data)
    print('event: '..event..' data: '..data)
end

web.mqtt('START', 'mqtt://mqtt.emake.run', 100)
web.mqtt('SUB', 'topic', 0)
web.mqtt('PUB', 'topic', 'data', 0)
web.mqtt('UNSUB', 'topic')
web.mqtt('STOP')
*/
static int web_mqtt(lua_State *L) 
{
    int ret = -1;
    char *method = luaL_checklstring(L, 1, NULL);
    static int state = 0;

    if (strcmp(method, "START") == 0) {
        if (state != 0) {
            esp_mqtt_client_stop(client);
            vQueueDelete(mqtt_event_queue);
            state = 0;
        }
        mqtt_event_queue = xQueueCreate(luaL_checkinteger(L, 3), sizeof(mqtt_event_t));
        ret = mqtt_app_start(luaL_checklstring(L, 2, NULL), (void*)L);
        state = 1;
    } else if (strcmp(method, "SUB") == 0) {
        ret = esp_mqtt_client_subscribe(client, luaL_checklstring(L, 2, NULL), luaL_checkinteger(L, 3));
    } else if (strcmp(method, "PUB") == 0) {
        ret = esp_mqtt_client_publish(client, luaL_checklstring(L, 2, NULL), luaL_checklstring(L, 3, NULL), 0, luaL_checkinteger(L, 4), 0);
    } else if (strcmp(method, "UNSUB") == 0) {
        ret = esp_mqtt_client_unsubscribe(client, luaL_checklstring(L, 2, NULL));
    } else if (strcmp(method, "STOP") == 0) {
        ret = esp_mqtt_client_stop(client);
        vQueueDelete(mqtt_event_queue);
        state = 0;
    } else if (strcmp(method, "HANDLE") == 0) {
        mqtt_event_t e;
        if (state && xQueueReceive(mqtt_event_queue, (void *)&e, luaL_checkinteger(L, 2) / portTICK_RATE_MS) == pdTRUE) {
            lua_pushstring(L, e.event);
            lua_pushstring(L, e.data);
            free(e.event);
            free(e.data);
            return 2;
        } else {
            ret = -1;
        }
    } else { 
        ret = -1;
    }

    lua_pushboolean(L, (ret >=0) ? true : false);
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
    {"mqtt",   web_mqtt},
    {"file",   web_file},
    {"ota",    web_ota},
    {NULL, NULL}
};

LUAMOD_API int esp_lua_lib_web(lua_State *L) 
{
    luaL_newlib(L, weblib);
    return 1;
}
