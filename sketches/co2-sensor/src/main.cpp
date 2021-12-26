/**** 
 * Co2 Sensor
 *  Compile with board WEMOS D1 R1
 * 
 * F. Guiet 
 * Creation           : 20211224
 * Last modification  : 
 * 
 * Version            : 1.0
 * 
 * History            : 1.0 - 20211224
 *                      
 */
                

// Co2 value taken here
//https://fr.doublet.com/appareil-mesure-de-la-qualite-de-l-air
//https://th-industrie.com/content/13-mesure-co2

// co2 < 800 OK
// 800 <= co2 < 1200 go open windows to make fresh air enter
// >= 1200 Danger!!

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <secret.h>

//SparkFun SCD30 CO₂ Sensor Library
//https://github.com/sparkfun/SparkFun_SCD30_Arduino_Library

// bibliotheque adafruit neopixel
#include <Adafruit_NeoPixel.h>

#include <Wire.h>

#include "SparkFun_SCD30_Arduino_Library.h" //Click here to get the library: http://librarymanager/All#SparkFun_SCD30
SCD30 airSensor;

#define DEBUG 0
#define MAX_RETRY 50
#define MQTT_SERVER "mqtt.guiet.lan"
#define FIRMWARE_VERSION "1.0"
const int SERIAL_BAUD = 9600;
const int MQTT_PORT = 1883;

#define MQTT_CLIENT_ID "CO2SensorMqttClient"  
#define MQTT_HUB_TOPIC "guiet/co2sensor/1"      
#define HOSTNAME "CO2_SENSOR"

// Wemos Pinout - https://randomnerdtutorials.com/esp8266-pinout-reference-gpios/

// D1 = GPIO5 = SCL
// D2 = GPIO4 = SDA

#define RINGLED_PIN  2 //D4 on silk screen


//Ringled of 24 leds
#define LED_COUNT  24

// 0 min => max 255
#define BRIGHTNESS 100

// Declare votre neopixel Ring

// Argument 1 = Nombre de Pixel dans le Ring
// Argument 2 = Wemos Pin
// Argument 3 = Pixel type
//   NEO_KHZ800  800 KHz bitstream (most NeoPixel products w/WS2812 LEDs)
//   NEO_KHZ400  400 KHz (classic 'v1' (not v2) FLORA pixels, WS2811 drivers)
//   NEO_GRB     Led cablé selon GRB (most NeoPixel products)
//   NEO_GRBW    Led cablé selon GRB + W (most NeoPixel products) + Led blanche
//   NEO_RGB     Led cablé selon RGB (v1 FLORA pixels, not v2)
//   NEO_RGBW   led cablé selon  RGBW (NeoPixel RGBW products)

Adafruit_NeoPixel ring(LED_COUNT, RINGLED_PIN, NEO_GRB + NEO_KHZ800);

// definition des couleurs
uint32_t Blue=ring.Color(0, 0, 255);
uint32_t Red=ring.Color(255, 0, 0);
uint32_t Green=ring.Color(0, 255, 0);
uint32_t Yellow=ring.Color(255, 255, 0);
uint32_t Orange=ring.Color(255, 106, 0);
uint32_t White=ring.Color(255, 255, 255);

WiFiClient espClient;
PubSubClient client(espClient);

unsigned long previousMillis=0;
const int INTERVAL = 60000; //Every 60s
char message_buff[200];

bool co2Low = true;
bool co2Medium = true;
bool co2High = true;

void debug_message(String message, bool doReturnLine) {
  if (DEBUG) {

    if (doReturnLine)
      Serial.println(message);
    else
      Serial.print(message);

    Serial.flush();
  }
}

void connectToMqtt() {
  
  client.setServer(MQTT_SERVER, MQTT_PORT); 

  int retry = 0;

  debug_message("Attempting MQTT connection...", true);
  while (!client.connected()) {   
    if (client.connect(MQTT_CLIENT_ID)) {
      debug_message("connected to MQTT Broker...", true);
    }
    else {
      delay(500);
      debug_message(".", false);
      retry++;
    }

    if (retry >= MAX_RETRY) {
      ESP.restart();
    }
  }
}

void connectToWifi() 
{
  debug_message("Connecting to WiFi...", true);
    
  WiFi.mode(WIFI_STA); //Should be there, otherwise hostname is not set properly  
  WiFi.setHostname(HOSTNAME);
  
  WiFi.begin(ssid, password);
  
  int retry = 0;
  while (WiFi.status() != WL_CONNECTED) {  
    delay(500);

    debug_message(".", false);
    retry++;  

    if (retry >= MAX_RETRY)
      ESP.restart();
  }

  if (DEBUG) {
    Serial.println ( "" );
    Serial.print ( "Connected to " );
    Serial.println ( WiFi.SSID() );
    Serial.print ( "IP address: " );
    Serial.println ( WiFi.localIP() );
    Serial.print ( "MAC: " );    
    Serial.println ( WiFi.macAddress());    
  }
}

// fonction qui allume les leds unes apres les autres de la même couleur
// arg 1 : couleur , arg 2 : Vitesse , arg 3 : une seule led allumé
void colorWipe(uint32_t color, int speed=50,bool single=false) {

  for(int i=0; i<ring.numPixels(); i++) {
    if (single) ring.clear();
    ring.setPixelColor(i, color);
    ring.show();    
    delay(speed);
  }

}

// Init ring with one color and show it
void initFullRingColor(uint32_t color) {
  for(int i=0; i<ring.numPixels(); i++) {    
    ring.setPixelColor(i, color);        
  }  
}

void showCardinalPoint(uint32_t color) {
  //Clear Ring
  ring.clear();
  ring.show();

  ring.setPixelColor(5, color);
  ring.setPixelColor(11, color);
  ring.setPixelColor(17, color);
  ring.setPixelColor(23, color);
  ring.show();
}

void fullRingBlink(uint32_t color, int blinkCount, int blinkTime) {

  for (int i=0;i<blinkCount;i++) {

    ring.clear();    
    ring.show();
    delay(blinkTime);
    initFullRingColor(color);  
    ring.show();
    delay(blinkTime);
  }

  //Reset to black
  ring.clear();    
  ring.show();
}

String ConvertToJSon(uint16_t co2, float temp, float humidity) {
    
    DynamicJsonDocument  jsonBuffer(200);
    JsonObject root = jsonBuffer.to<JsonObject>();

    root["name"] = HOSTNAME;
    root["firmware"] = FIRMWARE_VERSION;
    root["co2"] = String(co2);
    root["temperature"] = String(temp,2);
    root["humidity"] = String(humidity, 2);
  
    String result;    
    serializeJson(root, result);

    return result;
}

void setup() {

  //Init ring
  ring.begin();           // INITIALIZE NeoPixel ring object (REQUIRED)
  ring.setBrightness(BRIGHTNESS); // Set BRIGHTNESS to about 1/5 (max = 255)
  ring.show();            // Turn OFF all pixels ASAP*/

  fullRingBlink(Blue, 3, 500);

  if (DEBUG) {
    //Init Serial
    Serial.begin(SERIAL_BAUD);    
  }
  
  //Init CO2 sensor
  Wire.begin();

  if (airSensor.begin() == false)
  {
    debug_message("Air sensor not detected. Please check wiring. Freezing...", true);    
    while (1) {    
      //Red Blinking !! error
      fullRingBlink(Red, 1, 1000);
    };      
  }

  debug_message("CO2 Sensor Ready...", true); 
}

void loop() {

  if (WiFi.status() != WL_CONNECTED) {      
      connectToWifi();      
  }

  if (!client.connected()) {
    connectToMqtt();
  }

  client.loop();
  
  if (airSensor.dataAvailable())
  {

    uint16_t co2 = airSensor.getCO2(); 

    debug_message("co2(ppm):", false);
    debug_message(String(co2), true);    

    if (co2 < 800 && co2Low) {
      co2Low = false;
      co2Medium = true;
      co2High = true;
      showCardinalPoint(Green);
      //colorWipe(Green, 100, false);

    }

    if (co2 >= 800 && co2 < 1200 && co2Medium) {
      co2Low = true;
      co2Medium = false;
      co2High = true;
      showCardinalPoint(Orange);
      //colorWipe(Orange, 100, true);
    }

    if (co2 > 1200 && co2 < 2000 && co2High) {
      co2Low = true;
      co2Medium = true;
      co2High = false;
      showCardinalPoint(Red);
      //colorWipe(Red, 100, true);
    }

    if (co2 >= 2000) {
      co2Low = true;
      co2Medium = true;
      co2High = true;
      fullRingBlink(Red, 3, 250);      
    } 

    float temp = airSensor.getTemperature();

    debug_message("temp(C):", false);
    debug_message(String(temp,2), true);  
    
    float humidity = airSensor.getHumidity();

    debug_message("humidity(%):", false);
    debug_message(String(humidity,2), true); 

    unsigned long currentMillis = millis();
    if (((unsigned long)(currentMillis - previousMillis) >= INTERVAL) || ((unsigned long)(millis() - previousMillis) < 0)) {
          
      String messJson =  ConvertToJSon(co2, temp, humidity);
      messJson.toCharArray(message_buff, messJson.length()+1);
      client.publish(MQTT_HUB_TOPIC,message_buff);

      debug_message("Publishing : " + messJson, true);

      // Save the current time to compare "later"
      previousMillis = currentMillis;
    }         
  }
  
  //delay(500);
}