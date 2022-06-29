/* as part of https://hackweek.opensuse.org/21/projects/indoor-air-quality-sensor */
#include <WiFiManager.h> // https://github.com/tzapu/WiFiManager
#include <Wire.h>
#include <SPI.h>
#include <Arduino.h>
#include "MHZ19.h"                                        
#include <SoftwareSerial.h>                                // Remove if using HardwareSerial
#include <U8x8lib.h>
#include <Adafruit_Sensor.h>
#include "Adafruit_BME680.h"
#include <WiFi.h>                                          // Wifi+Webserver stuff
#include <WiFiClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>

WebServer server(80);

#define SEALEVELPRESSURE_HPA (1013.25)

Adafruit_BME680 bme;                                       // I2C 21 SDA, 22 SCL

#define RX_PIN 18                                          // Rx pin which the MHZ19 Tx pin is attached to
#define TX_PIN 19                                          // Tx pin which the MHZ19 Rx pin is attached to
#define BAUDRATE 9600                                      // Device to MH-Z19 Serial baudrate (should not be changed)

String firmware = "2022-06-28.1";
String WifiAP = "R-ESP";

#define keypin1 34 // button 1
#define keypin2 35 // button 2

/* keys */
boolean key1;
boolean lastkey1;
int keyduration1;
boolean key2;
boolean lastkey2;
int keyduration2;
int longkey = 3;   //seconds to enter menu

int refresh = 4000;
bool bmefail = false;
bool powersave = false;

int MHZTemp;  // for reference only, usually too high
int co2; 
float temp;
float press;
float humid;
float gas;
char rco2[7];
char rtemp[7];     //output formatting
char rpress[7];
char rhumid[7];
char rgas[7];

const int led = 13;

//U8X8_SSD1306_128X32_UNIVISION_HW_I2C u8x8(/* reset=*/ U8X8_PIN_NONE, /* clock=*/ SCL, /* data=*/ SDA); // 4 lines
U8X8_SSD1306_128X64_NONAME_HW_I2C u8x8(/* reset=*/ U8X8_PIN_NONE); // 8 lines

MHZ19 myMHZ19;                                             // Constructor for library
SoftwareSerial mySerial(RX_PIN, TX_PIN);                   // create device to MH-Z19 serial

unsigned long getDataTimer = 0;

void setup()
{
  WiFi.mode(WIFI_STA);
  Serial.begin(115200);                                   // Device to serial monitor feedback
  delay(1000);
  Serial.println("WifiManager start...");
  WiFiManager wm;
  wm.setConnectTimeout(20);
  bool res;
  // res = wm.autoConnect(); // auto generated AP name from chipid
  // res = wm.autoConnect("AutoConnectAP"); // anonymous ap
  //res = wm.autoConnect(WifiAP,"passwort"); // password protected ap
  res = wm.autoConnect("R-ESP"); // anonymous ap
  if(!res) {
    Serial.println("Failed to connect to Wifi");
    // ESP.restart();
  } 
  else {
    Serial.println("Connected to Wifi)");
  }
  pinMode(keypin1, INPUT); //down button
  pinMode(keypin2, INPUT); //up button
  u8x8.begin();
  u8x8.setPowerSave(0);
    
  mySerial.begin(BAUDRATE);                               // (Uno example) device to MH-Z19 serial start   
  myMHZ19.begin(mySerial);                                // *Serial(Stream) refence must be passed to library begin(). 
  Serial.println("");
  Serial.print("MHZ19 CO2 range: ");
  Serial.println(myMHZ19.getRange());
  myMHZ19.autoCalibration();                              // Turn auto calibration ON (OFF autoCalibration(false))
  bmeinit();

  Serial.print("Refresh: ");
  Serial.println(refresh);
  Serial.println("");

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

    
    prepdisplay();
}

void handleRoot() {
  digitalWrite(led, 1);
  int seconds = millis() / 1000;
  int minutes = seconds / 60;
  int hours = minutes / 60;
  int days = hours / 24;
  
  char temptext[800];
  
  snprintf(temptext, 800,
  
  "<html>\
    <head>\
     <meta http-equiv='refresh' content='5' charset='UTF-8'/>\
     <title>Env Sensor</title>\
    </head>\
   <body>\
    <p>\
    <h1>Hello from Robert\'s ESP32!</h1>\
    <table border=1>\
    <tr><th>Parameter</th><th>Value</th><th>Unit</th></tr>\
    <tr><td>CO2</td><td>%i</td><td>ppm</td></tr>\
    <tr><td>Temperature</td><td>%.1f</td><td>°C</td></tr>\
    <tr><td>Pressure</td><td>%.1f</td><td>hPa</td></tr>\
    <tr><td>Humidity</td><td>%.1f</td><td>&percnt;Rel</td></tr>\
    <tr><td>Quality</td><td>%.1f</td><td>k&Omega;</td></tr>\
    </table>\
    </p>\
    <p>\
    uptime (D:H:M:S): %02d:%02d:%02d:%02d\
    </p>\
    <p>\
    <a href=\"https://github.com/rsimai/airquality\">project repo</a>\
    </p>\
  </body>\
   </html>",

           co2, temp, press, humid, gas, days, hours % 24, minutes % 60, seconds % 60
          );

  server.send(200, "text/html", temptext);
  digitalWrite(led, 0);
}

void handleNotFound() {
  digitalWrite(led, 1);
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
  digitalWrite(led, 0);
}

void drawGraph() {
  String out = "";
  char temp[100];
  out += "<svg xmlns=\"http://www.w3.org/2000/svg\" version=\"1.1\" width=\"400\" height=\"150\">\n";
  out += "<rect width=\"400\" height=\"150\" fill=\"rgb(250, 230, 210)\" stroke-width=\"1\" stroke=\"rgb(0, 0, 0)\" />\n";
  out += "<g stroke=\"black\">\n";
  int y = rand() % 130;
  for (int x = 10; x < 390; x += 10) {
    int y2 = rand() % 130;
    sprintf(temp, "<line x1=\"%d\" y1=\"%d\" x2=\"%d\" y2=\"%d\" stroke-width=\"1\" />\n", x, 140 - y, x + 10, 140 - y2);
    out += temp;
    y = y2;
  }
  out += "</g>\n</svg>\n";

  server.send(200, "image/svg+xml", out);
}

void bmeinit() {
  Serial.println(F("bme680 init: temp, press, humid, quality"));

    if (!bme.begin()) {
      Serial.println("Could not find bme680 sensor, halt!");
      while (1);
    }

    //Set up oversampling and filter initialization
    bme.setTemperatureOversampling(BME680_OS_8X);
    bme.setHumidityOversampling(BME680_OS_2X);
    bme.setPressureOversampling(BME680_OS_4X);
    bme.setIIRFilterSize(BME680_FILTER_SIZE_3);
    bme.setGasHeater(320, 150); // 320*C for 150 ms

    bmefail = false;
}

void f1() {
  u8x8.setFont(u8x8_font_pressstart2p_f);
}

void f2() {
  u8x8.setFont(u8x8_font_8x13_1x2_f);
}

void f3() {
  u8x8.setFont(u8x8_font_inr33_3x6_f);
}

void readbme() {
  if (! bme.performReading()) {
    Serial.println("bme sensor fail, try re-init ...");
    bmefail = true;
    return;
  }
  else {
    temp = bme.temperature;
    press = bme.pressure / 100.0;
    humid = bme.humidity;
    gas = bme.gas_resistance / 1000.0;
  }
}

void readmhz() {
  MHZTemp = myMHZ19.getTemperature();
  co2 = myMHZ19.getCO2();
  
}

void error() {
  if ( bmefail == true ) {
    u8x8.clear();
    u8x8.drawUTF8(0,0, "bme sensor fail");
    u8x8.drawUTF8(0,1, "try re-init ...");
    u8x8.drawUTF8(0,5, "firmware");
    u8x8.setCursor(0,6);
    u8x8.print(firmware);
    delay(5000);
    bmeinit();
    prepdisplay();
  }
}

void service() {
  u8x8.clear();
  f2();
  u8x8.drawUTF8(0,0, "Service");
  f1();
  u8x8.drawUTF8(0,3, "IP");
  u8x8.setCursor(0,4);
  u8x8.print(WiFi.localIP());
  u8x8.drawUTF8(0,6, "firmware");
  u8x8.setCursor(0,7);
  u8x8.print(firmware);
  delay(5000);
  prepdisplay();
}

void showWifiManager() {
  u8x8.clear();
  f1();
  u8x8.drawUTF8(0,0, "WifiManager up");
  u8x8.drawUTF8(0,3, "Connect to AP");
}

void checkkey() {
  key1 = digitalRead(keypin1);
  key2 = digitalRead(keypin2);
  
  if ( key1 == true) {
    key2 = 0;
    keyduration2 = 0;
    keyduration1++;
    if ( keyduration1 == ( 25 * longkey )) { // force on
      powersave = 0;
      u8x8.setPowerSave(powersave);
      
    }
    if ( keyduration1 > ( 100 * longkey )) {
      //Serial.println("key1 long press, not implemented");
      service();
      
      lastkey1 = 0;
      key1 = 0;
    }
  }
  else {
    keyduration1 = 0;
  }
  if (key1 != lastkey1) {    //key change?
    if ( key1 > lastkey1 ) { //toggle only when key down
      powersave = !powersave;
    }
    lastkey1 = key1;
    u8x8.setPowerSave(powersave);
  }
  
  if ( key2 == true) {
    keyduration2++;
    if ( keyduration2 > ( 100 * longkey )) {
      reconfigwifi();
      lastkey2 = 0;
      key2 = 0;
    }
  }
  else {
    keyduration2 = 0;
  }
  if (key2 != lastkey2) {    //key change?
    if ( key2 > lastkey2 ) { //toggle only when key down
      Serial.println("key2 short press not implemented");
    }
    lastkey2 = key2;
  }
  delay(10);
}

void reconfigwifi() {
  Serial.println("stopping webserver");
  server.stop();                        //webserver already on port 80
  Serial.println("WifiManager started...");
  showWifiManager();
  WiFiManager wm;
  wm.setConfigPortalTimeout(120);
  if (!wm.startConfigPortal("RE-ESP")) {
    Serial.println("Failed to connect or hit timeout. Restart.");
    delay(3000);
    //reset and try again, or maybe put it to deep sleep
    ESP.restart();
    delay(5000);
  Serial.println("config successful, continue operation");
  server.begin();
  prepdisplay();
  }
}

void serialout() {
  Serial.print("CO2 = ");                      
  Serial.println(co2);                                
  //Serial.print("MHZTemp = ");                  
  //Serial.println(MHZTemp); 
  Serial.print("Temperature = ");
  Serial.println(temp);
  Serial.print("Pressure = ");
  Serial.println(press);
  Serial.print("Humidity = ");
  Serial.println(humid);
  Serial.print("Quality = ");
  Serial.println(gas);
  
}

void prepdisplay() { // provides the skeleton with units, static
  u8x8.clear();
  f1();
  u8x8.drawUTF8(13,1, "ppm");
  f2();
  u8x8.drawUTF8(13,2, "CO");
  f1();
  u8x8.drawUTF8(15,3, "2");
  u8x8.drawUTF8(13, 5, "°C");
  u8x8.drawUTF8(13,6, "hPa");
  u8x8.drawUTF8(13,7, "%RH");
  //u8x8.drawUTF8(14,3, "Q");
}

void oledout() {
  // format vars
  dtostrf(co2, 4, 0, rco2);
  dtostrf(temp, 4, 1, rtemp);
  dtostrf(press, 6, 1, rpress);
  dtostrf(humid, 4, 1, rhumid);
  dtostrf(gas, 6, 1, rgas);
  // format output
  u8x8.setCursor(0,0);
  f3();
  u8x8.print(rco2);
  f1();
  u8x8.setCursor(8,5);  // (x,y)
  u8x8.print(rtemp);
  u8x8.setCursor(6,6);
  u8x8.print(rpress);
  u8x8.setCursor(8,7);
  u8x8.print(rhumid);
  //u8x8.setCursor(7,3);
  //u8x8.print(rgas);
}

void loop()
{
    if ( millis() - getDataTimer >= refresh )
    {
        readmhz();
        readbme();
        error();
        oledout();
        serialout();
        getDataTimer = millis();
    }
    checkkey();
    server.handleClient();
}
