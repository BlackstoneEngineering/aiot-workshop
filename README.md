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
In Mbed Studio import [https://github.com/BlackstoneEngineering/aiot-workshop](https://github.com/BlackstoneEngineering/aiot-workshop) into your workspace. This process will take a couple of minutes to pull down all the libraries to your local workspace. 

![ImportProgramMbedStudio.gif](./img/ImportProgramMbedStudio.gif)

*NOTE* You may need to open a terminal in the aiot-folder and run `mbed deploy` if the program fails to download. 
