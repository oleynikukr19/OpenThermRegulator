#include <TFT_eSPI.h>
#include <Arduino.h>
#include <millisDelay.h>
#include <OpenTherm.h>
#include "rpcWiFi.h"
#include <NTPClient.h>
#include "DateTime.h"

TFT_eSPI tft;

OpenTherm ot(D0, D1);

int setBoilerTemp = 33; // Set temperature of boiler in degrees Celsius
int maxBoilerTemp = 60; // Max temperature of boiler in degrees Celsius
int minBoilerTemp = 20; // Min temperature of boiler in degrees Celsius
bool chTimeTable = true;

int setDhwTemp = 55; // Set Domestic hot water temperature in degrees Celsius
int maxsetDhwTemp = 70; // Max domestic hot water temperature in degrees Celsius
int minsetDhwTemp = 20; // // Min domestic hot water temperature in degrees Celsius
bool dhwTimeTable = true;

unsigned long burner_timestamp, burnerDuration, burner_off_time, response;
unsigned long ts = 0, new_ts = 0, interval = 0;

int window = 0; // Default window is Central heating
int flameReadAttempts = 0;

bool enableCentralHeating = false;
bool enableHotWater = false;
bool enableCooling = false;

bool isFlameOn;
bool isCHActive;
bool isDHWActive;

float dhw_temperature;
float boiler_temperature;

const char ssid[] = "UniFi UAP-AC-LR";
const char password[] =  "JtwRft@1203";

millisDelay updateDelay;
DateTime now;
int currentHour, currentMinute;

WiFiUDP ntpUDP;

NTPClient timeClient(ntpUDP, "europe.pool.ntp.org");

void IRAM_ATTR handleInterrupt() {
    ot.handleInterrupt();
}

void setup() {

  tft.begin();

  tft.setRotation(3);
  tft.fillScreen(TFT_WHITE);
  tft.setTextColor(TFT_BLACK, TFT_WHITE);

  Serial.begin(115200);

  pinMode(D1, OUTPUT);
  pinMode(D0, INPUT);
  pinMode(WIO_KEY_A, INPUT_PULLUP);
  pinMode(WIO_KEY_B, INPUT_PULLUP);
  pinMode(WIO_KEY_C, INPUT_PULLUP);
  pinMode(WIO_5S_PRESS, INPUT_PULLUP);

  pinMode(WIO_5S_UP, INPUT_PULLUP);
  pinMode(WIO_5S_DOWN, INPUT_PULLUP);
  pinMode(WIO_5S_LEFT, INPUT_PULLUP);
  pinMode(WIO_5S_RIGHT, INPUT_PULLUP);

  connectToWiFi(ssid, password);

  tft.setTextSize(2.8);
  tft.fillScreen(TFT_WHITE);

  timeClient.begin();
  timeClient.setTimeOffset(3600);

  timeClient.update();
  currentHour = timeClient.getHours();
  currentMinute = timeClient.getMinutes();
  updateDelay.start(600000); 
  ot.begin(handleInterrupt);
  
  ts = millis();
}


void loop() {
  new_ts = millis();
  interval = new_ts - ts;
  Serial.println("Interval: " + String(interval));
  if (interval >= 1000) {

    response = ot.setBoilerStatus(enableCentralHeating, enableHotWater, enableCooling);
    OpenThermResponseStatus responseStatus = ot.getLastResponseStatus();
    Serial.println("IsValidResponce: " + String(ot.isValidResponse(response)));
    Serial.println("Fault: " + String(ot.getFault()));
    Serial.println("Open Therm status: " + String(responseStatus));
    Serial.println("Is fault: " + String(ot.isFault(response)));


    if (responseStatus == OpenThermResponseStatus::SUCCESS && !ot.isFault(response) && ot.isValidResponse(response)) {
      if (ot.isFlameOn(response) == 0 && flameReadAttempts < 2) {
        flameReadAttempts ++;
      }
      else {
        isFlameOn = ot.isFlameOn(response);
        flameReadAttempts = 0;
      }

      isCHActive = ot.isCentralHeatingActive(response);
      isDHWActive = ot.isHotWaterActive(response);
      dhw_temperature = ot.getDHWTemperature(); 
      boiler_temperature = ot.getBoilerTemperature();
      Serial.println("isCHActive: " + String(isCHActive));
      Serial.println("flameReadAttempts: " + String(flameReadAttempts));
      Serial.println("isDHWActive: " + String(isDHWActive));
      Serial.println("isFlameOn: " + String(isFlameOn));
      Serial.println("Boler temp: " + String(boiler_temperature));
      Serial.println("Burner op: " + String(burnerDuration / 60000));
    }

    // Calculate burner duration time during CH
    if (!isFlameOn && !isDHWActive && OpenThermResponseStatus::SUCCESS && !ot.isFault(response)) {
      burner_off_time = millis();
      burnerDuration = 0;
    }
    else if (isFlameOn && !isDHWActive && OpenThermResponseStatus::SUCCESS && !ot.isFault(response)) {
      burner_timestamp = millis();
      burnerDuration = burner_timestamp - burner_off_time;
    }

    handleChTimeTable();
    handledhwTimeTable();

    if (enableCentralHeating && burnerDuration < 90000 && responseStatus == OpenThermResponseStatus::SUCCESS && !ot.isFault(response)) {
      ot.setBoilerTemperature(setBoilerTemp + 10);
    }
    else if (enableCentralHeating && burnerDuration >= 90000 && responseStatus == OpenThermResponseStatus::SUCCESS && !ot.isFault(response)) {
      ot.setBoilerTemperature(setBoilerTemp);
    }

    if (enableHotWater && responseStatus == OpenThermResponseStatus::SUCCESS && !ot.isFault(response)) {
      ot.setDHWSetpoint(setDhwTemp);
    }
    ts = new_ts;
  }

  switch (window) {
    case 0: // Central heating window
      tft.setTextSize(3);
      tft.drawString("Central Heating", 20, 20);
      tft.setTextSize(2.8);
      tft.drawString("CH Enabled:     " + String(enableCentralHeating ? "on " : "off"), 20, 50);
      tft.drawString("CH TimeTable:   " + String(chTimeTable ? "on " : "off"), 20, 75);
      tft.drawString("CH Active:      " + String(isCHActive ? "true " : "false"), 20, 100);
      tft.drawString("CH Temp:        " + String(setBoilerTemp) + " C", 20, 125);
      tft.drawString("Flame is:       " + String(isFlameOn ? "on " : "off"), 20, 150);
      tft.drawString("Flame Duration: " + String(burnerDuration / 60000) + " min", 20, 175);
      tft.drawString("Current Time:   " + String(currentHour) + ":" + String(currentMinute), 20, 200);

      if (digitalRead(WIO_KEY_B) == LOW && setBoilerTemp <= maxBoilerTemp) {
        setBoilerTemp ++;
      }
      else if (digitalRead(WIO_KEY_C) == LOW && setBoilerTemp >= minBoilerTemp) {
        setBoilerTemp --;
      }

      if (digitalRead(WIO_5S_LEFT) == LOW && chTimeTable) {
        chTimeTable = false;
      }
      else if (digitalRead(WIO_5S_RIGHT) == LOW && !chTimeTable) {
        chTimeTable = true;
      }

      if (digitalRead(WIO_5S_UP) == LOW && !chTimeTable && enableCentralHeating) {
        enableCentralHeating = false;
      }
      else if (digitalRead(WIO_5S_DOWN) == LOW && !chTimeTable && !enableCentralHeating) {
        enableCentralHeating = true;
      }
      
      break;
    case 1: // DHW
      tft.setTextSize(3);
      tft.drawString("DHW", 20, 20);
      tft.setTextSize(2.8);
      tft.drawString("DHW Enabled:     " + String(enableHotWater ? "on " : "off"), 20, 50);
      tft.drawString("DHW TimeTable:   " + String(dhwTimeTable ? "on " : "off"), 20, 75);
      tft.drawString("DHW Active:      " + String(isDHWActive ? "true " : "false"), 20, 100);
      tft.drawString("DHW Temp Set:    " + String(setDhwTemp) + " C", 20, 125);
      tft.drawString("DHW Temp Curr:   " + String(dhw_temperature) + " C", 20, 150);

      if (digitalRead(WIO_KEY_B) == LOW && setDhwTemp <= maxsetDhwTemp)
      {
        setDhwTemp ++;
      }
      else if (digitalRead(WIO_KEY_C) == LOW && setDhwTemp >= minsetDhwTemp)
      {
        setDhwTemp --;
      }

      if (digitalRead(WIO_5S_LEFT) == LOW && dhwTimeTable) {
        dhwTimeTable = false;
      }
      else if (digitalRead(WIO_5S_RIGHT) == LOW && !dhwTimeTable) {
        dhwTimeTable = true;
      }

      if (digitalRead(WIO_5S_UP) == LOW && !dhwTimeTable && enableHotWater) {
        enableHotWater = false;
      }
      else if (digitalRead(WIO_5S_DOWN) == LOW && !dhwTimeTable && !enableHotWater) {
        enableHotWater = true;
      }
      break;
    }

  if (digitalRead(WIO_KEY_A) == LOW) {
    window ++;
    if (window > 1) {
      window = 0;
      }
      tft.fillScreen(TFT_WHITE);
  }
  
  handleTimeClientUpdateRepeat();
  Serial.println("***************************************");

}

void connectToWiFi(const char* ssid, const char* pwd) {
    // delete old config
    WiFi.disconnect(true);
 
    Serial.println("Waiting for WIFI connection...");
 
    //Initiate connection
    WiFi.begin(ssid, pwd);
 
    while (WiFi.status() != WL_CONNECTED) {
      tft.drawString("Connecting to WiFi", 20, 40);
      delay(500);
      tft.fillScreen(TFT_WHITE);
    }
    delay(1000);
}

void handleTimeClientUpdateRepeat() {
    if (updateDelay.justFinished()) {
      timeClient.update();
      currentHour = timeClient.getHours();
      currentMinute = timeClient.getMinutes();
      updateDelay.repeat();
      }

}

void handleChTimeTable() {
  if (chTimeTable) {
    if ((currentHour >= 7 && currentHour <= 9) || (currentHour >= 15 && currentHour <= 17)) {
      enableCentralHeating = true;
    }
    else {
      enableCentralHeating = false;
    }
  }
}

void handledhwTimeTable() {
  if (dhwTimeTable) {
    if ((currentHour >= 7 && currentHour <= 10) || (currentHour >= 20 && currentHour <= 21)) {
      enableHotWater = true;
    }
    else {
      enableHotWater = false;
    }
  }
}
