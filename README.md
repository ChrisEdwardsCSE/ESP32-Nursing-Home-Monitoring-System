# ESP32 Nursing Home Monitoring System

[Demo Video](https://youtu.be/p48WhBOY-2Q)

Hi! This project uses ESP32 microcontrollers and the ESP-IDF framework to create an embedded Nursing Home Monitoring System.

Each resident wears a low-power **Device** that monitors their heart rate and detects any physical falls. Resident data is sent via BLE to nearby **Stations** which then serve a webpage monitored by the nursing staff via MQTT over WebSockets. 

Data indicating normal health is periodically published to the webpage. If a Device ever detects an emergency, it transmits an alert in real-time so that the nursing staff are notified as quickly as possible.

### Devices
The Device is comprised of an ESP32 microcontroller, an ADXL345 Accelerometer, and a MAX30102 Pulse Oximeter. The MCU communicates with the peripherals serially via I2C and custom drivers that I wrote.\
It uses BLE to broadcast advertisements containing resident data.\
The Device is implemented using an RTOS (FreeRTOS) to robustly control the scheduling of data sampling and BLE broadcasting.

### Stations
The Station is a BLE and WiFi-enabled ESP32 microcontroller. It continuously scans for BLE advertisements from nearby Devices.\
When normal, healthy data is received the ESP32 stores it along with other residents' data. Periodically, it fowards this data to a webpage using MQTT messaging over WebSockets.\
When data indicating an abnormal heart rate or a potential fall are detected, the Station immediately alerts the nurse-monitored webpage.\

Achievements
- Emergency detection has a worst case response latency of 2 seconds assuming available stable WiFi
- Stations can theoretically handle hundreds of incoming Device BLE signals at a time
- Stations can service 5 simultaneous emergencies in less than 6 seconds on average


Going forward, I would like to add more peripherals to the Device, i.e. Oxygen Level sensor, temperatuer sensor, GPS.
I would also like to pick a more low-power microcontroller to serve as the Device. Lastly, I think the flow of the RTOS could be smoother and more rigorously tested, so I would like to work on that as well.