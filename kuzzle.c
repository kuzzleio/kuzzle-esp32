#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "esp_log.h"
#include "esp_system.h"
#include <string.h>

#include "kuzzle.h"

#include "mqtt.h"

#define TAG "KUZZLE"

//!< Kuzzle sample IOT index and collections

#define K_INDEX_IOT "iot-3" //!< The index where our IOT collection where be located
#define K_COLLECTION_DEVICE_INFO \
    "device-info" //!< A collection of IOT devices (this collection will contain general inforamtion about our devices)
#define K_COLLECTION_DEVICE_STATES \
    "device-state" //!< A collection of IOT devices (this collection will contain general inforamtion about our devices)
#define K_COLLECTION_FW_UPDATES \
    "fw-updates" //!< This collection will keep track of available firmwares for our devices. Devices can query or/and subscribe
                 //! to this collection with filter to check for firmware updates

/// Kuzzle controllers
/// See http:// TODO:   for more details on Kuzzle controllers
#define K_CONTROLLER_REALTIME "realtime" //!< This the controller responsible for handling subscribtions
#define K_CONTROLLER_DOCUMENT "document" //!< This is the controller responsible for handle documents

//!< This tag is used to track response from a 'new firmware' query
#define REQ_ID_FW_UPDATE "fw_update"

//!< This tag is used to track response from a own state subscribtion query
#define REQ_ID_SUBSCRIBE_STATE "sub_state"

//!< This tag is used to track response from
//!<a firmware update notification subscribtion
#define REQ_ID_SUBSCRIBE_FW_UPDATE "subfw_update"

//!< This tag is used to track reponse from publish the device own state (this allow for avoiding trying
//! the apply and re-publish a state we just published)
#define REQ_ID_PUBLISH_OWN_STATE "publish_own_state"

//!< Kuzzle MQTT topics:

static const char* KUZZLE_REQUEST_TOPIC  = "Kuzzle/request";  //!< This is the MQTT where to post Kuzzle queries
static const char* KUZZLE_RESPONSE_TOPIC = "Kuzzle/response"; //!< This is the MQTT to susbcribe to receive Kuzzle responses

/// Device state publishing Kuzzle query fmt string
static const char* pubish_device_state_fmt =
    "{\"index\":\"" K_INDEX_IOT "\",\"collection\":\"" K_COLLECTION_DEVICE_STATES "\",\"controller\":\"" K_CONTROLLER_DOCUMENT
    "\",\"action\":\"create\",\"body\": { \"device_id\" : \"" K_DEVICE_ID_FMT "\", \"device_type\":\"%s\", \"state\" : %s}}";

/// Kuzzle subscribe query fmt string
/// Parameters:
/// @param collection: The collection
/// @param requestId: the id of the request in order to be able to identify the responce
/// @param body: a Kuzzle DSL query to subscribe to, see http://the doc TODO add link to documentation
static const char* subscribe_req_fmt =
    "{\"index\":\"" K_INDEX_IOT "\",\"collection\":\"%s\",\"controller\":\"" K_CONTROLLER_REALTIME
    "\",\"action\":\"subscribe\",\"requestId\":\"%s\",\"body\":%s}";

/// Kuzzle DSL queries for subscribing to own state and to fw update
static const char* subscribe_own_state_fmt  = "{\"equals\":{\"device_id\": \"" K_DEVICE_ID_FMT "\"}}";
static const char* subscribe_fw_updates_fmt = "{\"equals\":{\"target\": \"%s\"}}";

static const char* get_fw_update_req_fmt =
    "{\"index\":\"" K_INDEX_IOT "\",\"collection\":\"" K_COLLECTION_FW_UPDATES "\",\"controller\":\"" K_CONTROLLER_DOCUMENT
    "\",\"action\":\"search\",\"requestId\":\"" REQ_ID_FW_UPDATE "\",\"body\":"
    "{\"size\": 1,\"query\":{\"match\" :{\"target.keyword\":"
    "\"%d\"}},\"sort\":{\"_kuzzle_info.createdAt\":{\"order\":"
    "\"desc\"}}}}";

// -- MQTT callbacks --
static void _on_mqtt_connected(mqtt_client* client, mqtt_event_data_t* event_data);
static void _on_mqtt_disconnected(mqtt_client* client, mqtt_event_data_t* event_data);
static void _on_mqtt_kuzzle_response_subscribed(mqtt_client* client, mqtt_event_data_t* event_data);
static void _on_mqtt_published(mqtt_client* client, mqtt_event_data_t* event_data);
static void _on_mqtt_data_received(mqtt_client* client, mqtt_event_data_t* event_data);

// -- MQTT settings --
static mqtt_settings _mqtt_settings = {.auto_reconnect  = true,
                                       .host            = {0}, // or domain, ex: "google.com",
                                       .port            = 0,
                                       .client_id       = {0},
                                       .username        = "",
                                       .password        = "",
                                       .clean_session   = 0,
                                       .keepalive       = 120, // second
                                       .lwt_topic       = "",  //"/lwt",    // = "" for disable lwt, will don't care other options
                                       .lwt_msg         = "offline",
                                       .lwt_qos         = 0,
                                       .lwt_retain      = 0,
                                       .connected_cb    = _on_mqtt_connected,
                                       .disconnected_cb = _on_mqtt_disconnected,
                                       .subscribe_cb    = _on_mqtt_kuzzle_response_subscribed,
                                       .publish_cb      = _on_mqtt_published,
                                       .data_cb         = _on_mqtt_data_received};

typedef struct {
    kuzzle_settings_t* s;
    mqtt_client*       mqtt_client;
} kuzzle_priv_t;

static kuzzle_priv_t kuzzle = {0};

k_err_t kuzzle_init(kuzzle_settings_t* settings)
{
    ESP_LOGD(TAG, "Initialising Kuzzle");

    if (kuzzle.mqtt_client != NULL) {
        ESP_LOGW(TAG, "Kuzzle alraydai initialized...");
        return K_ERR_ALREADY_INIT;
    }

    kuzzle.s = settings;

    memcpy(_mqtt_settings.host, settings->host, strlen(settings->host));
    _mqtt_settings.port = settings->port;
    snprintf(_mqtt_settings.client_id, CONFIG_MQTT_MAX_CLIENT_LEN, K_DEVICE_ID_FMT, K_DEVICE_ID_ARGS(settings->device_id));

    kuzzle.mqtt_client = mqtt_start(&_mqtt_settings);

    return kuzzle.mqtt_client != NULL ? K_ERR_NONE : K_ERR_NO_MEM;
}

/**
 * @brief kuzzle_check_for_update
 *
 * Check with the back-end if a newer version of the firmware
 * is available for download
 */
void kuzzle_query_for_fw_update()
{
    if (kuzzle.mqtt_client == NULL) {
        ESP_LOGW(TAG, "MQTT client not initialized yet...")
    } else {
        ESP_LOGD(TAG, "Publishing msg: %s", get_fw_update_req_fmt);
        mqtt_publish(kuzzle.mqtt_client, KUZZLE_REQUEST_TOPIC, get_fw_update_req_fmt, strlen(get_fw_update_req_fmt), 0, 0);
    }
}

/**
 * @brief kuzzle_device_state_pub
 * @param device_state: the state of the device as a JSON object string (e.g. "{ "myprop1": "value", "myprop2": false ...}")
 */
void kuzzle_device_state_pub(const char* device_state)
{
    if (kuzzle.mqtt_client == NULL) {
        ESP_LOGW(TAG, "MQTT client not initialized yet...")
    } else {
        char req_buffer[K_REQUEST_MAX_SIZE] = {0};

        // TODO: Add error handling...
        snprintf(req_buffer,
                 K_REQUEST_MAX_SIZE,
                 pubish_device_state_fmt,
                 K_DEVICE_ID_ARGS(kuzzle.s->device_id),
                 kuzzle.s->device_type,
                 device_state);

        ESP_LOGD(TAG, "Publishing msg: %s", req_buffer);
        mqtt_publish(kuzzle.mqtt_client, KUZZLE_REQUEST_TOPIC, req_buffer, strlen(req_buffer), 0, 0);
    }
}

/**
 * @brief kuzzle_subscribe_to_state
 *
 * Subscribe to own state to update from cloud
 */
void kuzzle_device_own_state_sub()
{
    ESP_LOGD(TAG, "Subscribing to own state: " K_DEVICE_ID_FMT, K_DEVICE_ID_ARGS(kuzzle.s->device_id));

    if (kuzzle.mqtt_client == NULL) {
        ESP_LOGW(TAG, "MQTT client not initialized yet...");
    } else {
        char query_buffer[K_DOCUMENT_MAX_SIZE] = {0};
        char req_buffer[K_REQUEST_MAX_SIZE]    = {0};

        // TODO: Add error handling...
        snprintf(query_buffer, K_DOCUMENT_MAX_SIZE, subscribe_own_state_fmt, K_DEVICE_ID_ARGS(kuzzle.s->device_id));
        snprintf(
            req_buffer, K_REQUEST_MAX_SIZE, subscribe_req_fmt, K_COLLECTION_DEVICE_STATES, REQ_ID_SUBSCRIBE_STATE, query_buffer);

        ESP_LOGD(TAG, "Publishing msg: %s", req_buffer);
        mqtt_publish(kuzzle.mqtt_client, KUZZLE_REQUEST_TOPIC, req_buffer, strlen(req_buffer), 0, 0);
    }
}

void kuzzle_fw_update_sub()
{
    ESP_LOGD(TAG, "Subscribing to fw update %s", kuzzle.s->device_type);

    if (kuzzle.mqtt_client == NULL) {
        ESP_LOGW(TAG, "MQTT client not initialized yet...")
    } else {
        char query_buffer[K_DOCUMENT_MAX_SIZE] = {0};
        char req_buffer[K_REQUEST_MAX_SIZE]    = {0};

        // TODO: Add error handling...
        snprintf(query_buffer, K_DOCUMENT_MAX_SIZE, subscribe_fw_updates_fmt, kuzzle.s->device_type);

        snprintf(
            req_buffer, K_REQUEST_MAX_SIZE, subscribe_req_fmt, K_COLLECTION_FW_UPDATES, REQ_ID_SUBSCRIBE_FW_UPDATE, query_buffer);

        ESP_LOGD(TAG, "Publishing msg: %s", req_buffer);
        mqtt_publish(kuzzle.mqtt_client, KUZZLE_REQUEST_TOPIC, req_buffer, strlen(req_buffer), 0, 0);
    }
}

void _on_device_state_changed(cJSON* jresponse)
{
    ESP_LOGD(TAG, "Device state changed");

    if (kuzzle.s->on_device_state_changed_notification) {
        ESP_LOGD(TAG, "->Calling app callback");
        kuzzle.s->on_device_state_changed_notification(jresponse);
    }
}

static void _on_fw_update_pushed(cJSON* jresponse)
{
    ESP_LOGD(TAG, "Firmware update pushed from Kuzzle");

    cJSON* jresult = cJSON_GetObjectItem(jresponse, "result");
    cJSON* jfwdoc  = cJSON_GetObjectItem(jresult, "_source");

    if (kuzzle.s->on_fw_update_notification) {
        ESP_LOGD(TAG, "->Calling app callback");
        kuzzle.s->on_fw_update_notification(jfwdoc);
    }
}

/**
 * @brief _on_response
 * @param jresponse the cJSON of Kuzzle response
 */
static void _on_response(cJSON* jresponse)
{
    cJSON* jrequestid = cJSON_GetObjectItem(jresponse, "requestId");
    assert(jrequestid != NULL);
    assert(jrequestid->type == cJSON_String);

    cJSON* jstatus = cJSON_GetObjectItem(jresponse, "status");
    assert(jstatus != NULL);

    int16_t status_value = jstatus->valueint;

    if (jstatus) {
        ESP_LOGD(TAG, "Kuzzle response: status = %d", status_value);
    } else {
        ESP_LOGE(TAG, "ERROR: jstatus is NULL!!!!");
    }

    if (status_value == K_STATUS_NO_ERROR) {
        if (strcmp(REQ_ID_FW_UPDATE, jrequestid->valuestring) == 0) {
            ESP_LOGD(TAG, "response to fw_update req");
            // -- received response from fw_update request -- //
            cJSON* jresult = cJSON_GetObjectItem(jresponse, "result");
            cJSON* jtotal  = cJSON_GetObjectItem(jresult, "total");
            assert(jtotal->type == cJSON_Number);

            if (jtotal->valueint < 1) {
                ESP_LOGW(TAG, "No info found about available firmware");
            } else {
                cJSON* jhits  = cJSON_GetObjectItem(jresult, "hits");
                cJSON* jfwdoc = cJSON_GetObjectItem(cJSON_GetArrayItem(jhits, 0), "_source");

                if (kuzzle.s->on_fw_update_notification != NULL) {
                    ESP_LOGD(TAG, "-> Call application callback");
                    kuzzle.s->on_fw_update_notification(jfwdoc);
                }
            }
        } else if (strcmp(REQ_ID_SUBSCRIBE_STATE, jrequestid->valuestring) == 0) {
            ESP_LOGD(TAG, LOG_COLOR(LOG_COLOR_GREEN) "Received response from STATE sub");

            cJSON* jresult  = cJSON_GetObjectItem(jresponse, "result");
            cJSON* jchannel = cJSON_GetObjectItem(jresult, "channel");

            assert(jchannel->type == cJSON_String);

            mqtt_subscribe(kuzzle.mqtt_client, jchannel->valuestring, 0);
        } else if (strcmp(REQ_ID_SUBSCRIBE_FW_UPDATE, jrequestid->valuestring) == 0) {
            ESP_LOGD(TAG, LOG_COLOR(LOG_COLOR_GREEN) "Received response from FW UPDATES sub");

            cJSON* jresult  = cJSON_GetObjectItem(jresponse, "result");
            cJSON* jchannel = cJSON_GetObjectItem(jresult, "channel");

            assert(jchannel->type == cJSON_String);

            mqtt_subscribe(kuzzle.mqtt_client, jchannel->valuestring, 0);
        }
    }
}

/**
 * @brief mqtt_connected
 * @param client
 * @param event_data
 */
void _on_mqtt_connected(mqtt_client* client, mqtt_event_data_t* event_data)
{
    ESP_LOGD(TAG, "MQTT: connected");

    // Scubscribe to Kuzzle response topic
    mqtt_subscribe(client, KUZZLE_RESPONSE_TOPIC, 0);
}

/**
 * @brief mqtt_disconnected
 * @param client
 * @param event_data
 */
void _on_mqtt_disconnected(mqtt_client* client, mqtt_event_data_t* event_data) { ESP_LOGD(TAG, "MQTT: disconnected"); }

/**
 * @brief mqtt_kuzzle_light_state_subscribed
 * @param client
 * @param event_data
 */
void _on_mqtt_fw_updates_subscribed(mqtt_client* client, mqtt_event_data_t* event_data)
{
    ESP_LOGD(TAG, "MQTT: subscribed to fw updates");
}
/**
 * @brief mqtt_kuzzle_light_state_subscribed
 * @param client
 * @param event_data
 */
void _on_device_state_subscribed(mqtt_client* client, mqtt_event_data_t* event_data)
{
    ESP_LOGD(TAG, "MQTT: subscribed to light state");

    client->settings->subscribe_cb = _on_mqtt_fw_updates_subscribed;
    kuzzle_fw_update_sub();
}

/**
 * @brief mqtt_kuzzle_response_subscribed
 * @param client
 * @param event_data
 */
static void _on_mqtt_kuzzle_response_subscribed(mqtt_client* client, mqtt_event_data_t* event_data)
{
    ESP_LOGD(TAG, "MQTT: subscribed to topic: %s", KUZZLE_RESPONSE_TOPIC);
    // -- publish current state --
    //    kuzzle_publish_state();
    // -- subscribe to own state --

    client->settings->subscribe_cb = _on_device_state_subscribed;
    kuzzle_device_own_state_sub();
}

/**
 * @brief mqtt_published
 * @param client
 * @param event_data
 */
static void _on_mqtt_published(mqtt_client* client, mqtt_event_data_t* event_data) { ESP_LOGD(TAG, "MQTT: published"); }

/**
 * @brief mqtt_data_received
 * @param client
 * @param event_data
 */
static void _on_mqtt_data_received(mqtt_client* client, mqtt_event_data_t* event_data)
{
    ESP_LOGD(TAG, "MQTT: data received:");

    ESP_LOGD(TAG, "\tfrom topic: %.*s", event_data->topic_length, event_data->topic);
    ESP_LOGD(TAG, "\tdata: %.*s", event_data->data_length, event_data->data + event_data->data_offset);

    /* -- Parse response status -- */

    cJSON* jresponse = cJSON_Parse(event_data->data + event_data->data_offset); // cJASON_Parse
    // doesn't need a null
    // terminated string
    assert(jresponse != NULL);

    if (event_data->topic_length == strlen(KUZZLE_RESPONSE_TOPIC) &&
        strncmp(event_data->topic, KUZZLE_RESPONSE_TOPIC, event_data->topic_length) == 0) {
        _on_response(jresponse);
    } else {
        // switch according to the source collection to see if its a FW_UPDATE or STATE change nofitication
        // As we subscribe only once per collection, we can use the collection name to identify the source
        // of the notification

        cJSON* jcollection = cJSON_GetObjectItem(jresponse, "collection");

        if (strcmp(jcollection->valuestring, K_COLLECTION_DEVICE_STATES) == 0) {
            _on_device_state_changed(jresponse);
        } else if (strcmp(jcollection->valuestring, K_COLLECTION_FW_UPDATES) == 0)
            _on_fw_update_pushed(jresponse);
    }

    cJSON_Delete(jresponse);

    ESP_LOGD("MEM", LOG_BOLD(LOG_COLOR_PURPLE) "free mem: %d", esp_get_free_heap_size());
}
