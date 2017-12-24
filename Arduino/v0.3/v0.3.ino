// REVISION
// v0.3
// - Added MQTT interface
//
// v0.2
// - Filter change was read from ESP8266 instead of MCP23008
// - Swapped STATE2 with STATE3
// - Removed "Master" in naming
// - Moved interrupts to last action in setup
// - Removed legacy code
//
// v0.1
// - First release


// Include libraries //
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <EEPROM.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <SPI.h>                                                         // Required for OLED
#include <Wire.h>                                                        // Required for OLED
#include <Adafruit_GFX.h>                                                // Required for OLED
#include <Adafruit_SSD1306.h>                                            // Required for OLED
#include "Adafruit_MCP23008.h"                                           // Port Expander
// ----------------- //

// Variables
String eepromSSID;
String eepromPassword;
String eepromMQTT;
String content;
int statusCode;
bool startupMode;                                                        // false = Normal mode, true = Config mode
const char* NodeName = "Renovent HR";
const char* SSIDsetup = "RenoventHR";
MDNSResponder mdns;

//Web server
ESP8266WebServer server(80);

//MQTT
#define MQTTpublishInterval  60       // Interval in seconds to publish value
#define MQTTretryInterval    60       // Interval in seconds to retry MQTT connection
WiFiClient espClient;
PubSubClient mqttClient(espClient);
long lastMQTTmsg   = millis() + (MQTTpublishInterval * 1000);
long lastMQTTretry = millis() + (MQTTretryInterval   * 1000);


// GPIOA0 - ADC/A0                     OLED RESET (N/C)
// GPIO00 - RESERVED, must be HIGH     Boot mode (HIGH = normal, LOW = program)
// GPIO02 - RESERVED, must be HIGH     -
// GPIO04 -                            SDA
// GPIO05 -                            SCL
// GPIO12 -                            -
// GPIO13 -                            -
// GPIO14 -                            Startup mode
// GPIO15 - RESERVED, must be LOW      -
// GPIO16 - (no interrupt)             RESET pin
// GP00   -                            Relay 1
// GP01   -                            Relay 2
// GP02   -                            PIN_button_up
// GP03   -                            PIN_button_down
// GP04   -                            Filter indicator 
// GP05   -                            - 
// GP06   -                            - 
// GP07   -                            - 
//GPIO on ESP8266
#define PIN_OLED_RESET    20           // Something which isn't connected
#define PIN_PE_int        12           // Interrupt for port extender
#define PIN_startupmode   14           // Startup mode (setup or normal)
#define PIN_reset         16           // Connected to reset pin
//Port extender
#define PIN_relay1         0            // On port expander 
#define PIN_relay2         1            // On port expander
#define PIN_button_up      2            // Button up
#define PIN_button_down    3            // Button down
#define PIN_FilterChange   4            // Filter change indicator

//EEPROM
#define eepromSSIDstart    0            //  0 - 31  = SSID
#define eepromSSIDend     31            //
#define eepromPASSstart   32            // 32 - 95  = Passprhase
#define eepromPASSend     95            //
#define eepromWTWaddr     96            // 96       = wtwState
#define eepromMQTTstart   97            // 97 - 352 = MQTT broker
#define eepromMQTTend    352            //


// OLED 0.96 128x64 i2C (at 0x3C) //
Adafruit_SSD1306 display(PIN_OLED_RESET);
// ------------------------------ //

// Port expander MCP23008 //
Adafruit_MCP23008 mcp;
// ---------------------- //


// WTW state //
volatile int currentState;
volatile int changeFilter = false;
volatile int buttonState = 0;
#define buttonStateMin 0
#define buttonStateMax 3
// --------------- //



void setup() {

  // Begin Serial
  Serial.begin(115200);
  Serial.println("");
  Serial.println("Booting...");
  
  // OLED
  Serial.print(F("OLED..."));
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);                          // Initialize with the I2C addr 0x3C (for the 128x32)
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.println(NodeName);
  display.println(F(""));
  display.print(F("   ...starting..."));
  display.display();
  delay(2000);
  Serial.println(F("initialized")); delay(100);


  // Setup pins
  Serial.print(F("GPIO...")); delay(100);
  mcp.begin();                                                         // Start port expander on default address 
  pinMode(PIN_reset, OUTPUT);
  digitalWrite(PIN_reset, HIGH);                                      // Pull-up resistor
  pinMode(PIN_PE_int, INPUT);
  pinMode(PIN_startupmode, INPUT);                                    // Determine startup mode 
  mcp.pinMode(PIN_relay1, OUTPUT);
  mcp.pinMode(PIN_relay2, OUTPUT);
  mcp.pinMode(PIN_button_up, INPUT);                                  // Button up
  mcp.pinMode(PIN_button_down, INPUT);                                // Button down
  mcp.pinMode(PIN_FilterChange, INPUT);                               // Filter change indicator


  mcp.digitalWrite (PIN_relay1, HIGH);
  delay(2000);
  mcp.digitalWrite (PIN_relay1, LOW);
  Serial.println("done");
  
  // Determine startup mode
  Serial.print(F("Booting in "));
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  startupMode = (bool)digitalRead(PIN_startupmode);
  if (startupMode) {
    Serial.println(F("SETUP mode"));  display.println(F("SETUP mode"));
  } else {
    Serial.println(F("NORMAL mode")); display.println(F("NORMAL mode"));
  }
  display.display();


  // EEPROM
  EEPROM.begin(4096);
  Serial.println(F("Reading memory (EEPROM)"));
  for (int i = eepromSSIDstart; i <= eepromSSIDend; ++i) {
    //Serial.println(EEPROM.read(i));
    if (EEPROM.read(i) == 0) {
      break;
    }
    if (EEPROM.read(i) == 255) {
      break;
    }
    eepromSSID += char(EEPROM.read(i));
  }
  Serial.print(F("   SSID: ")); Serial.println(eepromSSID);
  for (int i = eepromPASSstart; i <= eepromPASSend; ++i) {
    //Serial.println(EEPROM.read(i));
    if (EEPROM.read(i) == 0) {
      break;
    }
    if (EEPROM.read(i) == 255) {
      break;
    }
    eepromPassword += char(EEPROM.read(i));
  }
  Serial.print(F("   PASS: ")); Serial.println(eepromPassword);  
  for (int i = eepromMQTTstart; i <= eepromMQTTend; ++i) {
    //Serial.println(EEPROM.read(i));
    if (EEPROM.read(i) == 0) {
      break;
    }
    if (EEPROM.read(i) == 255) {
      break;
    }
    eepromMQTT += char(EEPROM.read(i));
  }
  Serial.print(F("   MQTT broker: ")); Serial.println(eepromMQTT);  

  

   
  // Start WiFi
  if (startupMode) {
    display.print(F("SSID: "));
    display.println(SSIDsetup);
  } else {
    if (eepromSSID == "") {
      Serial.println(F("No SSID found!"));
      display.println(F("No SSID found!"));
      Serial.println(F("HALTING"));
      display.println(F("HALTING"));
      display.display();
      return;
    }
    display.println(F("Connecting to AP..."));
  }
  display.display();
  setupWIFI(startupMode);


  // Determine IP
  if (startupMode) {
    // Read IP address
    Serial.print(F("SoftAP IP: ")); Serial.println(WiFi.softAPIP());
    display.print(F("IP  : "));
    display.println(WiFi.softAPIP());
  } else {
    // Read IP address
    Serial.print(F("Local IP: ")); Serial.println(WiFi.localIP());

    // Setup mDNS
    Serial.print(F("Setup mDNS..."));
    if (mdns.begin("RenoventHR", WiFi.localIP())) {
      Serial.println(" OK");
    } else {
      Serial.println(" FAIL");
    }
  }
  display.display();

  // MQTT
  if (!startupMode) {
     if (eepromMQTT != "") {
        randomSeed(micros());
        Serial.print(F("Setup MQTT..."));
        mqttClient.setServer(eepromMQTT.c_str(), 1883);
        mqttClient.setCallback(MQTTcallback);
        Serial.println(F("done"));
     }
  }
  // Create webserver
  Serial.print(F("Create webserver..."));
  if (!startupMode) {
    display.println(F("Create webserver"));
    display.display();
  }
  createWebServer(startupMode);
  Serial.println(F("done"));


  // Start the server
  Serial.print(F("Start webserver..."));
  server.begin();
  Serial.println(F("done"));


  //WTW state and Change Filter
  if (startupMode == 0) {
    display.println(F("Configure WTW"));  display.display();
    int wtwState = EEPROM.read(eepromWTWaddr);
    if (wtwState == 255) {
      wtwState = 0;
    }
    doFilterChange();
    updateWTWstate(wtwState);                      // Push initial state to the WTW device (stored in EEPROM)
  }


  //Update display
  if (startupMode) {
    display.println(F("Webserver is ready"));  display.display();
    display.invertDisplay(true);
  } else {
    delay(2000);
    updateDisplay();
  }


   //Interrupts
  Serial.print(F("Interrupts..."));
  if (startupMode == 0) {
    attachInterrupt(PIN_PE_int, doPortExtender, FALLING);
    mcp.intMode(PIN_button_up, 1);    // RISING
    mcp.intMode(PIN_button_down, 1);  // RISING      
    mcp.intMode(PIN_FilterChange, 1); // RISING  
  }
  Serial.println(F("initialized")); delay(100);
}


void reconnectMQTT() {
   // Reconnect MQTT with defined interval
   if ( millis() - lastMQTTretry > MQTTretryInterval * 1000) {
       lastMQTTretry =  millis();
       Serial.print("Attempting MQTT connection...");
    
       // Create a random client ID
       String clientId = "RenoventHR-"; 
              clientId += String(random(0xffff), HEX);
           
       // Attempt to connect
       if (mqttClient.connect(clientId.c_str())) {
           Serial.println("connected");
           
           // (Re)subscribe
           mqttClient.subscribe("RenoventHR/setWTWstate");
       } else {
          Serial.print("failed, rc=");
          Serial.println(mqttClient.state());
       }
   }
}


void loop() {
   // Handle HTTP (REST)
   server.handleClient();
   // ---           

   // Handle MQTT
   if (!startupMode) {
    
     // Connect MQTT if necessary
     if (!mqttClient.connected()) { reconnectMQTT(); }

     // Only receive/send if MQTT is actually connected
     if (mqttClient.connected()) {
       
        // Receive 
        mqttClient.loop();

        // Publish new values on defined interval
        if ( millis() - lastMQTTmsg > MQTTpublishInterval * 1000) {
          lastMQTTmsg =  millis();
          Serial.println(F("Publish MQTT message"));
          mqttClient.publish("RenoventHR/wtwState", String(currentState).c_str());
          mqttClient.publish("RenoventHR/changeFilter", String(changeFilter).c_str());
        }
     }
  }
  // ---

  // Reboot device is startup mode is changed
  if ((bool)digitalRead(PIN_startupmode) != startupMode) doStartupMode(); 
}



void MQTTcallback(char* topic, byte* payload, unsigned int length) {
   String strTopic = topic;
          strTopic.toLowerCase();
   String strValue;
          
   Serial.print("Message arrived [");
   Serial.print(topic);
   Serial.print("] : ");
   
   for (int i = 0; i < length; i++) {
     Serial.print((char)payload[i]);
     strValue+=(char)payload[i];
   }
   Serial.println();
   
   if (strTopic == "renoventhr/setwtwstate") {
      currentState = strValue.toInt();
      updateWTWstate(currentState);
   } 
}



void doStartupMode() {
   // An intterupt is received that the value is changed
   Serial.println(F("Startup mode changed. Restart device."));
   display.clearDisplay();
   display.setTextSize(1);
   display.setTextColor(WHITE);
   display.setCursor(0, 0);
   display.println(F("Startup mode CHANGED"));
   display.println(F(""));
   display.setTextSize(2);
   display.print("RESTARTING");
   display.display();

   //Restart the device
   ESP.deepSleep(500000); // .5 sec
}



void updateDisplay() {
  //Clear
  display.clearDisplay();

  //Header
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.println(NodeName);

  //WTW state
  display.setTextSize(2);
  display.print("     ");
  if (currentState == 0) {
    display.write(15);
  } else {
    display.println(currentState);
  }

  //Change Filter
  display.setTextSize(1);
  if (changeFilter == 1) {
    display.setTextColor(BLACK);
    display.fillRect(78, 8, 50, 25, WHITE);
    display.println("        ");
    display.setCursor(80, 11);
    display.println(" CHANGE ");
    display.setCursor(80, 21);
    display.println(" FILTER ");
  }

  //IP address
  display.setTextColor(WHITE);
  display.setCursor(0, 25);
  display.println(WiFi.localIP());

  //Show content on LCD
  display.display();
}



void setupWIFI(int webtype) {
  //Determine startup mode
  if (webtype)
  {

    // -- Setup mode --
    // Confiure WiFi
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

     // Start WiFi as accesspoint
    Serial.print("Start accesspoint...");
    WiFi.softAP(SSIDsetup);
    Serial.println("done");

  } else {
    //-- Normal mode --
   
    // Connect to WiFi access point
    Serial.print("Connect to access point " + eepromSSID);
    WiFi.begin(eepromSSID.c_str(), eepromPassword.c_str());
    if (testWifi()) {
      Serial.println(" OK");
    } else {
      Serial.println(" FAIL");
    }
  }
}


bool testWifi(void) {
  int c = 0;
  while ( c < 20 ) {
    Serial.print(".");
    if (WiFi.status() == WL_CONNECTED) {
      return true;
    }
    delay(500);
    c++;
  }
  return false;
}


void createWebServer(int webtype)
{
  //Determine startup mode
  if (!webtype) {

    // -- Normal mode --
   server.on("/",[]() {
      String strPage ="<!DOCTYPE HTML>\n"
          "<meta charset=\"utf-8\">\n"
          "<meta name=\"apple-mobile-web-app-capable\" content=\"yes\" />\n"
          "<meta http-equiv=\"X-UA-Compatible\" content=\"IE=edge,chrome=1\">\n"
          "<meta name=\"viewport\" content=\"width=device-width, user-scalable=no\">\n"
          "\n"
          "<title>Renovent HR - WiFi</title>\n"
          "<link rel=\"stylesheet\" href=\"http://code.jquery.com/mobile/1.0/jquery.mobile-1.0.min.css\" />\n"
          "<link rel=\"stylesheet\" href=\"http://cdnjs.cloudflare.com/ajax/libs/font-awesome/4.4.0/css/font-awesome.min.css\">\n"
          "<script src=\"http://code.jquery.com/jquery-1.6.4.min.js\"></script>\n"
          "<script src=\"http://code.jquery.com/mobile/1.0/jquery.mobile-1.0.min.js\"></script>\n"
          "<style class=\"cp-pen-styles\">@import url(http://fonts.googleapis.com/css?family=Roboto:400);\n"
          "* {\n"
          "  margin: 0;\n"
          "  padding: 0;\n"
          "  box-sizing: border-box;\n"
          "}\n"
          "\n"
          "body {\n"
          "  background-color: #212121;\n"
          "  text-rendering: auto;\n"
          "  -webkit-font-smoothing: antialiased;\n"
          "  font-family: Roboto;\n"
          "}\n"
          "\n"
          ".text-wrapper {\n"
          "  position: absolute;\n"
          "  width: 100vw;\n"
          "  bottom: 20px;\n"
          "  text-align: center;\n"
          "}\n"
          ".text-wrapper p {\n"
          "  font-size: 15px;\n"
          "  color: #555;\n"
          "}\n"
          ".text-wrapper p a {\n"
          "  text-decoration: none;\n"
          "  color: #03a9f4;\n"
          "}\n"
          "\n"
          ".main-wrapper {\n"
          "  display: table;\n"
          "  margin: 0px auto 0;\n"
          "}\n"
          ".main-wrapper .buttons-wrapper {\n"
          "  display: table-cell;\n"
          "  vertical-align: middle;\n"
          "  height: 59px;\n"
          " width: 330px;\n"
          "  border-radius: 7px;\n"
          "  background-color: black;\n"
          "  border: 2px solid black;\n"
          "  box-shadow: 0 4px 8px rgba(0, 0, 0, 0.55);\n"
          "}"
          ".main-wrapper .buttons-wrapper:after {"
          "  content: \"\";"
          "  display: block;"
          "  clear: both;"
          "}"
          ".main-wrapper .buttons-wrapper .button {"
          "  position: relative;"
          "  z-index: 1;"
          "  float: left;"
          "  padding: 10px 15px;"
          "  background-image: linear-gradient(#333, #222);"
          "  text-align: center;"
          "  margin: 0 1px;"
          "  border-radius: 2px;"
          "  border-top: 1px solid rgba(255, 255, 255, 0.25);"
          "  border-left: 1px solid rgba(255, 255, 255, 0.05);"
          "  border-right: 1px solid rgba(255, 255, 255, 0.05);"
          "  box-shadow: inset 0 1px 0 rgba(0, 0, 0, 0.1);"
          "  cursor: pointer;"
          "  transition: all 0.175s ease;"
          "}"
          ".main-wrapper .buttons-wrapper .button:first-child {"
          "  border-top-left-radius: 5px;"
          "  border-bottom-left-radius: 5px;"
          "  margin-left: 0;"
          "}"
          ".main-wrapper .buttons-wrapper .button:first-child.selected:before {"
          "  display: none;"
          "}"
          ".main-wrapper .buttons-wrapper .button:last-child {"
          "  border-top-right-radius: 5px;"
          "  border-bottom-right-radius: 5px;"
          "  margin-right: 0;"
          "}"
          ".main-wrapper .buttons-wrapper .button:last-child.selected:after {"
          "  display: none;"
          "}"
          ".main-wrapper .buttons-wrapper .button:hover {"
          "  z-index: 2;"
          "  background-image: linear-gradient(#373737, #262626);"
          "  box-shadow: inset 0 1px 0 rgba(0, 0, 0, 0.1), 0 4px 16px rgba(0, 0, 0, 0.5);"
          "}"
          ".main-wrapper .buttons-wrapper .button .fa {"
          "  color: #424242;"
          "  text-shadow: 0 -1px 0 rgba(0, 0, 0, 0.75);"
          "  font-size: 28px;"
          "  width: 32px;"
          "}"
          ".main-wrapper .buttons-wrapper .button.selected {"
          " z-index: 3;"
          " cursor: default;"
          " background-image: linear-gradient(#202020, #151515);"
          " border-top-color: transparent;"
          " border-left-color: rgba(0, 0, 0, 0.55);"
          " border-right-color: rgba(0, 0, 0, 0.55);"
          " box-shadow: inset 0 1px 6px rgba(0, 0, 0, 0.5), 0 10px 20px rgba(255, 255, 255, 0.06);"
          "}"
          "main-wrapper .buttons-wrapper .button.selected:after, .main-wrapper .buttons-wrapper .button.selected:before {"
          " content: \"\";"
          " display: block;"
          " position: absolute;"
          " top: 0;"
          " width: 1px;"
          " height: 100%;"
          " background-image: linear-gradient(rgba(0, 0, 0, 0.25), rgba(2, 165, 238, 0.4), rgba(0, 0, 0, 0.25));"
          "}"
          ".main-wrapper .buttons-wrapper .button.selected:before {"
          " left: -4px;"
          "}"
          ".main-wrapper .buttons-wrapper .button.selected:after {"
          "  right: -4px;"
          "}"
          ".main-wrapper .buttons-wrapper .button.selected .fa {"
          "  color: white;"
          "  text-shadow: 0 0 10px rgba(2, 165, 238, 0.75);"
          "}"
          ".main-wrapper .buttons-wrapper .button.selected .fa:after {"
          "  content: \"\";"
          "  display: block;"
          "  position: absolute;"
          "  bottom: -3px;"
          "  left: 0;"
          " width: 80px;"
          "  height: 1px;"
          "  background-image: linear-gradient(to right, rgba(255, 255, 255, 0), rgba(255, 255, 255, 0.045), rgba(255, 255, 255, 0));"
          "}"
          "</style>\n"     
          "<SCRIPT>\n"
          "var xmlHttp=createXmlHttpObject();\n"
          "var wtwState;\n"
          "function createXmlHttpObject(){\n"
          " if(window.XMLHttpRequest){\n"
          "    xmlHttp=new XMLHttpRequest();\n"
          " }else{\n"
          "    xmlHttp=new ActiveXObject('Microsoft.XMLHTTP');\n"
          " }\n"
          " return xmlHttp;\n"
          "}\n"
          "\n"
          "function process(){\n"
          " if(xmlHttp.readyState==0 || xmlHttp.readyState==4){\n"
          "   xmlHttp.open('get','/status',true);\n"
          "   xmlHttp.onreadystatechange=handleServerResponse;\n" 
          "   xmlHttp.send(null);\n"
          " }\n"
          " setTimeout('process()',1000);\n"      // Update every 1000ms
          "}\n"
          "function handleServerResponse(){\n"
          " if(xmlHttp.readyState==4 && xmlHttp.status==200){\n"
          "   jsonResponse = JSON.parse(xmlHttp.responseText);\n"
          "\n"      
          "   if (jsonResponse.wtwState != wtwState) {\n"
          "      wtwState = jsonResponse.wtwState;\n"
          "      document.getElementById('0').className = ('button');\n"
          "      document.getElementById('1').className = ('button');\n"
          "      document.getElementById('2').className = ('button');\n"
          "      document.getElementById('3').className = ('button');\n"
          "      document.getElementById(parseInt(wtwState)).className = ('button selected');\n"
          "   }\n"
          "\n"
          "   if (jsonResponse.changeFilter==1){\n"
          "      document.getElementById('changeFilter').innerHTML=\"<font color=white>You need to </font><b><font color=red>change the filter</font></b><font color=white>!</font><br><br>\";\n"
          "   } else {\n"
          "      document.getElementById('changeFilter').innerHTML=\"<br><br>\";\n"
          "   }\n"
          " }\n"
          "}\n"
          "</SCRIPT>\n"
          "\n"
          "<BODY onload='process()'>\n"
          "<div data-role=\"header\" data-position=\"inline\">\n"
          "<h1>Renovent HR</h1>\n"
          "</div>\n"
          "<div class=\"ui-body ui-body-a\">\n"
          "<BR>Using this webpage you can change the state of the Renovent HR.<BR><BR>\n"
          "<center><b><P STYLE=\"font-size: 18pt;\">Current state</p><b/></center>\n"
          "<div class='main-wrapper'>\n"
          "  <div class='buttons-wrapper'>\n"
          "    <div name=wtwState id=0 class='button selected'><i class='fa fa-gear'></i></div>\n"
          "    <div name=wtwState id=1 class='button         '><i class='fa'>1</i></div>\n"
          "    <div name=wtwState id=2 class='button         '><i class='fa'>2</i></div>\n"
          "    <div name=wtwState id=3 class='button         '><i class='fa'>3</i></div>\n"
          "  </div>\n"
          "</div>\n"
          "<BR>\n"
          "<p class=\"blinking\"><A id='changeFilter'></A></p>\n"
          "<BR><BR><BR>\n"
          "</div>\n"
          "<script src='//cdnjs.cloudflare.com/ajax/libs/jquery/2.1.3/jquery.min.js'></script>\n" 
          "<script>\n"
          "$('.button').click(function(){\n" 
          "  $el = $(this);\n"
          "  if ($el.hasClass('selected')) {\n"
          "  } else {\n"
          "    $el.siblings().removeClass('selected');\n"
          "    $el.addClass('selected');\n"
          "    $.post('/newState', { wtwtState: this.id } );\n"        // HTTP POST to /wtwState with the new wtwState
          "  }\n"
          "});\n"
          "\n"
          "function blinker(){\n"
          "  $('.blinking').fadeOut(1000);\n"
          "  $('.blinking').fadeIn(1000);\n"
          "}\n"
          "setInterval(blinker, 2000);\n"
          "</SCRIPT>\n"
          "</BODY>\n"
          "</HTML>\n";
      server.send(200,"text/html",strPage);
   });

    // Address: /newState
    server.on("/newState", [](){
       int wtwState = server.arg("wtwtState").toInt();
       updateWTWstate(wtwState);
       server.send(200,"text/html","");
    });
    
    // Address: /status
    server.on("/status", []() {
      server.send(200, "application/json", getStatus());
    });
    
    // Address: /changeState
    server.on("/changeState", []() {
      int wtwState = server.arg("state").toInt();
      updateWTWstate(wtwState);
      server.send(200, "application/json", getStatus());
    });
    
    // Address: 404
    //server.onNotFound(handleNotFound);
    server.onNotFound([]() {
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
    });
  } else {
    // -- Setup mode --

    // -- Normal mode --
    server.on("/",[]() {
       String strPage ="<!DOCTYPE HTML>\n"
          "<meta charset=\"utf-8\">\n"
          "<meta name=\"apple-mobile-web-app-capable\" content=\"yes\" />\n"
          "<meta http-equiv=\"X-UA-Compatible\" content=\"IE=edge,chrome=1\">\n"
          "<meta name=\"viewport\" content=\"width=device-width, user-scalable=no\">\n"
          "\n"
          "<title>Renovent HR - WiFi</title>\n"
          "<link rel=\"stylesheet\" href=\"http://code.jquery.com/mobile/1.0/jquery.mobile-1.0.min.css\" />\n"
          "<link rel=\"stylesheet\" href=\"http://cdnjs.cloudflare.com/ajax/libs/font-awesome/4.4.0/css/font-awesome.min.css\">\n"
          "<script src=\"http://code.jquery.com/jquery-1.6.4.min.js\"></script>\n"
          "<script src=\"http://code.jquery.com/mobile/1.0/jquery.mobile-1.0.min.js\"></script>\n"
 
          "<SCRIPT>\n"
          "var xmlHttp=createXmlHttpObject();\n"
          "var wtwState;\n"
          "function createXmlHttpObject(){\n"
          " if(window.XMLHttpRequest){\n"
          "    xmlHttp=new XMLHttpRequest();\n"
          " }else{\n"
          "    xmlHttp=new ActiveXObject('Microsoft.XMLHTTP');\n"
          " }\n"
          " return xmlHttp;\n"
          "}\n"
          "\n"
          "function process(){\n"
          "   document.getElementById(\"networks_found\").innerHTML=\"...scanning networks...\"\n"
          " if(xmlHttp.readyState==0 || xmlHttp.readyState==4){\n"
          "   xmlHttp.open('get','/networks',true);\n"
          "   xmlHttp.onreadystatechange=handleServerResponse;\n" 
          "   xmlHttp.send(null);\n"
          " }\n"
          "}\n"
          "function handleServerResponse(){\n"
          " if(xmlHttp.readyState==4 && xmlHttp.status==200){\n"
          "   jsonResponse = JSON.parse(xmlHttp.responseText);\n"
          "   var selectBox = document.getElementById(\"listbox_networks\");\n"
          "       selectBox.innerHTML = \"\";\n"
          "   for (var i = 0; i < jsonResponse.Networks.length; i++) {\n"
          "       var counter = jsonResponse.Networks[i].SSID;\n"
          "       var RSSI = jsonResponse.Networks[i].RSSI;\n"
          "       var signalQuality = 0;\n"
          "       switch (true) {\n"
          "          case (RSSI <= -100): signalQuality=0; break;\n"
          "          case (RSSI >= -50): signalQuality=100; break;\n"
          "          default: signalQuality=2*(RSSI+100); break;\n"
          "       }\n"
          "       var hasOption = $('#listbox_networks option[value=\"' + jsonResponse.Networks[i].SSID + '\"]');\n"
          "       if (hasOption.length == 0) {\n"
          "          var option = document.createElement('option');\n"
          "              option.text = jsonResponse.Networks[i].SSID +\" (\"+signalQuality+\"%)\";\n"
          "              option.value = jsonResponse.Networks[i].SSID;\n"
          "              selectBox.add(option, 0);\n"
          "       }\n"
          "   }\n"  
          "   document.getElementById(\"networks_found\").innerHTML=selectBox.length+\" networks found\"\n"
          " }\n"
          "}\n"
          "</SCRIPT>\n"
          "\n"
          "<BODY onload='process()'>\n"
          "<div data-role=\"header\" data-position=\"inline\">\n"
          "<h1>Renovent HR</h1>\n"
          "</div>\n"
          "<div class=\"ui-body ui-body-a\">\n"
          "<BR>Using this webpage you can change the WiFi configuration settings of the Renovent HR.<BR><BR>\n"
          "</div>\n"
          "<div class=\"ui-body ui-group-theme-a\">\n"
          "<label>SSID: </label><input name='ssid' id='ssid' length=32 value='"+(String)eepromSSID+"'><br>\n"
          "<label>Passphrase: </label><input name='pass' id='pass' length=64 type='password' value='"+(String)eepromPassword+"'><br>\n"
          "<label>MQTT broker (optional): </label><input name='mqtt' id='mqtt' length=32 value='"+(String)eepromMQTT+"'>\n"
          "<button onclick=\"save()\">Save</button>\n"
          "<center><A id='saved'></A></center>\n"
          "<br>\n"
          "<hr>\n"
          "Feel free to select a network that I found (why type if you can be lazy?).\n"
          "<select id='listbox_networks' onchange='listboxUpdate(this)'></select>\n"
          "<button onclick=\"process()\" id='scan_networks'>Scan networks</button>\n"
          "<center><A id='networks_found'></A></center>\n"
          "</font>\n"
          "</div>\n"
          "<SCRIPT>\n"
          "function listboxUpdate(dropdownlistNetworks) {\n"
          "   var selectedText = dropdownlistNetworks.options[dropdownlistNetworks.selectedIndex].innerHTML;\n"
          "   var selectedValue = dropdownlistNetworks.value;\n"
          "   document.getElementById('ssid').value=selectedValue;\n"
          "   document.getElementById('pass').value='';\n"
          "}\n"
          "function save() {\n"
          "   $.post('/save', { ssid: document.getElementById('ssid').value,\n"
          "                     pass: document.getElementById('pass').value,\n"
          "                     mqtt: document.getElementById('mqtt').value} );\n"
          "   document.getElementById(\"saved\").innerHTML=\"New configuration saved\"\n"
          "}\n"
          "</SCRIPT>\n"
          "</BODY>\n"
          "</HTML>\n";
      server.send(200,"text/html",strPage);
    });
    
    // Address: /networks
    server.on("/networks", []() {
       server.send(200, "application/json", getNetworks());
    });
  
    // Address: /save
    server.on("/save", []()
    {
      String qsid = server.arg("ssid");
      String qpass = server.arg("pass");
      String qmqtt = server.arg("mqtt");
      if (qsid.length() > 0 ) {
        //noInterrupts();
        Serial.println("Clearing memory...");
        for (int i = eepromSSIDstart; i <= eepromSSIDend; ++i) {
          EEPROM.write(i, 0);
        }
        for (int i = eepromPASSstart; i <= eepromPASSend; ++i) {
          EEPROM.write(i, 0);
        }
        for (int i = eepromMQTTstart; i <= eepromMQTTend; ++i) {
          EEPROM.write(i, 0);
        }
        //for (int i = 0; i < 96; ++i) { EEPROM.write(i, 0); }

        Serial.print("Writing SSID in memory       : ");
        for (int i = 0; i < qsid.length(); ++i)
        {
          EEPROM.write(eepromSSIDstart + i, qsid[i]);
          Serial.print(qsid[i]);
        }
        Serial.println("");
        Serial.print("Writing passphrase in memory : ");
        for (int i = 0; i < qpass.length(); ++i)
        {
          EEPROM.write(eepromPASSstart + i, qpass[i]);
          Serial.print(qpass[i]);
        }
        Serial.println("");
        Serial.print("Writing MQTT broker in memory : ");
        for (int i = 0; i < qmqtt.length(); ++i)
        {
          EEPROM.write(eepromMQTTstart + i, qmqtt[i]);
          Serial.print(qmqtt[i]);
        }
        Serial.println("");        
        yield();
        EEPROM.commit();
        //interrupts();
        server.send(200, "application/json", "{\"Success\":\"Succesfully stored new wireless settings to memory. Restart to boot into normal mode\"}");

      } else {

        //Unkown page
        server.send(404, "application/json", "{\"Error\"}");
      }
      });
  }
}


String getStatus() {
  DynamicJsonBuffer jsonBuffer;
  JsonObject& jsonRoot = jsonBuffer.createObject();
  String Status;
  jsonRoot["wtwState"] = currentState;
  jsonRoot["changeFilter"] = changeFilter;
  jsonRoot["uptime"]=millis()/1000;
  jsonRoot.prettyPrintTo(Status);   //printTo
  return Status;
}

String getNetworks(){
  DynamicJsonBuffer jsonDynamicBuffer;
  String Networks;
  JsonObject& jsonNetworks = jsonDynamicBuffer.createObject();
  JsonArray& jsonSSIDarray = jsonNetworks.createNestedArray("Networks");

  int n = WiFi.scanNetworks();
  for (int i = 0; i < n; ++i)
  {
     JsonObject& jsonSSID = jsonDynamicBuffer.createObject();
     jsonSSID["ID"]   = i;
     jsonSSID["SSID"] = WiFi.SSID(i);
     jsonSSID["RSSI"] = WiFi.RSSI(i);
     jsonSSID["encryptionType"] = WiFi.encryptionType(i);
     jsonSSIDarray.add(jsonSSID);
   }
  
  jsonNetworks.prettyPrintTo(Networks); 
  return Networks;
}



void doPortExtender() {
  if (mcp.digitalRead(PIN_button_up)) {buttonUp();  updateWTWstate(buttonState);};
  if (mcp.digitalRead(PIN_button_down)) {buttonDown();  updateWTWstate(buttonState);};
  doFilterChange();  
}


void doFilterChange() {

  //Update value
  int newFilterValue = mcp.digitalRead(PIN_FilterChange);
  if (changeFilter == newFilterValue) {return;}
  changeFilter = newFilterValue;
  Serial.print  ("changefilter : ");  
  Serial.println(changeFilter);
  updateDisplay();               //Update display
}


void buttonUp() {
  if (buttonState < buttonStateMax) buttonState = buttonState + 1;                      // CW - Clock Wise
}

void buttonDown() {
  if (buttonState > buttonStateMin) buttonState = buttonState - 1;                      // CCW - Counter Clock Wise
}


void updateWTWstate(int newWTWstate) {
  
  // Remember new state
  currentState = newWTWstate;
  buttonState = currentState;
  
  // Display new state
  if (currentState != -1) {
    Serial.print(F("New WTW state: "));
    Serial.println(currentState);
    delay(100);
  }

  // Write WTW state to EEPROM
  EEPROM.write(eepromWTWaddr, currentState);
  EEPROM.commit();

  //Update display
  updateDisplay();

  // Push the WTW state to the relays
  switch (currentState) {                                                                       // Push state to relays
    case 0: mcp.digitalWrite (PIN_relay1, LOW);  mcp.digitalWrite (PIN_relay2, LOW);  break;    // WTW state 0
    case 1: mcp.digitalWrite (PIN_relay1, LOW);  mcp.digitalWrite (PIN_relay2, HIGH); break;    // WTW state 1
    case 2: mcp.digitalWrite (PIN_relay1, HIGH); mcp.digitalWrite (PIN_relay2, HIGH);  break;   // WTW state 2
    case 3: mcp.digitalWrite (PIN_relay1, HIGH); mcp.digitalWrite (PIN_relay2, LOW); break;     // WTW state 3
  }

  // Make sure the new value is send via MQTT
  lastMQTTmsg   = millis() + (MQTTpublishInterval * 1000);
}


boolean IsNumeric(String str) {
    for(char i = 0; i < str.length(); i++) {
        if ( !(isDigit(str.charAt(i)) || str.charAt(i) == '.' )) {
            return false;
        }
    }
    return true;
}
