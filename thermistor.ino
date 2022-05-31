#include <WiFi.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <WiFiManager.h>
#include <math.h>
#include <stdio.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <exception>
#include <vector>

#include <TM1637Display.h>
#include <aREST.h>
#include <EEPROM.h>
#define EEPROM_SIZE 100

#include <ezButton.h>
#define TUYA
#ifdef TUYA
//#include <esp_hmac.h>
#include <hmac_sha256.h>
//#include <TuyaWifi.h>
//#include <TuyaLink.h>
#include <ArduinoTuya.h>
//TuyaWifi plug;
TuyaDevice *plugh;
TuyaDevice *plugc;

#endif
#include "time.h"
#include "SPIFFS.h"

const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 0;
const int   daylightOffset_sec = 3600;


#define BUTTON_PIN 15
ezButton button(BUTTON_PIN);  // create ezButton object that attach to pin 7;

#define CLK 18 //blue
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

#define SHA256_HASH_SIZE 32

extern size_t hmac_sha256_mdw(const void* key,
                              const size_t keylen,
                              const void* data,
                              const size_t datalen,
                              void* out,
                              const size_t outlen);

extern  void *sha256_mdw(const void* data,
                         const size_t datalen,
                         void* out,
                         const size_t outlen);




class TuyaAuth {
    static void auth(void *parameter) {

      TuyaAuth *This = (TuyaAuth *)parameter;

      HTTPClient http;
      static time_t lastauth = 0;
      static time_t expires_in;
      for (;;) {

        char ts[14];
        time_t t = time(NULL);
        if (t < 99999999) {

          Serial.print("*");
          delay(5000);
          continue;
        }
        if (lastauth == 0 || t >= lastauth + expires_in) {
          char url[200];
          char ts[14];
          TuyaAuth::timestamp(ts);
          char sign[400];
#define QUERY "/v1.0/token?grant_type=1"
          sprintf(url, "%s%s", This->host, QUERY );
          //Serial.println(url);
          This->getRequestAuth(ts, This->client_id, This->secret_key, QUERY, "GET", "", sign);
          //Serial.println("Sign String");
          //Serial.println(sign);
          //Serial.println(This->client_id);
          http.begin(url);


          //client.println("Host: " + String(tuya_host));
          http.addHeader(String("t") , String(ts));
          http.addHeader(String("sign_method"), String("HMAC-SHA256"));
          http.addHeader(String("client_id"), String(This->client_id));
          http.addHeader(String("sign") , String(sign));
          int err = http.GET();


          String body = http.getString();

          if (err == 200) {
            DynamicJsonDocument root(5000);

            DeserializationError err = deserializeJson(root, body.c_str());

            if (err) {
              Serial.println("auth error");
              Serial.println(body);
            } else {
              if (root.containsKey("result")) {
                Serial.println(body);
                Serial.println("Authorised");
                strcpy(This->tuya_token, root["result"]["access_token"]);
                lastauth = t;
                expires_in = root["result"]["expire_time"];
              } else {
                Serial.println("missing result");
                Serial.println(body);
              }
            }
          } else {
            Serial.println("Failed to connect");
            Serial.println(err);
            Serial.println(body);
          }
        }

        delay(1000);
      }
    }

    String join(std::vector<String> &v, String delimiter) {
      static String j;
      for (int i = 0; i < v.size(); i++)
      {
        j +=   v[i] + delimiter;

      }
      return j.substring(0, j.length() - 1);
    }
    std::vector<String> split (String s, String delimiter) {
      size_t pos_start = 0, pos_end, delim_len = delimiter.length();
      String token;
      std::vector<String> res;

      while ((pos_end = s.indexOf(delimiter, pos_start)) != -1) {
        token = s.substring(pos_start, pos_end - pos_start);
        pos_start = pos_end + delim_len;
        res.push_back (token);
      }

      res.push_back (s.substring(pos_start));
      return res;
    }
  public:
    TaskHandle_t Task;
    char tuya_token[100];
    char client_id[33];
    char secret_key[33];
    char host[100];




    TuyaAuth(char *h, char *c, char *secret) {

      strcpy(client_id, c);
      strcpy(secret_key, secret);
      strcpy(host, h);
      tuya_token[0] = '\0';
      xTaskCreatePinnedToCore(
        this->auth,   /* Task function. */
        "Auth",     /* name of task. */
        10000,       /* Stack size of task */
        this,        /* parameter of the task */
        1,           /* priority of the task */
        &Task,      /* Task handle to keep track of created task */
        0);          /* pin task to core 0 */

    }

    void  hmac(const char * str_key, const char *str_data, char *out_str) {
      uint8_t out[SHA256_HASH_SIZE];


      hmac_sha256_mdw(str_key, strlen(str_key), str_data, strlen(str_data), &out,
                      sizeof(out));
      // Convert `out` to string with printf

      for (int i = 0; i < sizeof(out); i++) {
        snprintf(&out_str[i * 2], 3, "%02x", out[i]);
        out_str[i * 2] = toupper(out_str[i * 2]);
        out_str[i * 2 + 1] = toupper(out_str[i * 2 + 1]);
      }
      out_str[2 * SHA256_HASH_SIZE] = '\0';




    }
#if 1
    void _sha256(String key, char *out) {

      char buffer[SHA256_HASH_SIZE + 1];
      sha256_mdw((void *)key.c_str(), (size_t)key.length(), (void *)buffer, (size_t)SHA256_HASH_SIZE);
      for (int i = 0; i < SHA256_HASH_SIZE; i++) {
        snprintf(&out[i * 2], 3, "%02x", buffer[i]);

      }
      out[2 * SHA256_HASH_SIZE] = '\0';
    }
#else
    void _sha256(String key, char *out) {
      SHA256 sha;
      char buffer[SHA256_HASH_SIZE + 1];
      sha.update((void *)key.c_str(), (size_t)key.length());
      sha.finalize(buffer, SHA256_HASH_SIZE);
      for (int i = 0; i < SHA256_HASH_SIZE; i++) {
        snprintf(&out[i * 2], 3, "%02x", buffer[i]);

      }
      out[2 * SHA256_HASH_SIZE] = '\0';
    }
#endif

    static void timestamp(char *out) {
      time_t now = time(NULL);
   
      sprintf(out, "%d999", now);
   
    }



    void getRequestSign( const char *t, const char *accessKey, const char *clientKey,  const char *secretKey, const char *url, const char *method,  const char *body, char *out) {

      std::vector<String> host_params = split (String(url), String("?"));

      String request = host_params[0];
      if (host_params.size() > 1) {
        std::vector<String> params = split(host_params[1], "&");
        struct {
          bool operator()(String a, String b) const {
            return a < b;
          }
        } customLess;
        std::sort(params.begin(), params.end(), customLess);
        request += String("?") + join(params, String("&"));
      }

      char contentHash[SHA256_HASH_SIZE * 2 + 1];
      _sha256(body, contentHash);
      String StringToSign = String(method) + String("\n") + String(contentHash) + String("\n") + String("\n") + request;
      String signStr = String(clientKey) + String(accessKey) + String(t) + StringToSign;
      
      hmac(secretKey, signStr.c_str(), out);
  
    }

    void getRequestAuth( char *t, const char *clientKey, const char *secretKey, const char *url, const char *method, const  char *body, char *out) {

  
      std::vector<String> host_params = split (String(url), String("?"));
      String request = host_params[0];
      String query = host_params[1];

      std::vector<String> params = split(host_params[1], "&");
      struct {
        bool operator()(String a, String b) const {
          return a < b;
        }
      } customLess;
      std::sort(params.begin(), params.end(), customLess);


      request += String(" ? ") + join(params, String("&"));

      char contentHash[SHA256_HASH_SIZE * 2 + 1];
      _sha256("", contentHash);

      String StringToSign = String(method) + String("\n") + String(contentHash) + String("\n") + String("\n") + url;
      String signStr = String(clientKey) + String(t) + StringToSign;

      hmac(secretKey, signStr.c_str(), out);

    }

    bool TGetSwitch(const char *device_id, char *out) {
      if (strlen(tuya_token) == 0) {
        return false;
      }

      char buffer[1200];
      char command[200];
      sprintf(command, "/v1.0/iot-03/devices/%s/functions", device_id);
      if (TGet(device_id, command, buffer)) {
        Serial.println(buffer);
        DynamicJsonDocument root(3000);

        DeserializationError err = deserializeJson(root, buffer);
  
        if (err) {
          
          Serial.println("failed to deserialise");
          Serial.println(out);
          Serial.println(err.c_str());
        } else {
          Serial.println(out);
        
          if (root.containsKey("result")) {
            
            String s = root["result"]["functions"][0]["code"];
            strcpy(out,s.c_str());
            Serial.println("switch command");
            Serial.println(s);
            return true;
          } else {
            Serial.println("missing result");
            Serial.println(out);
          }
        }
      }
      return true;// BUG but we don;t want to spam
    }

    bool TGet(const char *device_id, const char *command, char *out) {

      HTTPClient http;
      char query[100];
      char url[200];
      char ts[14];
      timestamp(ts);
      char sign[200];

      if (strlen(tuya_token) == 0) {
        Serial.println("unauthorised");
        return false;
      }

      TuyaAuth::timestamp(ts);
      sprintf(url, "%s%s", this->host, command );

      getRequestSign(ts, tuya_token, client_id, secret_key, command, "GET", "", sign);
      http.begin(url);

      //client.println("Host: " + String(tuya_host));
      http.addHeader(String("t") , String(ts));
      http.addHeader(String("sign_method"), String("HMAC-SHA256"));
      http.addHeader(String("client_id"), String(client_id));
      http.addHeader(String("mode"), String("cors"));

      http.addHeader(String("access_token"), String(tuya_token));
      http.addHeader(String("sign") , String(sign));

      http.addHeader(String("Content-Type"), String("application/json"));

      int err = http.GET();

      String body = http.getString();
      Serial.println(body);
      if (err == 200) {
        strcpy(out, body.c_str());
        return true;
      } else {
        Serial.println("Failed to connect");
        Serial.println(err);
        Serial.println(body);
      }

      return false;
    }

    bool TCommand(const char *device_id, const char *command) {

      HTTPClient http;
      char query[100];
      char url[200];
      char ts[14];
      timestamp(ts);
      char sign[200];

      if (strlen(tuya_token) == 0) {
        Serial.println("unauthorised");
        return false;
      }

      TuyaAuth::timestamp(ts);
      sprintf(query, "/v1.0/iot-03/devices/%s/commands",  device_id);
      sprintf(url, "%s%s", this->host, query );

      getRequestSign(ts, tuya_token, client_id, secret_key, query, "POST", command, sign);
      http.begin(url);
      Serial.println(url);
      Serial.println(command);

      //client.println("Host: " + String(tuya_host));
      http.addHeader(String("t") , String(ts));
      http.addHeader(String("sign_method"), String("HMAC-SHA256"));
      http.addHeader(String("client_id"), String(client_id));
      http.addHeader(String("mode"), String("cors"));

      http.addHeader(String("access_token"), String(tuya_token));
      http.addHeader(String("sign") , String(sign));

      http.addHeader(String("Content-Type"), String("application/json"));

      int err = http.POST(command);
      String body = http.getString();
      Serial.println(body);
      if (err == 200) {
        DynamicJsonDocument root(5000);

        DeserializationError err = deserializeJson(root, body.c_str());

        if (err) {
          Serial.println("auth error");
          Serial.println(body);
        } else {
          if (root.containsKey("result")) {
            Serial.println(body);
            return true;
          } else {
            Serial.println("missing result");
            Serial.println(body);
          }
        }
      } else {
        Serial.println("Failed to connect");
        Serial.println(err);
        Serial.println(body);
      }

      return false;
    }

};

const uint8_t SEG_MINUS =  SEG_G; // -
TM1637Display display(CLK, DIO);
TuyaAuth *tuya;

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

float tmin, tmax;
char tuya_keyc[40];
char tuya_deviceh[40];
char tuya_keyh[40];
char tuya_devicec[40];
char tuya_ipc[20];
char tuya_iph[20];
char tuya_api_key[19];
char tuya_api_client[21];
char tuya_host[100];
char tuya_switch_c[20];
char tuya_switch_h[20];

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
  //sprintf(buffer, "Temperature in degree Celsius = % f, % d, % f % f % f",temperature,thermistor_adc_val ,thermistor_resistance,therm_res_ln,output_voltage);
  return temperature;
}


void handleRoot() {
  char buffer[60];
  double temperature1 = getThermistorReading(thermistor_output1);
  double temperature2 = getThermistorReading(thermistor_output2);
  double temperature = (temperature1 + temperature2) / 2;
  sprintf(buffer, " {\"c\":%f,\"low\":%f,\"high\":%f}", temperature, temperature1 < temperature2 ? temperature1 : temperature2, temperature1 > temperature2 ? temperature1 : temperature2);
  server.send(200, "text/plain", buffer);

}

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html><head>
<style type="text/css">
.form-style-2{
  max-width: 800px;
  padding: 20px 12px 10px 20px;
  font: 13px Arial, Helvetica, sans-serif;
}
.form-style-2-heading{
  font-weight: bold;
  font-style: italic;
  border-bottom: 2px solid #ddd;
  margin-bottom: 20px;
  font-size: 15px;
  padding-bottom: 3px;
}
.form-style-2 label{
  display: block;
  margin: 0px 0px 15px 0px;
}
.form-style-2 label > span{
  width: 170px;
  font-weight: bold;
  float: left;
  padding-top: 8px;
  padding-right: 5px;
}
.form-style-2 span.required{
  color:red;

.form-style-2 input.input-field, .form-style-2 .select-field{
  width: 48%; 
}
.form-style-2 input.input-field, 
.form-style-2 .tel-number-field, 
.form-style-2 .textarea-field, 
 .form-style-2 .select-field{
  box-sizing: border-box;
  -webkit-box-sizing: border-box;
  -moz-box-sizing: border-box;
  border: 1px solid #C2C2C2;
  box-shadow: 1px 1px 4px #EBEBEB;
  -moz-box-shadow: 1px 1px 4px #EBEBEB;
  -webkit-box-shadow: 1px 1px 4px #EBEBEB;
  border-radius: 3px;
  -webkit-border-radius: 3px;
  -moz-border-radius: 3px;
  padding: 7px;
  outline: none;
}
.form-style-2 .input-field:focus, 
.form-style-2 .tel-number-field:focus, 
.form-style-2 .textarea-field:focus,  
.form-style-2 .select-field:focus{
  border: 1px solid #0C0;
}

.form-style-2 input[type=submit],
.form-style-2 input[type=button]{
  border: none;
  padding: 8px 15px 8px 15px;
  background: #FF8500;
  color: #fff;
  box-shadow: 1px 1px 4px #DADADA;
  -moz-box-shadow: 1px 1px 4px #DADADA;
  -webkit-box-shadow: 1px 1px 4px #DADADA;
  border-radius: 3px;
  -webkit-border-radius: 3px;
  -moz-border-radius: 3px;
}
.form-style-2 input[type=submit]:hover,
.form-style-2 input[type=button]:hover{
  background: #EA7B00;
  color: #fff;
}
</style>

  <title>Thermistor Setup</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  </head><body>
  <div class="form-style-2">
  <div class="form-style-2-heading">Provide your information</div>
  <form action="/setting">
    <label for="min"><span>Min</span><input id="min" type="number" name="min" value="%MIN%"></label>
    <label for="max"><span>Max</span><input id="max" type="number" name="max" value="%MAX%"></label>
     <label for="api_host"><span>Tuya API url</span><input  id="api_host" type="text" maxlength="100"  name="tuya_host" value="%TUYA_HOST%"></label>
    <label for="api_id"><span>API Client Id</span><input  id="api_id" type="text" maxlength="20"  name="tuya_api_client" value="%TUYA_CLIENT%"></label>
    <label for="kh"><span>API Secret Key</span><input  id="api_key" type="text" maxlength="32" name="tuya_api_key" value="%TUYA_APIKEY%"></label>
    <label for="idh"><span>Heating Device Id</span><input  id="idh" type="text" maxlength="20"  name="tuya_deviceh" value="%TUYA_DEVICEH%"></label>
    <label for="kh"><span>Heating Device Local Key</span><input  id="kh" type="text" maxlength="16" name="tuya_keyh" value="%TUYA_KEYH%"></label>
    <label for="iph"><span>Heating Device Local Ip</span><input  id="iph" type="text" maxlength="16" name="tuya_iph" value="%TUYA_IPH%"></label>
    <label for="idc"><span>Cooling Device Id</span><input  id="idc" type="text" maxlength="20"  name="tuya_devicec" value="%TUYA_DEVICEC%"></label>
     <label for="kc"><span>Cooling Device Local Key</span><input  id="kc" type="text" maxlength="16"  name="tuya_keyc" value="%TUYA_KEYC%"></label>
     <label for="ipc"><span>Cooling Device Local ip</span><input  id="ipc" type="text" maxlength="16"  name="tuya_ipc" value="%TUYA_IPC%"></label>
    <input type="submit" value="Submit">
  </form>
 </div>
</body></html>)rawliteral";

void handleSetting() {

  if (server.hasArg("min")) {
    tmin = strtof(server.arg("min").c_str(), NULL);
  }
  if (server.hasArg("max")) {
    tmax = strtof(server.arg("max").c_str(), NULL);
  }
  if (server.hasArg("tuya_keyh") ) {
    const  char *tuya_key_arg = server.arg("tuya_keyh").c_str();
    if (strlen(tuya_key_arg) <= 18) {
      strcpy(tuya_keyh, tuya_key_arg);
    }

  }

  if (server.hasArg("tuya_deviceh") ) {
    const char *tuya_device_arg = server.arg("tuya_deviceh").c_str();
    if (strlen(tuya_device_arg) <= 20) {
      strcpy(tuya_deviceh, tuya_device_arg);
    }

  }

  if (server.hasArg("tuya_keyc") ) {
    const  char *tuya_key_arg = server.arg("tuya_keyc").c_str();
    if (strlen(tuya_key_arg) == 18) {
      strcpy(tuya_keyh, tuya_key_arg);
    }

  }

  if (server.hasArg("tuya_devicec") ) {
    const char *tuya_device_arg = server.arg("tuya_devicec").c_str();
    if (strlen(tuya_device_arg) == 20) {
      strcpy(tuya_deviceh, tuya_device_arg);
    }

  }

  if (server.hasArg("tuya_ipc") ) {
    const char *tuya_device_arg = server.arg("tuya_ipc").c_str();
    if (strlen(tuya_device_arg) < 16) {
      strcpy(tuya_ipc, tuya_device_arg);
    }
  }
  if (server.hasArg("tuya_iph") ) {
    const char *tuya_device_arg = server.arg("tuya_iph").c_str();
    if (strlen(tuya_device_arg) < 17) {
      strcpy(tuya_iph, tuya_device_arg);
    }
  }


  if (server.hasArg("tuya_api_key") ) {
    const char *tuya_device_arg = server.arg("tuya_api_key").c_str();
    if (strlen(tuya_device_arg) < 33) {
      strcpy(tuya_api_key, tuya_device_arg);
    }
  }
  if (server.hasArg("tuya_api_client") ) {
    const char *tuya_device_arg = server.arg("tuya_api_client").c_str();
    if (strlen(tuya_device_arg) < 21) {
      strcpy(tuya_api_client, tuya_device_arg);
    }
  }

  if (server.hasArg("tuya_host") ) {
    const char *tuya_device_arg = server.arg("tuya_host").c_str();
    if (strlen(tuya_device_arg) < 100) {
      strcpy(tuya_host, tuya_device_arg);
    }
  }
  char buffer[1000];
  sprintf(buffer, "{\"min\":\"%f\",\"max\":\"%f\",\"tuya_deviceh\":\"%s\",\"tuya_keyh\":\"%s\",\"tuya_devicec\":\"%s\",\"tuya_keyc\":\"%s\",\"tuya_ipc\":\"%s\",\"tuya_iph\":\"%s\",\"tuya_api_key\": \"%s\",\"tuya_api_client\": \"%s\",\"tuya_host\": \"%s\"}",
          tmin, tmax, tuya_deviceh, tuya_keyh, tuya_devicec, tuya_keyc, tuya_ipc, tuya_iph, tuya_api_key, tuya_api_client, tuya_host);

  File file = SPIFFS.open("/settings.txt", FILE_WRITE);
  int l = strlen(buffer);

  for (int i = 0; i <= l; i++) {
    file.write(buffer[i]);
  }
  file.close();
  String new_index_html = index_html;
  new_index_html.replace("%MIN%", String(tmin, 2));
  new_index_html.replace("%MAX%", String(tmax, 2));
  new_index_html.replace("%TUYA_DEVICEC%", tuya_devicec);
  new_index_html.replace("%TUYA_DEVICEH%", tuya_deviceh);
  new_index_html.replace("%TUYA_KEYH%", tuya_keyh);
  new_index_html.replace("%TUYA_KEYC%", tuya_keyc);
  new_index_html.replace("%TUYA_IPH%", tuya_iph);
  new_index_html.replace("%TUYA_IPC%", tuya_ipc);
  new_index_html.replace("%TUYA_CLIENT%", tuya_api_client);
  new_index_html.replace("%TUYA_APIKEY%", tuya_api_key);
  new_index_html.replace("%TUYA_HOST%", tuya_host);
  server.send(200, "text/html", new_index_html);
}


void Task1code( void * parameter) {

  char state = 'N';

  for (;;) {
    double temperature1 = getThermistorReading(thermistor_output1);
    double temperature2 = getThermistorReading(thermistor_output2);
    double temperature = (temperature1 + temperature2) / 2;

    char buffer[60];
    sprintf(buffer, "%2.2f %2.2f %2.2f", temperature, temperature1, temperature2);
    //Serial.println(buffer);
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

    if (strlen(tuya_switch_h) == 0 && strlen(tuya_deviceh) > 0) {
      tuya->TGetSwitch(tuya_deviceh,  tuya_switch_h);

    }
    if (strlen(tuya_switch_c) == 0 && strlen(tuya_devicec) > 0) {
      tuya->TGetSwitch(tuya_devicec,  tuya_switch_c);

    }
    int successHon = -1;
    int successCon = -1;
    int successHoff = -1;
    int successCoff = -1;
    if (temperature1 < tmin && temperature2 < tmin) {
      if ( strlen(tuya_deviceh) > 0 && state != 'H' ) {
        successHon = tuya->TCommand(tuya_deviceh, "{\"commands\":[{\"code\":\"switch\",\"value\":true}]}");
      }
      if ( strlen(tuya_devicec) > 0  && state != 'H')
      {
        successCoff = tuya->TCommand(tuya_devicec, "{\"commands\":[{\"code\":\"switch\",\"value\":false}]}");
      }
      if (successHon == 1) {
        state = 'H';
      }
    } else if  (temperature1 > tmin || temperature2 > tmax) {
      if ( strlen(tuya_deviceh) > 0 && state != 'C') {
        successHoff = tuya->TCommand(tuya_deviceh, "{\"commands\":[{\"code\":\"switch\",\"value\":false}]}");
      }
      if ( strlen(tuya_devicec) > 0 && state != 'C')
      {
        successCon = tuya->TCommand(tuya_devicec, "{\"commands\":[{\"code\":\"switch\",\"value\":true}]}");
      }
      if (successHoff == 1) {
        state = 'H';
      }
    }


    delay(1000);
  }

}




void setup(void) {
  Serial.begin(115200);

  if (!SPIFFS.begin(true)) {
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }

  tmin = 24;
  tmax = 40;
  tuya_keyh[0] = '\0';
  tuya_deviceh[0] = '\0';
  tuya_keyc[0] = '\0';
  tuya_devicec[0] = '\0';
  tuya_ipc[0] = '\0';
  tuya_iph[0] = '\0';
  tuya_switch_h[0] = '\0';
  tuya_switch_c[0] = '\0';

  strcpy(tuya_host, "https://openapi.tuyaeu.com");

  if (SPIFFS.exists("/settings.txt")) {


    File file = SPIFFS.open("/settings.txt", FILE_READ);

    if (file) {

      int pos = 0;
      char buffer[500];
      while (file.available()) {
        //Serial.print(pos);
        buffer[pos++] = file.read();
        Serial.print(buffer[pos - 1]);
        if (buffer[pos - 1] == '}') break;
      }
      buffer[pos] = '\0';

      file.close();

      DynamicJsonDocument root(5000);

      DeserializationError err = deserializeJson(root, buffer);

      if (!err) {
        if (root.containsKey("min")) {
          tmin = root["min"];
        } else {
          Serial.println("missing min key");
        }
        if (root.containsKey("max")) {
          tmax = root["max"];
        } else {
          Serial.println("missing max key");
        }
        if (root.containsKey("tuya_iph")) {
          strcpy(tuya_iph, root["tuya_iph"]);
        } else {
          Serial.println("missing tuya_iph key");
        }
        if (root.containsKey("tuya_ipc")) {
          strcpy(tuya_ipc, root["tuya_ipc"]);
        } else {
          Serial.println("missing tuya_ipc key");
        }
        if (root.containsKey("tuya_deviceh")) {
          strcpy(tuya_deviceh, root["tuya_deviceh"]);
        } else {
          Serial.println("missing tuya_deviceh key");
        }
        if (root.containsKey("tuya_keyh")) {
          strcpy(tuya_keyh, root["tuya_keyh"]);
        } else {
          Serial.println("missing tuya_keyh key");
        }
        if (root.containsKey("tuya_devicec")) {
          strcpy(tuya_devicec, root["tuya_devicec"]);
        } else {
          Serial.println("missing tuya_devicec key");
        }
        if (root.containsKey("tuya_keyc")) {
          strcpy(tuya_keyc, root["tuya_keyc"]);
        } else {
          Serial.println("missing tuya_keyc key");
        }
        if (root.containsKey("tuya_api_client")) {
          strcpy(tuya_api_client, root["tuya_api_client"]);
        } else {
          Serial.println("missing tuya_api_client key");
        }
        if (root.containsKey("tuya_api_key")) {
          strcpy(tuya_api_key, root["tuya_api_key"]);
        } else {
          Serial.println("missing tuya_api_key");
        }
      } else {
        Serial.println("json parse failed:");
        Serial.println(err.f_str());
        Serial.println(buffer);
      }
    }


  }
  if (strlen(tuya_keyh) == 16 && strlen(tuya_deviceh)) {
    //Serial.println("init heat plug");
    //Serial.println(tuya_deviceh);
    //Serial.println(tuya_keyh);
    //Serial.println(tuya_iph);
    if (strcmp("1030d805a7d4d605", tuya_keyh) != 0) {
      Serial.println("key error");
    }
    if (strcmp("42482361840d8e9c520c", tuya_deviceh) != 0) {
      Serial.println("device error");
    }
    plugh = new TuyaDevice(tuya_deviceh, tuya_keyh, tuya_iph, 6668, "3.1");;
  } else {
    Serial.println("missing heat plug ");
    Serial.println( strlen(tuya_deviceh));
    Serial.println( strlen(tuya_keyh));
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

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);



  display.setSegments(SEG_DONE);

  xTaskCreatePinnedToCore(
    Task1code,   /* Task function. */
    "Task1",     /* name of task. */
    8000,       /* Stack size of task */
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

  if (plugh) {
    Serial.println("Turn ON");
    //plugh->set(true);
  }

  if (strlen(tuya_api_client) && strlen(tuya_api_key) && strlen(tuya_host) ) {


    tuya = new TuyaAuth(tuya_host, tuya_api_client, tuya_api_key);

  }


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
  delay(1000);//allow the cpu to switch to other tasks
  //int btnState = button.getState();
  //Serial.println(btnState);
  char t[14];

  //Serial.println("TimeStamp");
  //TuyaAuth::timestamp(t);
  //Serial.println(t);
}

void readId(char *buffer) {
  for (int i = 0; i < 20; i++) {

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

  //Serial.println("check eeprom crc");
  // get stored crc
  unsigned long storedCrc;
  EEPROM.get(EEPROM.length() - 4, storedCrc);

  if (storedCrc != calculatedCrc) {
    Serial.println("initialise eeprom");
    writeId("thermistor");
  }
  //Serial.println("init complete");
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
