
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "mqtt_client.h"
#include "esp_lua_lib.h"

static const char *TAG = "esp_lib_mqtt";

#define MQTT_EVENT_QUEUE_NUM 100

static esp_mqtt_client_handle_t client = NULL;
static QueueHandle_t mqtt_event_queue = NULL;

typedef struct {
    char *event;
    char *topic;
    char *data;
} mqtt_event_t;

static int mqtt_event_send(char *event, char *topic, char *data)
{
    mqtt_event_t e;

    if (event) {
        e.event = calloc(sizeof(char), strlen(event) + 1);
        strcpy(e.event, event);
    } else {
        return -1;
    }

    if (topic) {
        e.topic = calloc(sizeof(char), strlen(topic) + 1);
        strcpy(e.topic, topic);
    } else {
        e.topic = NULL;
    }

    if (data) {
        e.data = calloc(sizeof(char), strlen(data) + 1);
        strcpy(e.data, data);
    } else {
        e.data = NULL;
    } 
    
    if (xQueueSend(mqtt_event_queue, (void *)&e, 0) != pdTRUE) {
        free(e.event);
        free(e.topic);
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
            mqtt_event_send("MQTT_EVENT_CONNECTED", NULL, NULL);
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
            mqtt_event_send("MQTT_EVENT_DISCONNECTED", NULL, NULL);
            break;
        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
            
            mqtt_event_send("MQTT_EVENT_SUBSCRIBED", NULL, msg_id_str);
            break;
        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
            mqtt_event_send("MQTT_EVENT_UNSUBSCRIBED", NULL, msg_id_str);
            break;
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            mqtt_event_send("MQTT_EVENT_PUBLISHED", NULL, msg_id_str);
            break;
        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "MQTT_EVENT_DATA");
            char *topic = calloc(sizeof(char), event->topic_len + 1);
            char *data = calloc(sizeof(char), event->data_len + 1);
            // sprintf(topic, "%.*s", event->topic_len, event->topic);
            // sprintf(data, "%.*s", event->data_len, event->data);
            memcpy(topic, event->topic, event->topic_len);
            memcpy(data, event->data, event->data_len);
            mqtt_event_send("MQTT_EVENT_DATA", topic, data);
            free(topic);
            free(data);
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
            mqtt_event_send("MQTT_EVENT_ERROR", NULL, NULL);
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

static int mqtt_app_start(char *url, void *context, char * cert_pem)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .uri = url,
        .user_context = context,
        .cert_pem = cert_pem,
    };

    client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, client);
    esp_mqtt_client_start(client);

    return 0;
}

static int mqtt_app_stop()
{
    int ret = -1;
    ret = esp_mqtt_client_destroy(client);
    mqtt_event_t e;
    while (1) {
        if (xQueueReceive(mqtt_event_queue, (void *)&e, 0) == pdTRUE) {
            free(e.event);
            free(e.data);
        } else{
            break;
        }
    }
    vQueueDelete(mqtt_event_queue);
    client = NULL;

    return ret;
}

/*
[true, false] = mqtt.start(url[, cert])
[true, false] = mqtt.sub('topic', 0)
[true, false] = mqtt.pub('topic', 'data', 0)
[true, false] = mqtt.unsub('topic')
[{event, data}, false] = mqtt.run([timeout_ms])
[true, false] = mqtt.stop()
*/
static int mqtt_start(lua_State *L) 
{
    int ret = -1;

    if (client != NULL) {
        ret = mqtt_app_stop();
    }
    char *cert_pem = "";
    if (lua_tostring(L, 2) != NULL) {
        cert_pem = lua_tostring(L, 2);
    }
    mqtt_event_queue = xQueueCreate(MQTT_EVENT_QUEUE_NUM, sizeof(mqtt_event_t));
    ret = mqtt_app_start(luaL_checklstring(L, 1, NULL), (void*)L, cert_pem);

    lua_pushboolean(L, (ret >=0) ? true : false);
    return 1;
}

static int mqtt_sub(lua_State *L) 
{
    int ret = -1;

    if (client != NULL) {
        ret = esp_mqtt_client_subscribe(client, luaL_checklstring(L, 1, NULL), luaL_checkinteger(L, 2));
    }

    lua_pushboolean(L, (ret >=0) ? true : false);
    return 1;
}

static int mqtt_pub(lua_State *L) 
{
    int ret = -1;

    if (client != NULL) {
        ret = esp_mqtt_client_publish(client, luaL_checklstring(L, 1, NULL), luaL_checklstring(L, 2, NULL), 0, luaL_checkinteger(L, 3), 0);
    }

    lua_pushboolean(L, (ret >=0) ? true : false);
    return 1;
}

static int mqtt_unsub(lua_State *L) 
{
    int ret = -1;

    if (client != NULL) {
        ret = esp_mqtt_client_unsubscribe(client, luaL_checklstring(L, 1, NULL));  
    } 

    lua_pushboolean(L, (ret >=0) ? true : false);
    return 1;
}

static int mqtt_run(lua_State *L) 
{
    int ret = -1;

    mqtt_event_t e;
    if (client != NULL) {
        if (xQueueReceive(mqtt_event_queue, (void *)&e, 0) == pdTRUE) {
            lua_newtable(L);
            lua_pushstring(L, "event");
            lua_pushstring(L, e.event);
            lua_settable(L,-3);
            free(e.event);
        
            if (e.topic) {
                lua_pushstring(L, "topic");
                lua_pushstring(L, e.topic);
                lua_settable(L,-3);
                free(e.topic);
            }

            if (e.data) {
                lua_pushstring(L, "data");
                lua_pushstring(L, e.data);
                lua_settable(L,-3);
                free(e.data);
            }

            return 1;
        } else {
            ret = -1;
        }
    }

    lua_pushboolean(L, (ret >=0) ? true : false);
    return 1;
}

static int mqtt_stop(lua_State *L) 
{
    int ret = -1;

    if (client != NULL) {
        ret = mqtt_app_stop();
    }

    lua_pushboolean(L, (ret >=0) ? true : false);
    return 1;
}

static const luaL_Reg mqttlib[] = {
    {"start",   mqtt_start},
    {"sub",   mqtt_sub},
    {"pub",   mqtt_pub},
    {"unsub",   mqtt_unsub},
    {"run",   mqtt_run},
    {"stop",   mqtt_stop},
    {NULL, NULL}
};

LUAMOD_API int esp_lib_mqtt(lua_State *L) 
{
    luaL_newlib(L, mqttlib);
    lua_pushstring(L, "0.1.0");
    lua_setfield(L, -2, "_version");
    return 1;
}
