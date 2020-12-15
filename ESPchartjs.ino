#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>
#include <FS.h>
#include <Ticker.h>
#include <SimpleDHT.h>
#include <ESP8266HTTPClient.h>
#include "MQ7.h"
#include <SoftwareSerial.h>
SoftwareSerial pmsSerial(12, 15); //D6, D8


// Collecting BMP180 sensor data
Ticker timer;
Ticker durationTimer;
Ticker betweenTimer;

unsigned int pm1 = 0;
unsigned int pm2_5 = 0;
unsigned int pm10 = 0;

char pm_value;
char pm_previousValue;

bool get_data = false;

// Connecting to the Internet
char * ssid = "SKYMEDIA-49";
char * password = "99109265";

//static values
byte temperature_inside = 20;
byte humidity_inside = 30;
float co_inside = 0;

float temperature_outside;
float humidity_outside;

int DHT11_pin = 5;  //D1

int MQ7_pin = A0;   //A0
int voltage = 3;

int fanState_pin = 2;  //D4
int fanDir_pin = 16;   //D0
int heater_pin = 14;   //D5

String APIKEY = "3e9b3259a28b1a2f6fc6d0e6d07a2b25";
String CityID = "2028462"; //Ulaanbaatar, MN

SimpleDHT11 dht11(DHT11_pin);    

// Running a web server
ESP8266WebServer server;

// Adding a websocket to the server
WebSocketsServer webSocket = WebSocketsServer(81);

// init MQ7 device
MQ7 mq7(MQ7_pin, voltage);

void setup() {
  
  Serial.begin(115200);
  pmsSerial.begin(9600);
  timer.attach(5, getData);
  
  int err = SimpleDHTErrSuccess;
  if ((err = dht11.read(&temperature_inside, &humidity_inside, NULL)) != SimpleDHTErrSuccess);
    

  pinMode(fanState_pin, OUTPUT);
  pinMode(fanDir_pin, OUTPUT);
  pinMode(heater_pin, OUTPUT);

  digitalWrite(heater_pin, 0);
  
  // put your setup code here, to run once:
  if(!SPIFFS.begin()){
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }
  Serial.println("");
  WiFi.begin(ssid, password);
  Serial.print("connecting.");
  
  while(WiFi.status()!=WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  Serial.println("");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  server.on("/",homepage);
  server.begin();
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  
  if (!MDNS.begin("automat-agaarjuulalt")) {             // Start the mDNS responder for esp8266.local
    Serial.println("Error setting up MDNS responder!");
  }else {
    Serial.println("mDNS responder started");
  }

  Serial.println("");   // blank new line

  Serial.println("Calibrating MQ7 . . .");
  mq7.calibrate();    // calculates R0
  Serial.println("Calibration done!");
  
  
}

void loop() {
  
  webSocket.loop();
  server.handleClient();
  MDNS.update();

  if (get_data) 
  {
     getWeatherData();

     getPmData();
     
    int err = SimpleDHTErrSuccess;
    if ((err = dht11.read(&temperature_inside, &humidity_inside, NULL)) != SimpleDHTErrSuccess);
    co_inside = mq7.readPpm();
    
    if ( co_inside > 20 )
    {
      digitalWrite(fanState_pin, 1);
      digitalWrite(fanDir_pin, 1);
    }else if ( pm2_5 > 200 )
    {
      digitalWrite(fanState_pin, 1);
    }
    
    String json = "{\"temp_o\":";
    json += temperature_outside; 
    json += ",";
    json += "\"temp_i\":";
    json += temperature_inside;
    json += ",";
    json += "\"hum_o\":";
    json += humidity_outside;
    json += ",";
    json += "\"hum_i\":";
    json += humidity_inside;
    json += ",";
    json += "\"co_i\":";
    json += co_inside;
    json += ",";
    json += "\"pm1\":";
    json += pm1;
    json += ",";
    json += "\"pm2_5\":";
    json += pm2_5;
    json += ",";
    json += "\"pm10\":";
    json += pm10;
    json += "}";
    
    webSocket.broadcastTXT(json.c_str(), json.length());
    get_data = false;
  }
  
}

void getData() {

  get_data = true;

}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length){
  // Do something with the data from the client
  if(type == WStype_TEXT){
    
    float dataRate = (float) atof((const char *) &payload[0]);
    float fanState = (float) atof((const char *) &payload[2]);
    float fanDir = (float) atof((const char *) &payload[4]);
    float automatState = (float) atof((const char *) &payload[6]);
    float pleasantTemp = (float) atof((const char *) &payload[8]);
    float durationTime = (float) atof((const char *) &payload[11]);
    float betweenTime = (float) atof((const char *) &payload[14]);

    if ( fanState == 1 && fanDir == 0 ) 
    {
      if ( pleasantTemp > temperature_outside)
     {
       digitalWrite(heater_pin, 1);
     }else
     {
       digitalWrite(heater_pin, 0);
     }
    }else if( fanState == 0 || fanDir == 1 )
    {
      digitalWrite(heater_pin, 0);
    }

    if ( automatState == 1 ) 
    {
      betweenTimer.detach();
      durationTimer.detach();
      automat( betweenTime, durationTime);
    }else
    {
      betweenTimer.detach();
      durationTimer.detach();
      digitalWrite(fanState_pin, fanState);
      digitalWrite(fanDir_pin, fanDir);
    }
    
    
    timer.detach();
    timer.attach(dataRate, getData);
     Serial.println(dataRate);
     Serial.println(fanState);
     Serial.println(fanDir);
     Serial.println(automatState);
     Serial.println(pleasantTemp);
     Serial.println(betweenTime);
     Serial.println(durationTime);
     Serial.println();
  }
  
}

void automat(int betweenTime, int durationTime)
{  
  betweenTimer.attach(betweenTime, premission, (int)durationTime);
}

void premission(int durationTime)
{
  digitalWrite(fanState_pin, 1);
  durationTimer.attach(durationTime, timeOut);
}

void timeOut()
{
  digitalWrite(fanState_pin, 0);
  durationTimer.detach();
}

void getWeatherData() //client function to send/receive GET request data.
{
    HTTPClient http;  //Object of class HTTPClient
    http.begin("http://api.openweathermap.org/data/2.5/weather?id=2028462&units=metric&APPID=3e9b3259a28b1a2f6fc6d0e6d07a2b25");

    int httpCode = http.GET();
//    Serial.println(httpCode);
//    Serial.println(http.getString());
    //Check the returning code                                                                  
    if (httpCode > 0) {
      // Parsing
      DynamicJsonDocument doc(2048);
      deserializeJson(doc, http.getString());

      float temperature = doc["main"]["temp"];
      float humidity = doc["main"]["humidity"];
      
      temperature_outside = temperature;
      humidity_outside = humidity;

//      Serial.println(Temperature);

    }
    http.end();   //Close connection
}

void getPmData() 
{
  int index = 0;

  while (pmsSerial.available()) {
    pm_value = pmsSerial.read();
//    Serial.println(value);
    
    if ((index == 0 && pm_value != 0x42) || (index == 1 && pm_value != 0x4d)){
      Serial.println("Cannot find the data header.");
      break;
    }

    if (index == 4 || index == 6 || index == 8 || index == 10 || index == 12 || index == 14) {
      pm_previousValue = pm_value;
    }
    else if (index == 5) {
      pm1 = 256 * pm_previousValue + pm_value;
//      Serial.print("{ ");
//      Serial.print("\"pm1\": ");
//      Serial.print(pm1);
//      Serial.print(", ");
    }
    else if (index == 7) {
      pm2_5 = 256 * pm_previousValue + pm_value;
//      Serial.print("\"pm2_5\": ");
//      Serial.print(pm2_5);
//      Serial.print(", ");
    }
    else if (index == 9) {
      pm10 = 256 * pm_previousValue + pm_value;
//      Serial.print("\"pm10\": ");
//      Serial.print(pm10);
    } else if (index > 15) {
      break;
    }
    index++;
  }
  while(pmsSerial.available()) pmsSerial.read();
//  Serial.println(" }");
}

void homepage()
{
  File file = SPIFFS.open("/index.html","r");
  server.streamFile(file, "text/html");

}

