/*-------------------------------------------------------------------------
  Arduino sketch to control a (old) Brose flipdot display (112x16 dots).

  See ... for more information.

  Written by dkroeske(dkroeske@gmail.com), august 2019

  Happy Coding
  
  -------------------------------------------------------------------------
  
  The MIT License (MIT)
  Copyright © 2019 <copyright Diederich Kroeske>
  
  Permission is hereby granted, free of charge, to any person obtaining a 
  copy of this software and associated documentation files (the “Software”), 
  to deal in the Software without restriction, including without limitation 
  the rights to use, copy, modify, merge, publish, distribute, sublicense, 
  and/or sell copies of the Software, and to permit persons to whom the 
  Software is furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in 
  all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR 
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL 
  THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER 
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, 
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN 
  THE SOFTWARE.

  -------------------------------------------------------------------------*/

//#define SECURE

#include <Adafruit_NeoPixel.h>

#include <WiFiManager.h>
#include <SPI.h>
#include <Wire.h>
#include <ESP8266WiFi.h>

#ifdef SECURE
  #include <WiFiClientSecure.h>
#else
  #include <WiFiClient.h>
#endif


#include <PubSubClient.h>
// MAKE SURE: in PubSubClient.h change MQTT_MAX_PACKET_SIZE to 2048 !! //

#include <ESP8266mDNS.h>
#include <ArduinoJson.h>
#include <time.h>
#include <string.h>
#include <simpleDSTadjust.h>

#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <FS.h>

// WiFi RESET pin GPIO_0 (D3 wemos D1 Mini)
#define RST_PIN 0 // D3 on WeMos
#define BL_PIN 14 // D5 on WeMos
bool backlightAutoOff;          

#include <Adafruit_GFX.h>
#include "brose_fp_gfx.h"
#include "fonts/FlipDot7x8.h"
#include "fonts/FlipDot16x10.h"

// Neo pixel
#define NEOPIXELCOUNT   1     // Number of Neo pixels
#define NEOPIXELPORT    2     // Neo pixel GPIO2 (D4 wemos)

#define COLOR_SATURATION 255       // Neo pixel saturartion
#define COLOR_IDLE_BRIGHTNESS 10   // Neo pixel brightness when idle
#define COLOR_FLASH_BRIGHTNESS 120 // Neo pixel brightness when flashing
Adafruit_NeoPixel neoPixel(NEOPIXELCOUNT, NEOPIXELPORT, NEO_GRB + NEO_KHZ800);

const uint32_t COLOR_RED = neoPixel.Color(COLOR_SATURATION, 0, 0);
const uint32_t COLOR_GREEN = neoPixel.Color(0, COLOR_SATURATION, 0);
const uint32_t COLOR_BLUE = neoPixel.Color(0, 0, COLOR_SATURATION);
const uint32_t COLOR_WHITE = neoPixel.Color(COLOR_SATURATION, COLOR_SATURATION, COLOR_SATURATION);
const uint32_t COLOR_BLACK = neoPixel.Color(0,0,0);

// Local variables
WiFiManager wifiManager;

// Application configs struct. 
bool shouldSaveConfig;

#define MQTT_USERNAME_LENGTH       32
#define MQTT_PASSWORD_LENGTH       32
#define MQTT_ID_TOKEN_LENGTH       64
#define MQTT_TOPIC_STRING_LENGTH   64
#define MQTT_REMOTE_HOST_LENGTH   128
#define MQTT_REMOTE_PORT_LENGTH    10

typedef struct {
   char     mqtt_username[MQTT_USERNAME_LENGTH];
   char     mqtt_password[MQTT_PASSWORD_LENGTH];
   char     mqtt_id[MQTT_ID_TOKEN_LENGTH];
   char     mqtt_topic[MQTT_TOPIC_STRING_LENGTH];
   char     mqtt_remote_host[MQTT_REMOTE_HOST_LENGTH];
   char     mqtt_remote_port[MQTT_REMOTE_HOST_LENGTH];
} APP_CONFIG_STRUCT;

APP_CONFIG_STRUCT app_config;

#ifdef SECURE
const char *sha1_fingerprint = "4D:CE:F3:36:5B:A7:5C:3F:9D:B8:C1:F8:3F:C8:E5:4D:46:79:4D:8F";
const char* mqttUser = "";
const char* mqttPassword = "";
#endif

BroseFlipDot_28x16 flipdot = BroseFlipDot_28x16();

#ifdef SECURE
WiFiClientSecure wifiClient;
#else
WiFiClient wifiClient;
#endif

// Only with some dummy values seems to work ... instead of mqttClient();
PubSubClient mqttClient("", 0, wifiClient);

/* Prototype FSM functions. */
void start_pre(void);
void start_heartbeat(void);
void start_refresh(void);
void start_post(void);

void idle_pre(void);
void idle_heartbeat(void);
void idle_refresh(void);
void idle_post(void);

void mqtt_pre(void);
void mqtt_heartbeat(void);
void mqtt_refresh(void);
void mqtt_post(void);

/* Define FSM (states, events) */
typedef enum { EV_TRUE, EV_MQTT, EV_IDLE } ENUM_EVENT;
typedef enum { STATE_START, STATE_IDLE, STATE_MQTT } ENUM_STATE;

/* Define FSM transition */
typedef struct {
   void (*pre)(void);
   void (*heartbeat)(void);
   void (*refresh)(void);
   void (*post)(void);
   ENUM_STATE nextState;
} STATE_TRANSITION_STRUCT;

// Flipdot FSM definition (see statemachine diagram)
//
//        | EV_TRUE  EV_MQTT_MSG EV_IDLE
// -----------------------------------------------------------------
// START  | START    START       ILDE   Handle STARTUP      
// IDLE   | IDLE     MQTT        IDLE   Handle IDLE loop
// MQTT   | MQTT     MQTT        IDLE   Handle MQTT messages 
STATE_TRANSITION_STRUCT fsm[3][3] = {
  { 
    {start_pre, start_heartbeat, start_refresh, NULL, STATE_START},
    {start_pre, start_heartbeat, start_refresh, start_post, STATE_START},
    {start_pre, start_heartbeat, start_refresh, start_post, STATE_IDLE}
  },  // State START
  { 
    {idle_pre, idle_heartbeat, idle_refresh, idle_post, STATE_IDLE},
    {idle_pre, idle_heartbeat, idle_refresh, idle_post, STATE_MQTT},
    {idle_pre, idle_heartbeat, idle_refresh, idle_post, STATE_IDLE}
  },  // State IDLE
  { 
    {mqtt_pre, mqtt_heartbeat, mqtt_refresh, mqtt_post, STATE_MQTT},
    {mqtt_pre, mqtt_heartbeat, mqtt_refresh, mqtt_post, STATE_MQTT},
    {mqtt_pre, mqtt_heartbeat, mqtt_refresh, mqtt_post, STATE_IDLE}
  },  // State MQTT
};

// State holder
ENUM_STATE state;
ENUM_EVENT event;

// NTP server update interval (every hour)
#define NTP_UPDATE_INTERVAL_SEC 1000*60*60
uint32_t ntp_update_cur=0, ntp_update_prev=0;

// Heartbeat (polling)
#define HEARTBEAT_UPDATE_INTERVAL_SEC 1000 * 1
uint32_t heartbeat_cur=0, heartbeat_prev=0;

// (Display) refresh (polling)
#define REFRESH_UPDATE_INTERVAL_SEC 1000 * 60 * 5
uint32_t refresh_cur=0, refresh_prev=0;

// NTP time/date servers in NL
#define NTP_SERVERS "0.nl.pool.ntp.org", "1.nl.pool.ntp.org", "2.nl.pool.ntp.org"
#define timezone 1 // Central Europe, Time Zone (Amsterdam, the Netherlands)
struct dstRule StartRule = {"CEST", Last, Sun, Mar, 2, 3600};    // Daylight time = UTC/GMT +2 hours
struct dstRule EndRule = {"CET", Last, Sun, Oct, 1, 0};          // Standard time = UTC/GMT +1 hours
simpleDSTadjust dstAdjusted(StartRule, EndRule);

// Some LOGO displayed at boot
static const unsigned char PROGMEM ilovedots [] = {
0x00, 0x0f, 0xf0, 0x3c, 0x3c, 0x0f, 0xe0, 0x03, 0xf0, 0x3f, 0xff, 0x07, 0xe0, 0x00, 0x00, 0x0f, 
0xf0, 0x7e, 0x7e, 0x0f, 0xf8, 0x07, 0xf8, 0x3f, 0xff, 0x1f, 0xf0, 0x00, 0x00, 0x0f, 0xf0, 0xff, 
0xff, 0x0f, 0xfc, 0x0f, 0xfc, 0x3f, 0xff, 0x1f, 0xf0, 0x00, 0x00, 0x03, 0xc1, 0xe7, 0xe7, 0x8e, 
0x3e, 0x1f, 0x3e, 0x3f, 0xff, 0x3c, 0x30, 0x00, 0x00, 0x03, 0xc1, 0xc3, 0xc3, 0x8e, 0x1e, 0x1e, 
0x1e, 0x01, 0xe0, 0x3c, 0x10, 0x00, 0x00, 0x03, 0xc1, 0xc1, 0x83, 0x8e, 0x0f, 0x3c, 0x0f, 0x01, 
0xe0, 0x3e, 0x00, 0x00, 0x00, 0x03, 0xc1, 0xc0, 0x03, 0x8e, 0x0f, 0x3c, 0x0f, 0x01, 0xe0, 0x3f, 
0xc0, 0x00, 0x00, 0x03, 0xc1, 0xe0, 0x07, 0x8e, 0x0f, 0x3c, 0x0f, 0x01, 0xe0, 0x1f, 0xf0, 0x00, 
0x00, 0x03, 0xc0, 0xe0, 0x07, 0x0e, 0x0f, 0x3c, 0x0f, 0x01, 0xe0, 0x0f, 0xf0, 0x00, 0x00, 0x03, 
0xc0, 0xf0, 0x0f, 0x0e, 0x0f, 0x3c, 0x0f, 0x01, 0xe0, 0x03, 0xf8, 0x00, 0x00, 0x03, 0xc0, 0x78, 
0x1e, 0x0e, 0x0f, 0x3c, 0x0f, 0x01, 0xe0, 0x00, 0xf8, 0x00, 0x00, 0x03, 0xc0, 0x3c, 0x3c, 0x0e, 
0x1e, 0x1e, 0x1e, 0x01, 0xe0, 0x20, 0x78, 0x00, 0x00, 0x03, 0xc0, 0x3e, 0x7c, 0x0e, 0x3e, 0x1f, 
0x3e, 0x01, 0xe0, 0x30, 0x78, 0x00, 0x00, 0x0f, 0xf0, 0x0f, 0xf0, 0x0f, 0xfc, 0x0f, 0xfc, 0x01, 
0xe0, 0x3f, 0xf0, 0x00, 0x00, 0x0f, 0xf0, 0x07, 0xe0, 0x0f, 0xf8, 0x07, 0xf8, 0x01, 0xe0, 0x3f, 
0xe0, 0x00, 0x00, 0x0f, 0xf0, 0x01, 0x80, 0x0f, 0xe0, 0x03, 0xf0, 0x01, 0xe0, 0x0f, 0xc0, 0x00
};

// mqtt topic strings: flipdot/<uid>/msg and flipdot/<uid>/msg
char mqtt_topic_raw[128];
char mqtt_topic_msg[128];

// MQTT Message, command
typedef struct {
  uint8_t x, y;
  uint8_t font;
  char content[128];
  char display_time;
} BFD_MESSAGE;

typedef struct {
  bool on;
  bool autoOff;
} BFD_BACKLIGHT;

BFD_MESSAGE bfd_message = { 0, 0, '\0', 60 };
BFD_BACKLIGHT bfd_backlight = { false, true };

/******************************************************************/
void setup()
/* 
short   : Arduino setup                   
inputs  :        
outputs : 
notes   :         
Version : DMK, Initial code
*******************************************************************/
{

  // Init Serial port
  Serial.begin(115200);

  // Define I/O pins
  pinMode(RST_PIN, INPUT);

  // Backlight
  bl_init();
  
  // Setup unique mqtt id and mqtt topic string
  create_unique_mqtt_topic_string(app_config.mqtt_topic);
  create_unigue_mqtt_id(app_config.mqtt_id);
  sprintf(mqtt_topic_raw,"bfd/%s/raw",app_config.mqtt_topic);
  sprintf(mqtt_topic_msg,"bfd/%s/msg",app_config.mqtt_topic);
  
  // Init with red led
  smartLedInit(COLOR_RED);
  for(int idx = 0; idx < 25; idx++ ) {
    smartLedFlash();
    delay(100);
  }
  
  // Perform factory when reset
  // is pressed during powerup
  if( 0 == digitalRead(RST_PIN) ) {
    Serial.printf("RST pushbutton pressed, reset WiFiManager and appConfif settings\n");
    wifiManager.resetSettings();
    deleteAppConfig();
    while(0 == digitalRead(RST_PIN)) {
      smartLedShowColor(COLOR_RED);
      Serial.printf("Wait for user to release RST pin\n");
      delay(500);
    }
    ESP.reset();
  }

  // Read config file or generate default
  if( !readAppConfig(&app_config) ) {
    strcpy(app_config.mqtt_username, "");
    strcpy(app_config.mqtt_password, "");
    strcpy(app_config.mqtt_remote_host, "test.mosquitto.org");
    strcpy(app_config.mqtt_remote_port, "1883");
    writeAppConfig(&app_config);
  }

  // Setup WiFiManager
  smartLedShowColor(COLOR_BLUE);
  wifiManager.setMinimumSignalQuality(20);
  wifiManager.setTimeout(300);
  wifiManager.setSaveConfigCallback(saveConfigCallback);
  wifiManager.setDebugOutput(false);
  shouldSaveConfig = false;

  // Adds some parameters to the default webpage
  WiFiManagerParameter wmp_text("<br/>MQTT setting:</br>");
  wifiManager.addParameter(&wmp_text);
  WiFiManagerParameter custom_mqtt_username("mqtt_username", "Username", app_config.mqtt_username, MQTT_USERNAME_LENGTH);
  WiFiManagerParameter custom_mqtt_password("mqtt_password", "Password", app_config.mqtt_password, MQTT_PASSWORD_LENGTH);
  WiFiManagerParameter custom_mqtt_remote_host("mqtt_remote_host", "Host", app_config.mqtt_remote_host, MQTT_REMOTE_HOST_LENGTH);
  WiFiManagerParameter custom_mqtt_remote_port("mqtt_port", "Port", app_config.mqtt_remote_port, MQTT_REMOTE_PORT_LENGTH);
  wifiManager.addParameter(&custom_mqtt_username);
  wifiManager.addParameter(&custom_mqtt_password);
  wifiManager.addParameter(&custom_mqtt_remote_host);
  wifiManager.addParameter(&custom_mqtt_remote_port);

  // Add the unit ID to the webpage
  char fd_str[128]="<p>Your Flipdot ID: <b>";
  strcat(fd_str, app_config.mqtt_topic);
  strcat(fd_str, "</b> (You will need this later in the app)</p>");
  WiFiManagerParameter mqqt_topic_text(fd_str);
  wifiManager.addParameter(&mqqt_topic_text);

  // Start WiFiManager ...
  if( !wifiManager.autoConnect("Brose Flipdot Config")) {
    delay(1000);
    ESP.reset();
  }

  //
  // Update config if needed
  //
  if(shouldSaveConfig) {
    strcpy(app_config.mqtt_username, custom_mqtt_username.getValue());
    strcpy(app_config.mqtt_password, custom_mqtt_password.getValue());
    strcpy(app_config.mqtt_remote_host, custom_mqtt_remote_host.getValue());
    strcpy(app_config.mqtt_remote_port, custom_mqtt_remote_port.getValue());
    writeAppConfig(&app_config);
  }

  //
  Serial.printf("Setting MDNS service to 'brose_flipdot_v10'\n");
  if( !MDNS.begin("BROSE_FLIPDOT_V10") ) {
  } else {
    MDNS.addService("brose_flipdot_v10", "tcp", 10000);
  }

  // Sync to NTP service
  Serial.printf("Sync to time server\n");
  updateNTP();

  // Start OverTheAir update services
  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH)
      type = "sketch";
    else // U_SPIFFS
      type = "filesystem";

    // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
    Serial.println("Start updating " + type);
  });
  
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
    
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });

  ArduinoOTA.begin();

  // Debug print
  Serial.printf("OAT ready\n");
  Serial.printf("mqtt_username: %s\n", app_config.mqtt_username);
  Serial.printf("mqtt_password: %s\n", app_config.mqtt_password);
  Serial.printf("mqtt_id: %s\n", app_config.mqtt_id);
  Serial.printf("mqtt_topic RAW: %s\n", mqtt_topic_raw);
  Serial.printf("mqtt_topic MSG: %s\n", mqtt_topic_msg);
  Serial.printf("mqtt_remote_host: %s\n", app_config.mqtt_remote_host);
  Serial.printf("mqtt_remote_port: %s\n", app_config.mqtt_remote_port);

  // Set statusled to GREEN
  smartLedInit(COLOR_GREEN);

  // Initialise FSM
  initFSM(STATE_START, EV_TRUE);

}

/******************************************************************/
void loop()
/* 
short   : Arduino loop                   
inputs  :        
outputs : 
notes   :         
Version : DMK, Initial code
*******************************************************************/
{

  // Check for IP connection
  if( WiFi.status() == WL_CONNECTED) {

    // Check for OTA firmware updates
    ArduinoOTA.handle();

    // Handle mqtt
    if( !mqttClient.connected() ) {
      mqtt_connect();
      delay(250);
    } else {
      // Handle MQTT loop
      mqttClient.loop();
    }
  } 

  // 
  // Sync time with NTP
  //
  if( WiFi.status() == WL_CONNECTED) {
    ntp_update_cur = millis();
    uint32_t ntp_update_elapsed = ntp_update_cur - ntp_update_prev;
    if( ntp_update_elapsed >= NTP_UPDATE_INTERVAL_SEC ) {
      ntp_update_prev = ntp_update_cur; 
      updateNTP();
    }
  }

  // 
  // Handle heartbeat (Ticker.h causes crashes)
  //
  heartbeat_cur = millis();
  uint32_t heartbeat_elapsed = heartbeat_cur - heartbeat_prev;
  if( heartbeat_elapsed >= HEARTBEAT_UPDATE_INTERVAL_SEC ) {
    
    //
    heartbeat_prev = heartbeat_cur; 
    
    // Call the heartbeat fp
    if( fsm[state][event].heartbeat != NULL) {
      fsm[state][event].heartbeat() ;
    } 
  }

  // 
  // Handle (display)refresh (Ticker.h causes crashes)
  //
  refresh_cur = millis();
  uint32_t refresh_elapsed = refresh_cur - refresh_prev;
  if( refresh_elapsed >= REFRESH_UPDATE_INTERVAL_SEC ) {
    
    //
    refresh_prev = refresh_cur; 

    // Call the refresh fp
    if( fsm[state][event].refresh != NULL) {
      fsm[state][event].refresh() ;
    } 
  }
}

/******************************************************************
*
* MQTT section
*
******************************************************************/

/******************************************************************/
void mqtt_callback(char* topic, byte* payload, unsigned int length)
/* 
short   : mqtt callback                  
inputs  :        
outputs : 
notes   :         
Version : DMK, Initial code
*******************************************************************/
{
  // 
  // Raw Message (pixels)
  // 
  if( 0 == strcmp(topic, mqtt_topic_raw) ) {
//
//    //
//    uint8_t buf[ DISPLAY_WIDTH * DISPLAY_HEIGHT/8 ];
//    uint8_t *pbuf = buf;
//    
//    DynamicJsonBuffer jsonBuffer(2048);
//    //StaticJsonBuffer<2048> jsonBuffer;
//    JsonObject& root = jsonBuffer.parseObject(payload);
//    if( root.success() ) {
//      JsonArray& pixels = root["pixels"]; 
//      for( JsonArray::iterator it = pixels.begin(); it!=pixels.end(); ++it) {
//        *pbuf++ = it->as<uint8_t>();
//      }
//    }
//    flipdot.directDrawBuffer(buf);
//    flipdot.display();
//
//    // Flash the pixel
//    smartLedFlash(colorFlashLevel);
  }

  // 
  // Normal Text
  // 
  if( 0 == strcmp(topic, mqtt_topic_msg) ) {

    DynamicJsonDocument jsonDocument(2048);
    DeserializationError error = deserializeJson(jsonDocument, payload);

    Serial.printf("**%s\n",error.c_str());
    
    if( !error ) {

      // Test for flipdot message
      JsonVariant msg = jsonDocument["message"];

      if(!msg.isNull()) {

        // Parse JSON  
        JsonVariant json_content = msg["content"];
          if(!json_content.isNull()) {
            strcpy(bfd_message.content, json_content.as<const char*>());
          } else {
            strcpy(bfd_message.content, "--()--");
        }

        JsonVariant json_pos_x = msg["x"];
        if(!json_pos_x.isNull()) {
          bfd_message.x = json_pos_x.as<uint8_t>();
        } else {
          bfd_message.x = 0;
        }

        JsonVariant json_pos_y = msg["y"];
        if(!json_pos_y.isNull()) {
          bfd_message.y = json_pos_y.as<uint8_t>();
        } else {
          bfd_message.y = 0;
        }

        JsonVariant json_font = msg["font"];
        bfd_message.font = 0;
        if(!json_font.isNull()) {
          bfd_message.font = json_font.as<uint8_t>();
        } else {
          bfd_message.font = 0;
        }

        JsonVariant json_display_time = msg["display_time"];
        if(!json_display_time.isNull()) {
          bfd_message.display_time = json_display_time.as<uint16_t>();
        } else {
          bfd_message.display_time = 20;
        }
      }

      // Test for flipdot command
      JsonVariant cmd = jsonDocument["backlight"];
      if(!cmd.isNull()) {

        // Test for BACKLIGHT ON/OFF
        JsonVariant json_on = cmd["on"];
        if(!json_on.isNull()) {
          if( json_on.as<bool>() == true ) {
            bfd_backlight.on = true;
          } else {
            bfd_backlight.on = false;
          }
        }

        // Test for BACKLIGHT AutoOff ON/OFF
        JsonVariant json_autoOff = cmd["autoOff"];
        if(!json_autoOff.isNull()) {
          if( json_autoOff.as<bool>() == true ) {
            bfd_backlight.autoOff = true;
          } else {
            bfd_backlight.autoOff = false;          
          }
        }
      }
    
      // Flash the pixel
      smartLedFlash();

      // Raise MQTT mode
      raiseEvent(EV_MQTT);
    }
  }
}


/******************************************************************
*
* FSM section
*
******************************************************************/

/******************************************************************/
void initFSM(ENUM_STATE new_state, ENUM_EVENT new_event)
/* 
short:         
inputs:        
outputs: 
notes:         
Version :      DMK, Initial code
*******************************************************************/
{
  // Set start state
  state = new_state;
  event = new_event;

  // and call event.pre
  if( fsm[state][event].pre != NULL) {
    fsm[state][event].pre() ;
  } 
}
 
/******************************************************************/
void raiseEvent(ENUM_EVENT new_event)
/* 
short:         
inputs:        
outputs: 
notes:         
Version :      DMK, Initial code
*******************************************************************/
{
  // call event.post
  if( fsm[state][event].post != NULL) {
    fsm[state][event].post() ;
  } 
  
  // Set new state
  ENUM_STATE new_state = fsm[state][new_event].nextState;
  
  // call newstate ev.pre
  if( fsm[new_state][new_event].pre != NULL) {
    fsm[new_state][new_event].pre() ;
  } 
  
  // Set new state
  state = new_state;
  
  // Store new event
  event = new_event;
}

/******************************************************************
*
* FSM callbacks section
*
******************************************************************/

/******************************************************************/
void start_pre(void){
  Serial.print("START: pre() ...\n");

  // Display important info at startup (on flipdot)
  bl_on();
  flipdot_display_powerup();
  bl_off();
  
  // Enter idle mode
  raiseEvent(EV_IDLE);
}

/******************************************************************/
void start_heartbeat(void){
  Serial.print("START: heartbeat ...\n");
}

/******************************************************************/
void start_refresh(void){
  Serial.print("START: refresh ...\n");
}

/******************************************************************/
void start_post(void){
  Serial.print("START: post() ...\n");
}

/******************************************************************/
void idle_pre(void){
  //Serial.print("IDLE: pre() ...\n");
}

/******************************************************************/
void idle_heartbeat(void){
  //Serial.print("IDLE: heartbeat ...\n");
  flipdot_display_idle();
}

/******************************************************************/
void idle_refresh(void){
  Serial.print("IDLE: refresh ...\n");
  flipdot_display_refresh();
}

/******************************************************************/
void idle_post(void){
  Serial.print("IDLE: post() ...\n");
}

/******************************************************************/
void mqtt_pre(void){
  Serial.print("MQTT: pre() ...\n");

  Serial.printf("(x,y): (%d,%d)\n", bfd_message.x, bfd_message.y);
  Serial.printf("font : %d\n", bfd_message.font);
  Serial.printf("backlight On : %d\n", bfd_backlight.on);
  Serial.printf("backlight AutoOff : %d\n", bfd_backlight.autoOff);
  Serial.printf("%s\n", bfd_message.content);
  Serial.printf("display time: %d\n", bfd_message.display_time);


  // Handle font and position, display message
  flipdot.clearDisplay();
  flipdot.setTextColor(BLACK);
  flipdot.setTextSize(1);
  switch(bfd_message.font) {
    default:
    case 0: 
      flipdot.setFont();
      flipdot.setCursor(bfd_message.x, bfd_message.y);
      break;
    case 1: 
      flipdot.setFont(&FlipDot7x8);        
      flipdot.setCursor(bfd_message.x, bfd_message.y+6);
      break;
    case 2: 
      flipdot.setFont(&FlipDot16x10);
      flipdot.setCursor(bfd_message.x, bfd_message.y+15);
      break;
  }

  flipdot.println(bfd_message.content);
  flipdot.display();

  if(true == bfd_backlight.on) {
    bl_on();
  } else {
    bl_off();
  }
}

/******************************************************************/
void mqtt_heartbeat(void){
  Serial.print("MQTT: heartbeat ...\n");

  if( bfd_message.display_time > 0 ) {
    bfd_message.display_time--;
  } else {
    raiseEvent(EV_IDLE);
  }
}

/******************************************************************/
void mqtt_refresh(void){
  Serial.print("MQTT: refresh ...\n");
}

/******************************************************************/
void mqtt_post(void){
  Serial.print("MQTT: post() ...\n");
  bl_off();
}

/******************************************************************/
/*
 * Flipdot section
 */
/******************************************************************/

/******************************************************************/
void flipdot_display_powerup()
/* 
short:         
inputs:        
outputs: 
notes:         
Version :      DMK, Initial code
*******************************************************************/
{
  flipdot.clearDisplay();
  flipdot.display();
  flipdot.setTextSize(1);
  flipdot.setTextColor(BLACK);

  char str[40];

  // Print IP
  flipdot.clearDisplay();
  flipdot.setCursor(0,0);
  sprintf(str, "ip:\n%s", WiFi.localIP().toString().c_str());  
  flipdot.println(str);
  flipdot.display();
  Serial.printf("%s\n", str);
  delay(1500);

  // Print MQTT host
  flipdot.clearDisplay();
  flipdot.setCursor(0,0);
  sprintf(str, "mqtt:\n%s", app_config.mqtt_remote_host);
  flipdot.println(str);
  flipdot.display();
  Serial.printf("%s\n", str);
  delay(1500);

  // Print UUID = part of mqtt topic
  flipdot.clearDisplay();
  flipdot.setCursor(0,0);
  sprintf(str, "UUID:\n%s", app_config.mqtt_topic);
  flipdot.println(str);
  flipdot.display();
  Serial.printf("%s\n", str);
  delay(2500);

  // Show flipdot image
  flipdot.clearDisplay();
  flipdot.drawBitmap(0, 0, ilovedots, 112, 16, 1);
  flipdot.display();
  delay(2500);
}

/******************************************************************/
void flipdot_display_refresh()
/* 
short:         
inputs:        
outputs: 
notes:         
Version :      DMK, Initial code
*******************************************************************/
{
  flipdot.invertDisplay();
  flipdot.display();

  flipdot.invertDisplay();
  flipdot.display();
}


/******************************************************************/
void flipdot_display_idle()
/* 
short:      Update flipdot
inputs:        
outputs: 
notes:         
Version :   DMK, Initial code
*******************************************************************/
{
  char buf[40];
  char *dstAbbrev;
  time_t t = dstAdjusted.time(&dstAbbrev);
  struct tm *timeinfo = localtime (&t);
  
  flipdot.clearDisplay();
  flipdot.setTextColor(BLACK);

  // Set Time in BIG font
  flipdot.setFont(&FlipDot16x10);
  flipdot.setTextSize(1);
  flipdot.setCursor(0, 15);
  //sprintf(buf, "%d:%02d",timeinfo->tm_hour, timeinfo->tm_min);
  strftime(buf, 40, "%R", timeinfo);
  flipdot.println(buf);

  // Set DOW in SMALL font
  flipdot.setFont();
  flipdot.setTextSize(1);
  flipdot.setCursor(64, 0);
  strftime(buf, 40, "%a %z", timeinfo);
  flipdot.println(buf);
  
  // Set Date in SMALL font
  flipdot.setFont();
  flipdot.setTextSize(1);
  flipdot.setCursor(64, 9);
  strftime(buf, 40, "%D", timeinfo);
  flipdot.println(buf);

  // Display all
  flipdot.display();

  // Handle Backlight. Always turn off except 
  // when backlightAutoOff == false;
  if( backlightAutoOff == true ) {
    bl_off();
  }
}

#ifdef SECURE
/******************************************************************/
void mqttConnectAndVerify_tls() 
/* 
short:      Connect to MQTT server and verify signed cert sha-1 
inputs:        
outputs: 
notes:         
Version :   DMK, Initial code
*******************************************************************/
{
//  Serial.print("connecting to ");
//  Serial.println(mqttServer);
//  if (!wifiClient.connect(mqttServer, mqttPort)) {
//    Serial.println("connection failed");
//    return;
// }
//
//  //if (wifiClient.verify(fingerprint, mqtt_server.toString().c_str())) {
//  if (wifiClient.verify(sha1_fingerprint, mqttServer)) {
//    Serial.println("certificate matches");
//  } else {
//    Serial.println("certificate doesn't match");
//  }
}
#endif

#ifndef SECURE

/******************************************************************/
void mqtt_connect() 
/* 
short:      Connect to MQTT server UNSECURE
inputs:        
outputs: 
notes:         
Version :   DMK, Initial code
*******************************************************************/
{
  char *host = app_config.mqtt_remote_host;
  int port = atoi(app_config.mqtt_remote_port);
  
  mqttClient.setClient(wifiClient);
  mqttClient.setServer(host, port);
  if(mqttClient.connect(app_config.mqtt_id)){

    // Subscribe to ../raw and ../msg
    mqttClient.subscribe(mqtt_topic_raw);
    mqttClient.subscribe(mqtt_topic_msg);

    // Set callback
    mqttClient.setCallback(mqtt_callback);
    Serial.printf("%s: MQTT connected to %s:%d\n", __FUNCTION__, host, port);
  } else {
    Serial.printf("%s: MQTT connection ERROR (%s:%d)\n", __FUNCTION__, host, port);
  }
}
#endif

/******************************************************************/
void create_unique_mqtt_topic_string(char *topic_string)
/* 
short:      Construct unique mqtt_signature    
inputs:        
outputs: 
notes:         
Version :   DMK, Initial code
*******************************************************************/
{
   char tmp[30];
   strcpy(topic_string,"BFD01");
   sprintf(tmp,"-%06X",ESP.getChipId());
   strcat(topic_string,tmp);
   sprintf(tmp,"-%06X",ESP.getFlashChipId()); 
   strcat(topic_string,tmp);
}

/******************************************************************/
void create_unigue_mqtt_id(char *signature)
/* 
short:      Construct unique mqtt_signature    
inputs:        
outputs: 
notes:         
Version :   DMK, Initial code
*******************************************************************/
{
   char tmp[30];
   strcpy(signature,"BroseFlipdot");
   strcat(signature,"-v01");
   sprintf(tmp,"-%06X",ESP.getChipId());
   strcat(signature,tmp);
   sprintf(tmp,"-%06X",ESP.getFlashChipId()); 
   strcat(signature,tmp);
}

/******************************************************************/
void updateNTP(){
/* 
short:      Sync to NTP server    
inputs:        
outputs: 
notes:         
Version :   DMK, Initial code
*******************************************************************/
  configTime(timezone * 3600, 0, NTP_SERVERS);
  delay(500);
  while (!time(nullptr)) {
    delay(500);
  }
}

/******************************************************************/
void printTime(time_t offset)
/* 
short:         
inputs:        
outputs: 
notes:         
Version :   DMK, Initial code
*******************************************************************/
{
  char buf[30];
  char *dstAbbrev;
  time_t t = dstAdjusted.time(&dstAbbrev)+offset;
  struct tm *timeinfo = localtime (&t);
  
  int hour = (timeinfo->tm_hour+11)%12+1;  // take care of noon and midnight
  sprintf(buf, "%02d/%02d/%04d %02d:%02d:%02d%s %s\n",timeinfo->tm_mon+1, timeinfo->tm_mday, timeinfo->tm_year+1900, hour, timeinfo->tm_min, timeinfo->tm_sec, timeinfo->tm_hour>=12?"pm":"am", dstAbbrev);
  Serial.print(buf);
}

/******************************************************************/
/*
 * Smart LED section
 */
/******************************************************************/

/******************************************************************/
void smartLedShowColor(uint32_t color)
/* 
short:         
inputs:        
outputs: 
notes:         
Version :      DMK, Initial code
*******************************************************************/
{
  neoPixel.setPixelColor(0, color);
  neoPixel.show();
}

/******************************************************************/
void smartLedFlash()
/* 
short:      Flash current color         
inputs:        
outputs: 
notes:         
Version :   DMK, Initial code
*******************************************************************/
{
  neoPixel.setBrightness(COLOR_FLASH_BRIGHTNESS);
  neoPixel.show();
  delay(50);
  neoPixel.setBrightness(COLOR_IDLE_BRIGHTNESS);
  neoPixel.show();
}

/******************************************************************/
void smartLedInit( uint32_t color)
/* 
short:      Init         
inputs:        
outputs: 
notes:         
Version :   DMK, Initial code
*******************************************************************/
{
  neoPixel.begin();
  delay(100);
  neoPixel.clear();
  neoPixel.setBrightness(COLOR_IDLE_BRIGHTNESS);
  neoPixel.setPixelColor(0, color);
  neoPixel.show();
}

/******************************************************************/
/*
 * Backlight function
 */
/******************************************************************/

/******************************************************************/
void bl_init(void)
/* 
short:      Init backlight (lightstrip or TL in original Brose FP)        
inputs:        
outputs: 
notes:      
Version :   DMK, Initial code
*******************************************************************/
{
  pinMode(BL_PIN, OUTPUT);
  bl_off();
  backlightAutoOff = true;
}


/******************************************************************/
void bl_on(void)
/* 
short:      turn BL on         
inputs:        
outputs: 
notes:         
Version :   DMK, Initial code
*******************************************************************/
{
  digitalWrite(BL_PIN, HIGH);
}

/******************************************************************/
void bl_off(void)
/* 
short:      turn BL off
inputs:        
outputs: 
notes:         
Version :   DMK, Initial code
*******************************************************************/
{
  digitalWrite(BL_PIN, LOW);
}


/******************************************************************/
/*
 * Application signature and config
 */
/******************************************************************/

/******************************************************************/
void saveConfigCallback () 
/* 
short:         
inputs:        
outputs: 
notes:         
Version :      DMK, Initial code
*******************************************************************/
{
   shouldSaveConfig = true;
}

/******************************************************************/
bool readAppConfig(APP_CONFIG_STRUCT *app_config) 
/* 
short:         loop(), runs forever executing FSM
inputs:        
outputs: 
notes:         
Version :      DMK, Initial code
*******************************************************************/
{
   bool retval = false;

   if( SPIFFS.begin() ) {
      if( SPIFFS.exists("/config.json") ) {
         File file = SPIFFS.open("/config.json","r");
         if( file ) {
            size_t size = file.size();
            std::unique_ptr<char[]> buf(new char[size]);

            file.readBytes(buf.get(), size);

            //DynamicJsonBuffer jsonBuffer;
            DynamicJsonDocument json(1024);
            
            //JsonObject& json = jsonBuffer.parseObject(buf.get());
            DeserializationError error = deserializeJson(json, buf.get());
            if( !error ) {
               strcpy(app_config->mqtt_username, json["MQTT_USERNAME"]);
               strcpy(app_config->mqtt_password, json["MQTT_PASSWORD"]);
               strcpy(app_config->mqtt_remote_host, json["MQTT_HOST"]);
               strcpy(app_config->mqtt_remote_port, json["MQTT_PORT"]);
               retval = true;
            }
         }
      }
   } 
   return retval;
}

/******************************************************************/
bool writeAppConfig(APP_CONFIG_STRUCT *app_config) 
/* 
short:         Write config to FFS
inputs:        
outputs: 
notes:         
Version :      DMK, Initial code
*******************************************************************/
{
   bool retval = false;

  if( SPIFFS.begin() ) {
      
      // Delete config if exists
      if( SPIFFS.exists("/config.json") ) {
         SPIFFS.remove("/config.json");
      }

      DynamicJsonDocument doc(1024);
      doc["MQTT_USERNAME"] = app_config->mqtt_username;
      doc["MQTT_PASSWORD"] = app_config->mqtt_password;
      doc["MQTT_HOST"] = app_config->mqtt_remote_host;
      doc["MQTT_PORT"] = app_config->mqtt_remote_port;

      File file = SPIFFS.open("/config.json","w");
      if( file ) {
         //serializeJson(doc, file);
         serializeJson(doc, Serial);

         file.close();
         retval = true;
      }
   } 
   return retval;
}

/******************************************************************/
void deleteAppConfig() 
/* 
short:         Erase config to FFS
inputs:        
outputs: 
notes:         
Version :      DMK, Initial code
*******************************************************************/
{
   if( SPIFFS.begin() ) {
      
      // Delete config if exists
      if( SPIFFS.exists("/config.json") ) {
         SPIFFS.remove("/config.json");
      }
   } 
}
