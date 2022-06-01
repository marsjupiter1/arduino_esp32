[![paypal](https://www.paypalobjects.com/en_US/i/btn/btn_donate_LG.gif)](https://www.paypal.com/cgi-bin/webscr?cmd=_s-xclick&hosted_button_id=28J8RUP2899JE)

A collection of arduino files for the esp32 type board with builtin wifi and bluetooth

thermistor.ino - a web server reporting back on a couple of attached thermistors.

The device can be configured via a web page to control tuya devices for heating and cooling.
This uses the tuya cloud service and hence contains code to authorise and call the tuya cloud.
There is an associated android app that can also control the device.
The app will check with the cloud the switch code to control the attached devices.

The circuit is here albeit on the esp32 board I'm connecting to a 3.2v pin.

https://www.electronicwings.com/arduino/thermistor-interfacing-with-arduino-uno

