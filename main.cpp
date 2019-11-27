// ----------------------------------------------------------------------------
// Copyright 2016-2019 ARM Ltd.
//
// SPDX-License-Identifier: Apache-2.0
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
// ----------------------------------------------------------------------------

#include "simplem2mclient.h"
#include "SensorQueue.hpp"
#ifdef TARGET_LIKE_MBED
#include "mbed.h"
#endif
#include "application_init.h"
#include "mcc_common_button_and_led.h"
#include "blinky.h"
#ifndef MBED_CONF_MBED_CLOUD_CLIENT_DISABLE_CERTIFICATE_ENROLLMENT
#include "certificate_enrollment_user_cb.h"
#endif

#include "XNucleoIKS01A3.h"     // ST Sensor Shield
#include "treasure-data-rest.h" // Pelion Data Management
#include "models/models/workshop_model.hpp" // uTensor

#if defined(MBED_CONF_NANOSTACK_HAL_EVENT_LOOP_USE_MBED_EVENTS) && \
 (MBED_CONF_NANOSTACK_HAL_EVENT_LOOP_USE_MBED_EVENTS == 1) && \
 defined(MBED_CONF_EVENTS_SHARED_DISPATCH_FROM_APPLICATION) && \
 (MBED_CONF_EVENTS_SHARED_DISPATCH_FROM_APPLICATION == 1)
#include "nanostack-event-loop/eventOS_scheduler.h"
#endif

 // Default network interface object. Don't forget to change the WiFi SSID/password in mbed_app.json if you're using WiFi.
NetworkInterface *net = NetworkInterface::get_default_instance();

#define BUFF_SIZE   100 // used by td rest api
TreasureData_RESTAPI* td = new TreasureData_RESTAPI(net,"aiot_workshop_db","data", MBED_CONF_APP_API_KEY);

/* Instantiate the expansion board */
static XNucleoIKS01A3 *mems_expansion_board = XNucleoIKS01A3::instance(D14, D15, D4, D5, A3, D6, A4);
 
/* Retrieve the composing elements of the expansion board */
static STTS751Sensor *temp = mems_expansion_board->t_sensor;
static HTS221Sensor *hum_temp = mems_expansion_board->ht_sensor;
static LPS22HHSensor *press_temp = mems_expansion_board->pt_sensor;
// static LSM6DSOSensor *acc_gyro = mems_expansion_board->acc_gyro;
// static LIS2MDLSensor *magnetometer = mems_expansion_board->magnetometer;
// static LIS2DW12Sensor *accelerometer = mems_expansion_board->accelerometer;

// event based LED blinker, controlled via pattern_resource
#ifndef MCC_MINIMAL
static Blinky blinky;
#endif

static void main_application(void);

#if defined(MBED_CLOUD_APPLICATION_NONSTANDARD_ENTRYPOINT)
extern "C"
int mbed_cloud_application_entrypoint(void)
#else
int main(void)
#endif
{
    return mcc_platform_run_program(main_application);
}


// Pointers to the resources that will be created in main_application().
static M2MResource* button_res;
static M2MResource* pattern_res;
static M2MResource* blink_res;
static M2MResource* unregister_res;

void unregister_received(void);
void unregister(void);

// Pointer to mbedClient, used for calling close function.
static SimpleM2MClient *client;

void pattern_updated(const char *)
{
    printf("PUT received, new value: %s\n", pattern_res->get_value_string().c_str());
}

void blink_callback(void *)
{
    String pattern_string = pattern_res->get_value_string();
    const char *pattern = pattern_string.c_str();
    printf("LED pattern = %s\n", pattern);

    // The pattern is something like 500:200:500, so parse that.
    // LED blinking is done while parsing.
#ifndef MCC_MINIMAL
    const bool restart_pattern = false;
    if (blinky.start((char*)pattern_res->value(), pattern_res->value_length(), restart_pattern) == false) {
        printf("out of memory error\n");
    }
#endif
    blink_res->send_delayed_post_response();
}

void notification_status_callback(const M2MBase& object,
                            const M2MBase::MessageDeliveryStatus status,
                            const M2MBase::MessageType /*type*/)
{
    switch(status) {
        case M2MBase::MESSAGE_STATUS_BUILD_ERROR:
            printf("Message status callback: (%s) error when building CoAP message\n", object.uri_path());
            break;
        case M2MBase::MESSAGE_STATUS_RESEND_QUEUE_FULL:
            printf("Message status callback: (%s) CoAP resend queue full\n", object.uri_path());
            break;
        case M2MBase::MESSAGE_STATUS_SENT:
            printf("Message status callback: (%s) Message sent to server\n", object.uri_path());
            break;
        case M2MBase::MESSAGE_STATUS_DELIVERED:
            printf("Message status callback: (%s) Message delivered\n", object.uri_path());
            break;
        case M2MBase::MESSAGE_STATUS_SEND_FAILED:
            printf("Message status callback: (%s) Message sending failed\n", object.uri_path());
            break;
        case M2MBase::MESSAGE_STATUS_SUBSCRIBED:
            printf("Message status callback: (%s) subscribed\n", object.uri_path());
            break;
        case M2MBase::MESSAGE_STATUS_UNSUBSCRIBED:
            printf("Message status callback: (%s) subscription removed\n", object.uri_path());
            break;
        case M2MBase::MESSAGE_STATUS_REJECTED:
            printf("Message status callback: (%s) server has rejected the message\n", object.uri_path());
            break;
        default:
            break;
    }
}

void sent_callback(const M2MBase& base,
                   const M2MBase::MessageDeliveryStatus status,
                   const M2MBase::MessageType /*type*/)
{
    switch(status) {
        case M2MBase::MESSAGE_STATUS_DELIVERED:
            unregister();
            break;
        default:
            break;
    }
}

void unregister_triggered(void)
{
    printf("Unregister resource triggered\n");
    unregister_res->send_delayed_post_response();
}

// This function is called when a POST request is received for resource 5000/0/1.
void unregister(void)
{
    printf("Unregister resource executed\n");
    client->close();
}

// SensorQueue
// Each window of input consists of 10 samples
// Overlaps 5 samples at a time
// Caching max of 2 overlaping segments at a time
SensorQueue<float> temp_input_pool(10, 5, 2);

void inference(void) {
    Context ctx;
    static float temp_values[10];
    temp_input_pool.copyTo(temp_values);
    Tensor* temp_value_tensor = new WrappedRamTensor<float>({0,10}, (float*) &temp_values);
    // run inference
    get_workshop_model_ctx(ctx, temp_value_tensor);
    printf("...Running Eval...");
    ctx.eval();
    printf("finished....");
    S_TENSOR prediction = ctx.get({"dense_3/BiasAdd:0"});
    float result = *(prediction->read<float>(0,0));
    printf("\r\nResult is %f\r\n", result);
}

/**
 * Update sensors and report their values.
 * This function is called periodically.
 */


char td_buff     [BUFF_SIZE] = {0};
float temp_value;

void sensors_update() {
    temp->get_temperature(&temp_value);
    float humidity_value;
    hum_temp->get_humidity(&humidity_value);
    float pressure_value;
    press_temp->get_pressure(&pressure_value);
    
    // if (endpointInfo) {
        printf("temp:%6.4f,humidity:%6.4f,pressure:%6.4f\r\n", temp_value, humidity_value, pressure_value);
        // Send data to Pelion Device Management
        // res_temperature->set_value(temp_value);
        // res_voltage->set_value(vref);
        // res_humidity->set_value(humidity_value);
        // res_pressure->set_value(pressure_value);

        // Send data to Treasure Data
        int x = 0;
        x = sprintf(td_buff,"{\"temp\":%f,\"humidity\":%f,\"pressure\":%f}", temp_value,humidity_value,pressure_value);
        td_buff[x]=0; //null terminate string
        td->sendData(td_buff,strlen(td_buff));

        temp_input_pool.append(temp_value);

    // }
}


void main_application(void)
{

    /* Enable all sensors */
    hum_temp->enable();
    press_temp->enable();
    temp->enable();
    temp_input_pool.setCallBack(inference);
    // magnetometer->enable();
    // accelerometer->enable_x();
    // acc_gyro->enable_x();
    // acc_gyro->enable_g();
#if defined(__linux__) && (MBED_CONF_MBED_TRACE_ENABLE == 0)
        // make sure the line buffering is on as non-trace builds do
        // not produce enough output to fill the buffer
        setlinebuf(stdout);
#endif

    // Initialize trace-library first
    if (application_init_mbed_trace() != 0) {
        printf("Failed initializing mbed trace\n" );
        return;
    }

    // Initialize storage
    if (mcc_platform_storage_init() != 0) {
        printf("Failed to initialize storage\n" );
        return;
    }

    // Initialize platform-specific components
    if(mcc_platform_init() != 0) {
        printf("ERROR - platform_init() failed!\n");
        return;
    }

    // Print some statistics of the object sizes and their heap memory consumption.
    // NOTE: This *must* be done before creating MbedCloudClient, as the statistic calculation
    // creates and deletes M2MSecurity and M2MDevice singleton objects, which are also used by
    // the MbedCloudClient.

printf("\r\n Application Version 2\r\n");

#ifdef MBED_HEAP_STATS_ENABLED
    print_m2mobject_stats();
#endif

    // SimpleClient is used for registering and unregistering resources to a server.
    SimpleM2MClient mbedClient;

    // Save pointer to mbedClient so that other functions can access it.
    client = &mbedClient;

    /*
     * Pre-initialize network stack and client library.
     *
     * Specifically for nanostack mesh networks on Mbed OS platform it is important to initialize
     * the components in correct order to avoid out-of-memory issues in Device Management Client initialization.
     * The order for these use cases should be:
     * 1. Initialize network stack using `nsapi_create_stack()` (Mbed OS only). // Implemented in `mcc_platform_interface_init()`.
     * 2. Initialize Device Management Client using `init()`.                   // Implemented in `mbedClient.init()`.
     * 3. Connect to network interface using 'connect()`.                       // Implemented in `mcc_platform_interface_connect()`.
     * 4. Connect Device Management Client to service using `setup()`.          // Implemented in `mbedClient.register_and_connect)`.
     */
    (void) mcc_platform_interface_init();
    mbedClient.init();

    // application_init() runs the following initializations:
    //  1. platform initialization
    //  2. print memory statistics if MBED_HEAP_STATS_ENABLED is defined
    //  3. FCC initialization.
    if (!application_init()) {
        printf("Initialization failed, exiting application!\n");
        return;
    }

    // Print platform information
    mcc_platform_sw_build_info();

    // Initialize network
    if (!mcc_platform_interface_connect()) {
        printf("Network initialized, registering...\n");
    } else {
        return;
    }

#ifdef MBED_HEAP_STATS_ENABLED
    printf("Client initialized\r\n");
    print_heap_stats();
#endif
#ifdef MBED_STACK_STATS_ENABLED
    print_stack_statistics();
#endif

#ifndef MCC_MEMORY
    // Create resource for button count. Path of this resource will be: 3200/0/5501.
    button_res = mbedClient.add_cloud_resource(3200, 0, 5501, "button_resource", M2MResourceInstance::INTEGER,
                              M2MBase::GET_ALLOWED, 0, true, NULL, (void*)notification_status_callback);
    button_res->set_value(0);

    // Create resource for led blinking pattern. Path of this resource will be: 3201/0/5853.
    pattern_res = mbedClient.add_cloud_resource(3201, 0, 5853, "pattern_resource", M2MResourceInstance::STRING,
                               M2MBase::GET_PUT_ALLOWED, "500:500:500:500", true, (void*)pattern_updated, (void*)notification_status_callback);

    // Create resource for starting the led blinking. Path of this resource will be: 3201/0/5850.
    blink_res = mbedClient.add_cloud_resource(3201, 0, 5850, "blink_resource", M2MResourceInstance::STRING,
                             M2MBase::POST_ALLOWED, "", false, (void*)blink_callback, (void*)notification_status_callback);
    // Use delayed response
    blink_res->set_delayed_response(true);

    // Create resource for unregistering the device. Path of this resource will be: 5000/0/1.
    unregister_res = mbedClient.add_cloud_resource(5000, 0, 1, "unregister", M2MResourceInstance::STRING,
                 M2MBase::POST_ALLOWED, NULL, false, (void*)unregister_triggered, (void*)sent_callback);
    unregister_res->set_delayed_response(true);

    // Create resource for running factory reset for the device. Path of this resource will be: 3/0/6.
    M2MInterfaceFactory::create_device()->create_resource(M2MDevice::FactoryReset);

#endif

// For high-latency networks with limited total bandwidth combined with large number
// of endpoints, it helps to stabilize the network when Device Management Client has
// delayed registration to Device Management after the network formation.
// This is applicable in large Wi-SUN networks.
#if defined(STARTUP_MAX_RANDOM_DELAY) && (STARTUP_MAX_RANDOM_DELAY > 0)
    wait_application_startup_delay();
#endif

    mbedClient.register_and_connect();

#ifndef MCC_MINIMAL
    blinky.init(mbedClient, button_res);
    blinky.request_next_loop_event();
#endif


#ifndef MBED_CONF_MBED_CLOUD_CLIENT_DISABLE_CERTIFICATE_ENROLLMENT
    // Add certificate renewal callback
    mbedClient.get_cloud_client().on_certificate_renewal(certificate_renewal_cb);
#endif // MBED_CONF_MBED_CLOUD_CLIENT_DISABLE_CERTIFICATE_ENROLLMENT

#if defined(MBED_CONF_NANOSTACK_HAL_EVENT_LOOP_USE_MBED_EVENTS) && \
 (MBED_CONF_NANOSTACK_HAL_EVENT_LOOP_USE_MBED_EVENTS == 1) && \
 defined(MBED_CONF_EVENTS_SHARED_DISPATCH_FROM_APPLICATION) && \
 (MBED_CONF_EVENTS_SHARED_DISPATCH_FROM_APPLICATION == 1)
    printf("Starting mbed eventloop...\n");

    eventOS_scheduler_mutex_wait();

    EventQueue *queue = mbed::mbed_event_queue();
    queue.call_every(10000, sensors_update);
    queue->dispatch_forever();
#else
    printf("\r\nThis is so dumb...\r\n");
    // Check if client is registering or registered, if true sleep and repeat.
    while (mbedClient.is_register_called()) {
        sensors_update();
        mcc_platform_do_wait(10000);
    }

    // Client unregistered, disconnect and exit program.
    mcc_platform_interface_close();
#endif
}
