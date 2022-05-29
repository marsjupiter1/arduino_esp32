#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <WiFiManager.h>
#include <math.h>
#include <stdio.h>
#include <Arduino.h>
#include <ArduinoJson.h>

#include <TM1637Display.h>
#include <aREST.h>
#include <EEPROM.h>
#define EEPROM_SIZE 100

#include <ezButton.h>

#ifdef TUYA
#include <ArduinoTuya.h>
TuyaDevice plug("01200885ecfabc87b0f9", "fd351bcdb819492f", "192.168.1.159");
#endif
  
#include "SPIFFS.h"

#define BUTTON_PIN 15
ezButton button(BUTTON_PIN);  // create ezButton object that attach to pin 7;

#define CLK 18//blue
#define DIO 19 // green
//    A1
// F2    B3
//    G4
// E5    C4
//    D8
const uint8_t SEG_DONE[] = {
  SEG_B | SEG_C | SEG_D | SEG_E | SEG_G, // d
  SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F, // O
  SEG_C | SEG_E | SEG_G, // n
  SEG_A | SEG_D | SEG_E | SEG_F | SEG_G
}; // E
const uint8_t SEG_CONF[] = {
  SEG_B | SEG_C | SEG_D | SEG_E | SEG_G, // C
  SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F, // O
  SEG_C | SEG_E | SEG_G, // n
  SEG_A | SEG_D | SEG_E | SEG_F //F
};

const uint8_t SEG_CONN[] = {
  SEG_B | SEG_C | SEG_D | SEG_E | SEG_G, // C
  SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F, // O
  SEG_C | SEG_E | SEG_G, // n
  SEG_C | SEG_E | SEG_G, // n
}; // F

const uint8_t SEG_MINUS =  SEG_G; // -
TM1637Display display(CLK, DIO);


uint8_t data[] = {0xff, 0xff, 0xff, 0xff};
uint8_t blank[] = {0x00, 0x00, 0x00, 0x00};


TaskHandle_t Task1;

#define VOLTS 3.2
#define RESISTANCEK 10
#define ADC_RANGE 4095.0
#define SMOOTH 20
WebServer server(80);

WiFiManager wifiManager;

const int thermistor_output1 = 35;
const int thermistor_output2 = 32;

float tmin,tmax;
char tuya_device[40];
char tuya_key[40];
char device_name[20];


double getThermistorReading(int pin) {
  int thermistor_adc_val;
  double output_voltage, thermistor_resistance, therm_res_ln, temperature;
  char buffer[100];
  thermistor_adc_val = 0;

  for (int i = 0; i < SMOOTH; i++) {
    thermistor_adc_val += analogRead(pin);
    //Serial.print(thermistor_adc_val);
  }
  thermistor_adc_val /= SMOOTH;
  output_voltage = ( (thermistor_adc_val * VOLTS) / ADC_RANGE );
  thermistor_resistance = ( (VOLTS * ( RESISTANCEK / output_voltage ) ) - RESISTANCEK ); /* Resistance in kilo ohms */
  thermistor_resistance = thermistor_resistance * 1000 ; /* Resistance in ohms   */
  therm_res_ln = log(thermistor_resistance);
  /*  Steinhart-Hart Thermistor Equation: */
  /*  Temperature in Kelvin = 1 / (A + B[ln(R)] + C[ln(R)]^3)   */
  /*  where A = 0.001129148, B = 0.000234125 and C = 8.76741*10^-8  */
  temperature = ( 1 / ( 0.001129148 + ( 0.000234125 * therm_res_ln ) + ( 0.0000000876741 * therm_res_ln * therm_res_ln * therm_res_ln ) ) ); /* Temperature in Kelvin */
  temperature = temperature - 273.15; /* Temperature in degree Celsius */
  //Serial.print("Temperature in degree Celsius = ");
  //Serial.print(temperature);
  //Serial.print("\t\t");
  //Serial.print("Resistance in ohms = ");
  //.print(thermistor_adc_val);
  //Serial.print("\n\n");
  //sprintf(buffer, "Temperature in degree Celsius = %f, %d, %f %f %f",temperature,thermistor_adc_val ,thermistor_resistance,therm_res_ln,output_voltage);
  return temperature;
}


void handleRoot() {
  char buffer[60];
  double temperature1 = getThermistorReading(thermistor_output1);
  double temperature2 = getThermistorReading(thermistor_output2);
  double temperature = (temperature1 + temperature2) / 2;
  sprintf(buffer, "{\"c\":%f,\"low\":%f,\"high\":%f}", temperature, temperature1 < temperature2 ? temperature1 : temperature2, temperature1 > temperature2 ? temperature1 : temperature2);
  server.send(200, "text/plain", buffer);

}

void handleSetting() {

    if (server.hasArg("min")){
        tmin = strtof(server.arg("min").c_str(),NULL);
    }
    if (server.hasArg("max")){
        tmax = strtof(server.arg("max").c_str(),NULL);
    }
    if (server.hasArg("tuya_key") ){
        const  char *tuya_key_arg = server.arg("tuya_key").c_str();
        if (strlen(tuya_key_arg)==18){
             strcpy(tuya_key,tuya_key_arg);
        }else{
           server.send(400, "text/plain", "bad tuya local key");
        }
       
    }

       if (server.hasArg("tuya_device") ){
         const char *tuya_device_arg = server.arg("tuya_device").c_str();
        if (strlen(tuya_device_arg)==20){
             strcpy(tuya_device,tuya_device_arg);
        }else{
          
          server.send(400, "text/plain", "bad tuya device id");
        }
       
    }
    char buffer[400];
    sprintf(buffer,"{\"min\":%f,\"max\":%f,\"tuya_device\",\"%s\",\"tuya_key\",\"%s\"",tmin,tmax,tuya_device,tuya_key);

    File file = SPIFFS.open("/settings.txt",FILE_WRITE);
    int l=strlen(buffer);
   for(int i =0; i<= l;i++){
    file.write(buffer[i]); 
   }
   file.close();

   server.send(200, "text/plain", "success");
}


void Task1code( void * parameter) {



  for (;;) {
    double temperature1 = getThermistorReading(thermistor_output1);
    double temperature2 = getThermistorReading(thermistor_output2);
    double temperature = (temperature1 + temperature2) / 2;

    char buffer[60];
    sprintf(buffer, "%2.2f %2.2f %2.2f", temperature,temperature1,temperature2);
    Serial.println(buffer);
    if (temperature < -10) {
      data[0] = SEG_MINUS;
      data[1] = display.encodeDigit(buffer[1]);
      data[2] = display.encodeDigit(buffer[2]);
      data[3] = display.encodeDigit(buffer[3]);
      display.setSegments(data);
    } else if (temperature < 0) {
      data[0] = SEG_MINUS;
      data[1] = display.encodeDigit(buffer[1]);
      data[2] = display.encodeDigit(buffer[2]);
      data[3] = display.encodeDigit(buffer[4]);
      display.setSegments(data);
    } else if (temperature < 10) {
      data[0] = 0;
      data[1] = display.encodeDigit(buffer[0]);
      data[2] = SEG_D;
      data[3] = display.encodeDigit(buffer[2]);
      display.setSegments(data);
    } else {
      int temp = temperature * 100;

      display.showNumberDecEx(temp, 0b01000000);
      /*data[0] = display.encodeDigit(buffer[0]);
        data[1] = display.encodeDigit(buffer[1])|128;

        data[2] = display.encodeDigit(buffer[3]);
        data[3] = display.encodeDigit(buffer[4]);
        display.setSegments(data);*/
    }


    delay(1000);
  }

}




void setup(void) {
  Serial.begin(115200);

  if(!SPIFFS.begin(true)){
     Serial.println("An Error has occurred while mounting SPIFFS");
     return;
}

  tmin = 24;
  tmax = 40;
  tuya_key[0] = '\0';
  tuya_device[0] = '\0';

  File file = SPIFFS.open("/settings.txt");
  
  if(file){
    int pos = 0;
    char buffer[500];
    while(file.available()){
      buffer[pos++]= file.read();
    }
    file.close();
    DynamicJsonDocument root(1024);
   deserializeJson(root,buffer);
    
    tmin = root["min"];
    tmax = root["max"];
    strcpy(tuya_device,root["tuya_device"]);
    strcpy(tuya_key,root["tuya_key"]);
    
  }

  pinMode(BUTTON_PIN, INPUT_PULLUP); // config GIOP21 as input pin and enable the internal pull-up resistor

  display.setBrightness(0x0f);

  EEPROM.begin(EEPROM_SIZE);
  init_eeprom();
  EEPROM.commit();
  readId(device_name);
  EEPROM.end();


  if (strcmp(device_name, "") == 0) {
    Serial.print("reset blank device name");
    EEPROM.begin(EEPROM_SIZE);
    writeId("thermistor");
    EEPROM.commit();
    EEPROM.end();
  }
  Serial.println(device_name);

  WiFiManagerParameter MDNSName("device_name", "Device Name", device_name, 19);
  wifiManager.addParameter(&MDNSName);
  display.setSegments(SEG_CONN);
  wifiManager.setEnableConfigPortal(false);
  if (!wifiManager.autoConnect("AutoConnectAP", "password")) {
    display.setSegments(SEG_CONF);
    wifiManager.startConfigPortal("ConfigureWiFi");
    const char *new_name = MDNSName.getValue();
    if (strcmp(new_name, device_name) != 0) {
      EEPROM.begin(EEPROM_SIZE);
      writeId(new_name);
      EEPROM.commit();
      EEPROM.end();
      strcpy(device_name, new_name);
    }
  }
  display.setSegments(SEG_DONE);

  xTaskCreatePinnedToCore(
    Task1code,   /* Task function. */
    "Task1",     /* name of task. */
    2000,       /* Stack size of task */
    NULL,        /* parameter of the task */
    1,           /* priority of the task */
    &Task1,      /* Task handle to keep track of created task */
    0);          /* pin task to core 0 */

  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  /*if (MDNS.begin(device_name)) {
    Serial.println("MDNS responder started");
    }*/

  server.on("/", handleRoot);
  server.on("/setting", handleSetting);

  server.begin();
  Serial.println("HTTP server started");
}

void loop(void) {
  static int counter = 0;
  button.loop(); // MUST call the loop() function first
  server.handleClient();
 
  //int btnState = digitalRead(BUTTON_PIN);
  if (button.isPressed()) {
    counter++;
    display.showNumberDecEx(counter);
  }
  /*if(btnState== LOW){
      counter++;
      display.showNumberDecEx(counter);
    }*/
  delay(20);//allow the cpu to switch to other tasks
  //int btnState = button.getState();
  //Serial.println(btnState);

}

void readId(char *buffer) {
  for (int i = 0; i <= 20; i++) {

    buffer[i] = EEPROM.read(i);
  }
}

void writeId(const char *id) {
  for (int i = 0; i <= strlen(id); i++) {
    EEPROM.write(i, id[i]);
  }
  unsigned long calculatedCrc = eeprom_crc();
  EEPROM.put(EEPROM.length() - 4, calculatedCrc);

}

void init_eeprom() {
  unsigned long calculatedCrc = eeprom_crc();

  Serial.println("check eeprom crc");
  // get stored crc
  unsigned long storedCrc;
  EEPROM.get(EEPROM.length() - 4, storedCrc);

  if (storedCrc != calculatedCrc) {
    Serial.println("initialise eeprom");
    writeId("thermistor");
  }
  Serial.println("init complete");
}

unsigned long eeprom_crc(void)
{

  const unsigned long crc_table[16] =
  {
    0x00000000, 0x1db71064, 0x3b6e20c8, 0x26d930ac,
    0x76dc4190, 0x6b6b51f4, 0x4db26158, 0x5005713c,
    0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c,
    0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c
  };

  unsigned long crc = ~0L;

  for (int index = 0 ; index < EEPROM.length() - 4  ; ++index)
  {
    byte b = EEPROM.read(index);
    crc = crc_table[(crc ^ b) & 0x0f] ^ (crc >> 4);
    crc = crc_table[(crc ^ (b >> 4)) & 0x0f] ^ (crc >> 4);
    crc = ~crc;
  }
  return crc;
}
