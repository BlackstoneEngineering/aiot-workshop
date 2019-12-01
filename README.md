# Arm AIoT Developer Summit : Buidling End to End Machine Learning Workshop
This workshop covers how to do full lifecycle management of an IoT Edge device that uses machine learning and the Arm Pelion ecosystem. We will cover how to send data from your device running Mbed OS to the Arm Pelion Data Management service, how to then create a model in the cloud, and then deploy that model back to the device. 

TODO: Insert architecture diagram

At the end of this workshop you will have a Cortex M IoT device running machine learning at the edge and using that model to determine what data to send to the cloud. 

## Pre-Requisites
1) [Create a Mbed Compiler account](https://os.mbed.com/account/login/?next=/)
1) [Login to a Pelion Dashboard](https://portal.mbedcloud.com/federated-login?issuer=https%3A%2F%2Faccount.mbed.com%2F&next=%2F)  account using Mbed Compiler Account from previous step
1) Install [Mbed Studio](https://os.mbed.com/studio/) on your machine
1) Give your email to your instructor to add you to the Arm Data Management account. 


## Section 1: Device to Cloud
In the first workshop session we will connect our device running Mbed OS to the Pelion Data Management service and the Pelion Device Management service. Pelion Data will allow us to store data and then process it later into a ML model, Pelion Device management will allow us to view real time data on the device as well as issue commands and firmware updates to the device. 


### Import and configure device code

#### Pelion Device Management
In Mbed Studio import [https://github.com/BlackstoneEngineering/aiot-workshop](https://github.com/BlackstoneEngineering/aiot-workshop) into your workspace. This process will take a couple of minutes to pull down all the libraries to your local workspace. 

![](./img/ImportProgramMbedStudio.gif)

*NOTE* You may need to open a terminal in the aiot-folder and run `mbed deploy` if the program fails to download or compile. 

Next you will need to get your Pelion Device Management API key from the Pelion Portal : [https://portal.mbedcloud.com/access/keys/list](https://portal.mbedcloud.com/access/keys/list). Click `New API Key` in the top right to create a key. You will need it for the next step. 

![](peliondm_create_apikey.gif)


Next you will need to add Pelion Device Management certificates for both connectivity and update to your project. You can do this easily from the left hand nav bar. 

![](./img/add_certificates.gif)

#### Pelion Data Management (Treasure Data)

Next we are going to add a Pelion Data Management (Treasure Data) [API key](https://console.treasuredata.com/app/mp/ak/) and a custom table name to `mbed_app.json`. This will allow the device to send data to treasure data and store it into the specified table. (Note, the database is hard coded in `main.cpp` for reasons that will become apparent later). Everyone needs a unique table name, so I reccomend using your last name, or your first_last name as the table name.

Grab TD API key from here : [https://console.treasuredata.com/app/mp/ak/](https://console.treasuredata.com/app/mp/ak/) 

```mbed_app.json
"api-key":{
            "help":  "REST API Key for Treasure Data",
            "value": "\"CHANGE_ME\""
        },
"table-name":{
            "help":  "name of table in Treasure Data to send data to",
            "value": "\"CHANGE_ME\""
        }
```

#### Compile the code

Now that you have setup all the configuration parameters you can compile and load the code onto your board. Click on the `Build` and then the `run` icons in Mbed Studio. This may take a minute or two to compile, when the board is being programmed you will see the LED flash, this is a good sign, wait for it to stop flashing and then move on to check the serial output!


#### Check Serial Output
Mbed Studio has a handy Serial Terminal built right into it, you can simply go to `View->Serial Monitor` and you should be ablet o see the data coming out of your board. Click the blue button on the board to see direct output, or try clicking the black button to reset the board and watch it boot up!

Success looks like this: 

```
Mbed Bootloader
[DBG ] Active firmware up-to-date
booting...

 Application Version 2
Start Device Management Client
Starting developer flow
Connecting with interface: Ethernet
NSAPI_STATUS_CONNECTING
NSAPI_STATUS_GLOBAL_UP
IP: 192.168.1.250
Network initialized, registering...

Client registered
Endpoint Name: 016e56566fb0000000000001001d6d5e
Device Id: 016e56566fb0000000000001001d6d5e
temp:28.6875,humidity:45.2000,pressure:993.3853

```


#### View Data in Pelion Device Management
Once your board has connected you can view it in the [Pelion Dashboard](https://portal.mbedcloud.com/devices/list). Try clicking through the resources on your device to see the temperature, humidity, pressure, and button clicks in real time from the board!

#### Check Treasure Data Database


### Compile ML Models based on Data
