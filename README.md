webinos-iot-driver-arduino
===================

###How to use HTTP driver with Arduino Mega board

1) Go to webinos-driver-arduino folder, and do 'npm install express'.

2) Fash your mega with the driver *platform/arduino_mega/api/sensors_actuator/http_driver/http_driver.ino*

3) Copy and edit the file *platform/arduino_mega/api/sensors_actuator/http_driver/config.txt* in the root folder of arduino SD card.

4) Connect arduino to the PZP through ethernet cable.

P.S.
Be sure that your PZP's IP is the same as the one in config.txt. You can refer to the [documentation page](https://github.com/webinos/webinos-iot-driver-arduino/blob/master/platform/arduino_mega/api/sensors_actuator/http_driver/docs/sensors_actuators_driver.md) in order to set the config.txt file.




