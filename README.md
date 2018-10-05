# kuzzle-esp32

This a simple ESP32 component allowing using Kuzzle as backend for your IoT device base on [Espressif ESP32](https://www.espressif.com/en/products/hardware/esp32/overview) microcontrollers.

## Add it to your project

To make `kuzzle-esp32` component availlable to your project, you just need to clone it in the `components` folder of your ESP32 project.

As it depends on `esp-mqtt` components, this one also needs to be cloned in your `components` folder.

``` console
$ git submodule add https://github.com/espressif/esp-mqtt components/esp-mqtt
$ git submodule add https://github.com/kuzzleio/kuzzle-esp32 components/kuzzle-esp32
```

Your project folder structure should look like this:

``` console
$ tree -d -L 2
.
├── components
│   ├── esp-mqtt
│   └── kuzzle-esp32
└── main
```

## Setup your Kuzzle

In order to use this component, you will have to setup Kuzzle as describe in this [how-to](https://github.com/kuzzleio/kuzzle-ow-to)

## Usage

Initiallize and connect to Kuzzle:

``` c
#include "kuzzle.h"


static kuzzle_settings_t _k_settings = {
    .host = "your.kuzzle.instance",
    .port = 1883,   // Kuzzle MQTT port
    .device_type = "your-device-type-id",
    .device_id = "my-device-id", // This has to be unique for each device
    .on_fw_update_notification = NULL,
    .on_device_state_changed_notification = on_device_state_change_request,  // a callback that will be called when a state change request is received from kuzzle
    .on_connected = on_kuzzle_connected
};

if(kuzzle_init(&_k_settings) != K_ERR_NONE) {
  ESP_LOGE(TAG, "Failed to initialise Kuzzle ESP32 module");
}
```

Receiving device state change request:

```c
void on_device_state_change_request(cJSON *jpartial_state)
{
  cJSON *jstatus = cJSON_GetObjectItem(jpartial_state, "status");
  assert(jstatus != NULL);

  int16_t status_value = jstatus->valueint;

  if (status_value == K_STATUS_NO_ERROR)
  {
    cJSON *jresult = cJSON_GetObjectItem(jpartial_state, "result");
    cJSON *jsource = cJSON_GetObjectItem(jresult, "_source");
    cJSON *jstate = cJSON_GetObjectItem(jsource, "state");

    // Parse the new state from JSON here...
    cJSON *r = cJSON_GetObjectItem(jstate, "a_property");
    if (r != NULL)
      _device_state.a_property = r->valueint;

    cJSON *on = cJSON_GetObjectItem(jstate, "on");
    if (on != NULL)
      _device_state.on = on->valueint;

    // Apply the new state to the hardware
    _update_state();
  }
  else
  {
    ESP_LOGD(TAG, "Error: Something went wrong");
  }
}
```

To publish device state to Kuzzle, you have to build a JSON string representing it.
This can be done using a simplte format string for simple device states, or using JSON library as cJSON that comes with ESP32 SDK.

```c
static const char *light_body_fmt =
    "{ \"a_property\": %d, \"on\": %s}";

static void _publish_light_state()
{
  static char device_state_body[K_DOCUMENT_MAX_SIZE] = {0};

  snprintf(device_state_body,
           K_DOCUMENT_MAX_SIZE,
           light_body_fmt,
           _light_state.a_property,
           _light_state.on ? "true" : "false");
  kuzzle_device_state_pub(device_state_body);
}
```

You can find a more complete how to about `kuzzle-esp32` here: <https://github.com/kuzzleio/kuzzle-how-to>

## More documentation

More detailed documentation can be generated using [doxygen](http://www.doxygen.nl/)

Run the following command in `kuzzle-esp32`:

``` console
$ doxygen
Searching for include files...
Searching for example files...
.
.
finished...
```

The documentation is availlable in `_docs/html/index.html`.