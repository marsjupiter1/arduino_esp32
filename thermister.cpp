#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <math.h>
#include <stdio.h>

const char* ssid = ".....";
const char* password = "....!";

#define VOLTS 3.2
#define RESISTANCEK 10
#define ADC_RANGE 4095.0
#define SMOOTH 20
WebServer server(80);

const int thermistor_output = 35;

void handleRoot() {

  int thermistor_adc_val;
  double output_voltage, thermistor_resistance, therm_res_ln, temperature; 
  char buffer[100];
  thermistor_adc_val = 0;

  for (int i = 0; i < SMOOTH; i++){
    thermistor_adc_val += analogRead(thermistor_output);
     //Serial.print(thermistor_adc_val);
  }
  thermistor_adc_val/= SMOOTH;
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
  Serial.print(thermistor_adc_val);
  //Serial.print("\n\n");
  //sprintf(buffer, "Temperature in degree Celsius = %f, %d, %f %f %f",temperature,thermistor_adc_val ,thermistor_resistance,therm_res_ln,output_voltage);
  sprintf(buffer,"{\"c\":%f}",temperature);
  server.send(200, "text/plain", buffer);

}

void handleNotFound() {
 
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
  
}

void setup(void) {
  
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.println("");

  // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  if (MDNS.begin("esp32")) {
    Serial.println("MDNS responder started");
  }

  server.on("/", handleRoot);

  server.on("/inline", []() {
    server.send(200, "text/plain", "this works as well");
  });

  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("HTTP server started");
}

void loop(void) {
  server.handleClient();
  delay(2);//allow the cpu to switch to other tasks
}
