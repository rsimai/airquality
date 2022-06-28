/* as part of https://hackweek.opensuse.org/21/projects/indoor-air-quality-sensor */

#include <Wire.h>
#include <SPI.h>
#include <Arduino.h>
#include "MHZ19.h"                                        
#include <SoftwareSerial.h>                                // Remove if using HardwareSerial
#include <U8x8lib.h>
#include <Adafruit_Sensor.h>
#include "Adafruit_BME680.h"

#define SEALEVELPRESSURE_HPA (1013.25)

Adafruit_BME680 bme;                                       // I2C 21 SDA, 22 SCL

#define RX_PIN 18                                          // Rx pin which the MHZ19 Tx pin is attached to
#define TX_PIN 19                                          // Tx pin which the MHZ19 Rx pin is attached to
#define BAUDRATE 9600                                      // Device to MH-Z19 Serial baudrate (should not be changed)

String firmware = "2022-06-28.1";

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

//U8X8_SSD1306_128X32_UNIVISION_HW_I2C u8x8(/* reset=*/ U8X8_PIN_NONE, /* clock=*/ SCL, /* data=*/ SDA); // 4 lines
U8X8_SSD1306_128X64_NONAME_HW_I2C u8x8(/* reset=*/ U8X8_PIN_NONE); // 8 lines

MHZ19 myMHZ19;                                             // Constructor for library
SoftwareSerial mySerial(RX_PIN, TX_PIN);                   // (Uno example) create device to MH-Z19 serial

unsigned long getDataTimer = 0;

void setup()
{
    pinMode(keypin1, INPUT); //down button
    pinMode(keypin2, INPUT); //up button
    u8x8.begin();
    u8x8.setPowerSave(0);
    Serial.begin(9600);                                     // Device to serial monitor feedback
    delay(1000);
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
    
    prepdisplay();
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
  u8x8.drawUTF8(0,2, "Not yet");
  u8x8.drawUTF8(0,3, "implemented");
  u8x8.drawUTF8(0,5, "firmware");
  u8x8.setCursor(0,6);
  u8x8.print(firmware);
  delay(3000);
  prepdisplay();
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
      Serial.println("key1 long press, not implemented");
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
      Serial.println("key2 long press, not implemented");
      lastkey2 = 0;
      key2 = 0;
    }
  }
  else {
    keyduration2 = 0;
  }
  if (key2 != lastkey2) {    //key change?
    if ( key2 > lastkey2 ) { //toggle only when key down
      Serial.println("key1 not implemented");
    }
    lastkey2 = key2;
  }
  delay(10);
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
}
