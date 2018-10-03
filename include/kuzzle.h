#ifndef __KUZZLE_IOT_H
#define __KUZZLE_IOT_H

#include "cJSON.h"
#include "stdint.h"

#define K_DOCUMENT_MAX_SIZE 512
#define K_REQUEST_MAX_SIZE 1024
#define K_DEVICE_ID_MAX_SIZE 32
#define K_STATUS_NO_ERROR 200

typedef enum {
    K_ERR_NONE = 0,
    K_ERR_NO_MEM,      //!< No more memory
    K_ERR_ALREADY_INIT //!< Kuzzle component already has  been initilized
} k_err_t;

typedef void (*kuzzle_callback)(cJSON* jresponse);
typedef void (*kuzzle_connected_cd)(void);

typedef char  k_device_id_t[K_DEVICE_ID_MAX_SIZE];
typedef char* k_device_type_t;

typedef struct kuzzle_settings {
    k_device_id_t   device_id;
    k_device_type_t device_type;

    char*    host;
    uint32_t port;

    const char* username;
    const char* password;

    kuzzle_callback     on_fw_update_notification;
    kuzzle_callback     on_device_state_changed_notification;
    kuzzle_connected_cd on_connected;

} kuzzle_settings_t;

#define K_DEVICE_ID_FMT "%s"
#define K_DEVICE_ID_ARGS(device_id) (device_id)

/**
 * @brief kuzzle_init
 * @param client: mqtt client to use to send Kuzzle requests/receive Kuzzle responces
 */
k_err_t kuzzle_init(kuzzle_settings_t* settings);

/**
 * @brief kuzzle_fw_update_sub
 * @param device_type: the type of the target firmware device we want to subscribe to
 */
void kuzzle_fw_update_sub();

/**
 * @brief kuzzle_device_own_state_sub
 */
void kuzzle_device_own_state_sub();

/**
 * @brief kuzzle_device_state_pub
 * @param device_state: the state of the device as a JSON object string (e.g. "{ "myprop1": "value", "myprop2": false ...}")
 */
void kuzzle_device_state_pub(const char* jstate);

/**
 * @brief kuzzle_get_device_id
 * @return
 */
const char *kuzzle_get_device_id();


#endif // __KUZZLE_IOT_H
