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
#ifdef TARGET_LIKE_MBED
#include "mbed.h"
#endif
#include "application_init.h"
#include "mcc_common_button_and_led.h"
#include "blinky.h"
#ifndef MBED_CONF_MBED_CLOUD_CLIENT_DISABLE_CERTIFICATE_ENROLLMENT
#include "certificate_enrollment_user_cb.h"
#endif

#include "models/models/data.hpp" // uTensor
#include "XNucleoIKS01A3.h"     // ST Sensor Shield
#include "treasure-data-rest.h" // Pelion Data Management

#if defined(MBED_CONF_NANOSTACK_HAL_EVENT_LOOP_USE_MBED_EVENTS) && \
 (MBED_CONF_NANOSTACK_HAL_EVENT_LOOP_USE_MBED_EVENTS == 1) && \
 defined(MBED_CONF_EVENTS_SHARED_DISPATCH_FROM_APPLICATION) && \
 (MBED_CONF_EVENTS_SHARED_DISPATCH_FROM_APPLICATION == 1)
#include "nanostack-event-loop/eventOS_scheduler.h"
#endif

 // Default network interface object. Don't forget to change the WiFi SSID/password in mbed_app.json if you're using WiFi.
NetworkInterface *net = NetworkInterface::get_default_instance();

#define BUFF_SIZE   100 // used by td rest api
TreasureData_RESTAPI* td = new TreasureData_RESTAPI(net,"aiot_workshop_db",MBED_CONF_APP_TABLE_NAME, MBED_CONF_APP_API_KEY);

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
static M2MResource* temperature_res;
static M2MResource* humidity_res;
static M2MResource* pressure_res;

void unregister_received(void);
void unregister(void);

// Pointer to mbedClient, used for calling close function.
static SimpleM2MClient *client;

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
            // unregister();
            break;
        default:
            break;
    }
}

/**
 * Update sensors and report their values.
 * This function is called periodically.
 */
char td_buff     [BUFF_SIZE] = {0};
float temp_value[10];
volatile int temp_index =0;
void sensors_update() {

    temp_index = (temp_index +1)%10; //wrap index
    temp->get_temperature(&temp_value[temp_index]);
    float humidity_value;
    hum_temp->get_humidity(&humidity_value);
    float pressure_value;
    press_temp->get_pressure(&pressure_value);
    
    // if (endpointInfo) {
        printf("temp[%d]:%6.4f,humidity:%6.4f,pressure:%6.4f\r\n",temp_index, temp_value[temp_index], humidity_value, pressure_value);
        // Send data to Pelion Device Management
        // res_temperature->set_value(temp_value);
        // res_voltage->set_value(vref);
        // res_humidity->set_value(humidity_value);
        // res_pressure->set_value(pressure_value);

        // Send data to Treasure Data
        int x = 0;
        x = sprintf(td_buff,"{\"temp\":%f,\"humidity\":%f,\"pressure\":%f}", temp_value[temp_index],humidity_value,pressure_value);
        td_buff[x]=0; //null terminate string
        td->sendData(td_buff,strlen(td_buff));

        // run inference
        Context ctx;
        Tensor* temp_value_tensor = new WrappedRamTensor<float>({1,10}, (float*) &temp_value);    
        get_data_ctx(ctx, temp_value_tensor);
        printf("...Running Eval...");
        ctx.eval();
        printf("finished....");
        S_TENSOR prediction = ctx.get({"dense_3_1/BiasAdd:0"});
        float result = *(prediction->read<float>(0,0));
        printf("\r\nResult is %f\r\n",result);
        if(abs(result - (float)temp_value[temp_index]) > (result/100)*5 ){
            printf("\r\n Its getting hot in here.. thar may be fire afoot!");
        } else{
            // do nothing
        }

    // }
}


void main_application(void)
{
    /* Enable sensors on the shield */
    hum_temp->enable();
    press_temp->enable();
    temp->enable();
    // magnetometer->enable();
    // accelerometer->enable_x();
    // acc_gyro->enable_x();
    // acc_gyro->enable_g();

    // SimpleClient is used for registering and unregistering resources to a server.
    SimpleM2MClient mbedClient;

    // Save pointer to mbedClient so that other functions can access it.
    client = &mbedClient;
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

    // Create resource for button count. Path of this resource will be: 3200/0/5501.
    button_res = mbedClient.add_cloud_resource(3200, 0, 5501, "button_resource", M2MResourceInstance::INTEGER,
                              M2MBase::GET_ALLOWED, 0, true, NULL, (void*)notification_status_callback);
    button_res->set_value(0);

    temperature_res = mbedClient.add_cloud_resource(3303, 0, 5501, "temperature_resource", M2MResourceInstance::FLOAT,
                              M2MBase::GET_ALLOWED, 0, true, NULL, NULL);
    temperature_res->set_value_float(0);
    
    humidity_res = mbedClient.add_cloud_resource(3304, 0, 5501, "humidity_resource", M2MResourceInstance::FLOAT,
                              M2MBase::GET_ALLOWED, 0, true, NULL, NULL);
    humidity_res->set_value_float(0);
    
    pressure_res = mbedClient.add_cloud_resource(3323, 0, 5501, "pressure_resource", M2MResourceInstance::FLOAT,
                              M2MBase::GET_ALLOWED, 0, true, NULL, NULL);
    pressure_res->set_value_float(0);

    // Create resource for running factory reset for the device. Path of this resource will be: 3/0/6.
    M2MInterfaceFactory::create_device()->create_resource(M2MDevice::FactoryReset);
    mbedClient.register_and_connect();
    blinky.init(mbedClient, button_res);
    blinky.request_next_loop_event();

    // Check if client is registering or registered, if true sleep and repeat.
    while (mbedClient.is_register_called()) {
        sensors_update();
        mcc_platform_do_wait(10000);
    }

    // Client unregistered, disconnect and exit program.
    mcc_platform_interface_close();
}
