#include <TFT_eSPI.h>
#include <Arduino.h>
#include <millisDelay.h>
#include <OpenTherm.h>
#include "rpcWiFi.h"
#include <NTPClient.h>
#include "DateTime.h"

TFT_eSPI tft;


const int OT_TX_PIN = D0;
const int OT_RX_PIN = D1;

// OpenTherm initialization
OpenTherm ot(D0, D1);
int SET_BOILER_TEMP = 33; // Set temperature of boiler in degrees Celsius
const int MAX_BOILER_TEMP = 60; // Max temperature of boiler in degrees Celsius
const int MIN_BOILER_TEMP = 20; // Min temperature of boiler in degrees Celsius
bool chTimeTable = true;

int SET_DHW_TEMP = 55; // Set Domestic hot water temperature in degrees Celsius
const int MAX_DHW_TEMP = 70; // Max domestic hot water temperature in degrees Celsius
const int MIN_DHW_TEMP = 20; // // Min domestic hot water temperature in degrees Celsius
bool dhwTimeTable = true;


unsigned long burner_timestamp, burnerDuration, burner_off_time;
unsigned long ts = 0, new_ts = 0, interval = 0;

int window = 0; // Default window is Central heating
int flameReadAttempts = 0;
const int MAX_FLAME_READ_ATTEMPTS = 2;

bool enableCentralHeating = false;
bool enableHotWater = false;
bool enableCooling = false;

// Status variables
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

  // Get the current time from the time client
  timeClient.update();
  time_t currentTime = timeClient.getEpochTime();

  // Convert the current time to a struct tm for easier manipulation
  struct tm *currentLocalTime = localtime(&currentTime);
  currentHour = currentLocalTime->tm_hour;
  currentMinute = currentLocalTime->tm_min;

  // Start a timer for periodic updates
  updateDelay.start(600000);

  // Begin the OpenTherm communication
  ot.begin(handleInterrupt);
  
  ts = millis();
}

// Helper function to check response status
bool isValidResponse(int response, OpenThermResponseStatus responseStatus) {
  return ot.isValidResponse(response) && responseStatus == OpenThermResponseStatus::SUCCESS && !ot.isFault(response);
}


void loop() {
  new_ts = millis();
  interval = new_ts - ts;
  Serial.println("Interval: " + String(interval));
  if (interval >= 1000) {

    int response = ot.setBoilerStatus(enableCentralHeating, enableHotWater, enableCooling);
    OpenThermResponseStatus responseStatus = ot.getLastResponseStatus();
    // Check response status
    Serial.printf("IsValidResponse: %d\n", isValidResponse(response, responseStatus));
    Serial.printf("Fault: %d\n", ot.getFault());
    Serial.printf("Open Therm status: %d\n", responseStatus);
    Serial.printf("Is fault: %d\n", ot.isFault(response));

      // Handle invalid or faulty response
    if (!isValidResponse(response, responseStatus)) {
      Serial.println("OpenTherm response is not valid or contains a fault");
      return;
    }

    // Read flame status and boiler temperature
    if (ot.isFlameOn(response) == 0 && flameReadAttempts < MAX_FLAME_READ_ATTEMPTS) {
      flameReadAttempts++;
    } else {
      isFlameOn = ot.isFlameOn(response);
      flameReadAttempts = 0;
    }

    isCHActive = ot.isCentralHeatingActive(response);
    isDHWActive = ot.isHotWaterActive(response);
    dhw_temperature = ot.getDHWTemperature(); 
    boiler_temperature = ot.getBoilerTemperature();
    
    Serial.printf("isCHActive: %d\n", isCHActive);
    Serial.printf("flameReadAttempts: %d\n", flameReadAttempts);
    Serial.printf("isDHWActive: %d\n", isDHWActive);
    Serial.printf("isFlameOn: %d\n", isFlameOn);
    Serial.printf("Boiler temp: %d\n", boiler_temperature);
    Serial.printf("Burner op: %d\n", burnerDuration / 60000);

    // Calculate burner duration time during CH
    if (!isFlameOn && !isDHWActive && OpenThermResponseStatus::SUCCESS && !ot.isFault(response)) {
      burner_off_time = millis();
      burnerDuration = 0;
    }
    else if (isFlameOn && !isDHWActive && OpenThermResponseStatus::SUCCESS && !ot.isFault(response)) {
      burner_timestamp = millis();
      burnerDuration = burner_timestamp - burner_off_time;
    }

    updateCentralHeatingStatus();
    updateHotWaterStatus();

    if (enableCentralHeating && burnerDuration < 90000 && responseStatus == OpenThermResponseStatus::SUCCESS && !ot.isFault(response)) {
      ot.setBoilerTemperature(SET_BOILER_TEMP + 10);
    }
    else if (enableCentralHeating && burnerDuration >= 90000 && responseStatus == OpenThermResponseStatus::SUCCESS && !ot.isFault(response)) {
      ot.setBoilerTemperature(SET_BOILER_TEMP);
    }

    if (enableHotWater && responseStatus == OpenThermResponseStatus::SUCCESS && !ot.isFault(response)) {
      ot.setDHWSetpoint(SET_DHW_TEMP);
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
      tft.drawString("CH Temp:        " + String(SET_BOILER_TEMP) + " C", 20, 125);
      tft.drawString("Flame is:       " + String(isFlameOn ? "on " : "off"), 20, 150);
      tft.drawString("Flame Duration: " + String(burnerDuration / 60000) + " min", 20, 175);
      tft.drawString("Current Time:   " + String(currentHour) + ":" + String(currentMinute), 20, 200);

      if (digitalRead(WIO_KEY_B) == LOW && SET_BOILER_TEMP <= MAX_BOILER_TEMP) {
        SET_BOILER_TEMP ++;
      }
      else if (digitalRead(WIO_KEY_C) == LOW && SET_BOILER_TEMP >= MIN_BOILER_TEMP) {
        SET_BOILER_TEMP --;
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
      tft.drawString("DHW Temp Set:    " + String(SET_DHW_TEMP) + " C", 20, 125);
      tft.drawString("DHW Temp Curr:   " + String(dhw_temperature) + " C", 20, 150);

      if (digitalRead(WIO_KEY_B) == LOW && SET_DHW_TEMP <= MAX_DHW_TEMP)
      {
        SET_DHW_TEMP ++;
      }
      else if (digitalRead(WIO_KEY_C) == LOW && SET_DHW_TEMP >= MIN_DHW_TEMP)
      {
        SET_DHW_TEMP --;
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
  
  updateTimeFromServer();
  Serial.println("***************************************");

}

void connectToWiFi(const char* ssid, const char* pwd) {
  // delete old config
  WiFi.disconnect(true);

  Serial.println("Waiting for WIFI connection...");

  //Initiate connection
  WiFi.begin(ssid, pwd);

  int timeout = 10000; // 10 seconds timeout
  unsigned long start = millis();

  while (WiFi.status() != WL_CONNECTED && (millis() - start) < timeout) {
    delay(500);
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi connected");
  } else {
    Serial.println("WiFi connection timeout");
  }
}

void updateTimeFromServer() {
  // Only update the time if the delay period has elapsed
  if (updateDelay.justFinished()) {
    // Attempt to update the time from the server
    if (timeClient.update()) {
      // Update the current time variables
      time_t currentTime = timeClient.getEpochTime();
      // Convert the current time to a struct tm for easier manipulation
      struct tm *currentLocalTime = localtime(&currentTime);
      currentHour = currentLocalTime->tm_hour;
      currentMinute = currentLocalTime->tm_min;
    } else {
      // Handle the case where the update fails
      Serial.println("Error updating time from server!");
    }
    // Reset the delay timer to repeat the update after a fixed period
    updateDelay.repeat();
  }
}


// Returns true if the current time falls within the specified time range
bool isTimeInRange(int startHour, int endHour) {
  return (currentHour >= startHour && currentHour <= endHour);
}

// Updates the status of the central heating system based on the time table
void updateCentralHeatingStatus() {
  if (chTimeTable) {
    enableCentralHeating = isTimeInRange(7, 9) || isTimeInRange(15, 17);
  } else {
    enableCentralHeating = false;
  }
}

// Updates the status of the hot water system based on the time table
void updateHotWaterStatus() {
  if (dhwTimeTable) {
    enableHotWater = isTimeInRange(7, 10) || isTimeInRange(20, 21);
  } else {
    enableHotWater = false;
  }
}
