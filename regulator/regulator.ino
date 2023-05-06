#include <TFT_eSPI.h>
#include <Arduino.h>
#include <OpenTherm.h>
#include "rpcWiFi.h"
#include <NTPClient.h>
#include "DateTime.h"
#include "RTC_SAMD51.h"

TFT_eSPI tft; // Initialize tft

const int OT_TX_PIN = D0;
const int OT_RX_PIN = D1;

OpenTherm ot(D0, D1);                   // OpenTherm initialization

// Heating
int SET_BOILER_TEMP = 33;               // Set temperature of boiler in degrees Celsius
const int MAX_BOILER_TEMP = 60;         // Max temperature of boiler in degrees Celsius
const int MIN_BOILER_TEMP = 20;         // Min temperature of boiler in degrees Celsius
const int BURNER_CHANGE_CONDITION = 90; // in seconds
bool chTimeTable = false;

// Domestic hot water
int SET_DHW_TEMP = 55;       // Set Domestic hot water temperature in degrees Celsius
const int MAX_DHW_TEMP = 70; // Max domestic hot water temperature in degrees Celsius
const int MIN_DHW_TEMP = 20; // // Min domestic hot water temperature in degrees Celsius
bool dhwTimeTable = true;

unsigned long burner_timestamp, burnerDuration, burner_off_time;
unsigned long ts = 0, new_ts = 0, interval = 0;
const int SECONDS_IN_HOUR = 3600;
const int SECONDS_IN_MINUTE = 60;

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
const char password[] = "";

// Initialize the RTC and NTP client objects
RTC_SAMD51 rtc;
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");

void IRAM_ATTR handleInterrupt()
{
  ot.handleInterrupt();
}

void setup()
{

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

  rtc.begin();                   // Initialize the RTC
  connectToWiFi(ssid, password); // Connect to WiFi
  setTimeFromNTP();              // Set the initial time from NTP

  tft.setTextSize(2.8);
  tft.fillScreen(TFT_WHITE);

  ot.begin(handleInterrupt);

  ts = millis();
}

void loop()
{
  DateTime now = rtc.now(); // Get the current time from the RTC
  int currentHour = now.hour();
  int currentMinute = now.minute();
  int second = now.second();

  Serial.printf("Current hour: %d\n", currentHour);
  Serial.printf("Current currentMinute: %d\n", currentMinute);

  new_ts = millis();
  interval = new_ts - ts;
  Serial.println("Interval: " + String(interval));
  if (interval >= 1000)
  {

    int response = ot.setBoilerStatus(enableCentralHeating, enableHotWater, enableCooling);
    OpenThermResponseStatus responseStatus = ot.getLastResponseStatus();
    // Check response status
    Serial.printf("IsValidResponse: %d\n", isValidResponse(response, responseStatus));
    Serial.printf("Fault: %d\n", ot.getFault());
    Serial.printf("Open Therm status: %d\n", responseStatus);
    Serial.printf("Is fault: %d\n", ot.isFault(response));

    // Handle invalid or faulty response
    if (isValidResponse(response, responseStatus))
    {

      // Read flame status and boiler temperature
      if (ot.isFlameOn(response) == 0 && flameReadAttempts < MAX_FLAME_READ_ATTEMPTS)
      {
        flameReadAttempts++;
      }
      else
      {
        isFlameOn = ot.isFlameOn(response);
        flameReadAttempts = 0;
      }

      isCHActive = ot.isCentralHeatingActive(response);
      isDHWActive = ot.isHotWaterActive(response);
      dhw_temperature = ot.getDHWTemperature();
      boiler_temperature = ot.getBoilerTemperature();
    }

    Serial.printf("isCHActive: %d\n", isCHActive);
    Serial.printf("flameReadAttempts: %d\n", flameReadAttempts);
    Serial.printf("isDHWActive: %d\n", isDHWActive);
    Serial.printf("isFlameOn: %d\n", isFlameOn);
    Serial.printf("Boiler temp: %d\n", boiler_temperature);
    Serial.printf("Burner op: %d\n", burnerDuration);

    // Calculate burner duration time during CH
    if (!isFlameOn && !isDHWActive && isValidResponse(response, responseStatus))
    {
      burner_off_time = (currentHour * SECONDS_IN_HOUR) + (currentMinute * SECONDS_IN_MINUTE);
      burnerDuration = 0;
    }
    else if (isFlameOn && !isDHWActive && isValidResponse(response, responseStatus))
    {
      burner_timestamp = (currentHour * SECONDS_IN_HOUR) + (currentMinute * SECONDS_IN_MINUTE);
      burnerDuration = burner_timestamp - burner_off_time;
    }

    updateCentralHeatingStatus(chTimeTable, currentHour);
    updateHotWaterStatus(dhwTimeTable, currentHour);

    if (enableCentralHeating && burnerDuration < BURNER_CHANGE_CONDITION && isValidResponse(response, responseStatus))
    {
      ot.setBoilerTemperature(SET_BOILER_TEMP + 12);
    }
    else if (enableCentralHeating && burnerDuration >= BURNER_CHANGE_CONDITION && isValidResponse(response, responseStatus))
    {
      ot.setBoilerTemperature(SET_BOILER_TEMP);
    }

    if (enableHotWater && isValidResponse(response, responseStatus))
    {
      ot.setDHWSetpoint(SET_DHW_TEMP);
    }
    ts = new_ts;
  }

  switch (window)
  {
  case 0: // Central heating window
    tft.setTextSize(3);
    tft.drawString("Central Heating", 20, 20);
    tft.setTextSize(2.8);
    tft.drawString("CH Enabled:     " + String(enableCentralHeating ? "on " : "off"), 20, 50);
    tft.drawString("CH TimeTable:   " + String(chTimeTable ? "on " : "off"), 20, 75);
    tft.drawString("CH Active:      " + String(isCHActive ? "true " : "false"), 20, 100);
    tft.drawString("CH Temp:        " + String(SET_BOILER_TEMP) + " C", 20, 125);
    tft.drawString("Flame is:       " + String(isFlameOn ? "on " : "off"), 20, 150);
    tft.drawString("Flame Duration: " + String(burnerDuration) + "  min", 20, 175);
    tft.drawString("Current Time:   " + String(currentHour) + ":" + String(currentMinute), 20, 200);

    if (digitalRead(WIO_KEY_B) == LOW && SET_BOILER_TEMP <= MAX_BOILER_TEMP)
    {
      SET_BOILER_TEMP++;
    }
    else if (digitalRead(WIO_KEY_C) == LOW && SET_BOILER_TEMP >= MIN_BOILER_TEMP)
    {
      SET_BOILER_TEMP--;
    }

    if (digitalRead(WIO_5S_LEFT) == LOW && chTimeTable)
    {
      chTimeTable = false;
    }
    else if (digitalRead(WIO_5S_RIGHT) == LOW && !chTimeTable)
    {
      chTimeTable = true;
    }

    if (digitalRead(WIO_5S_UP) == LOW && !chTimeTable && enableCentralHeating)
    {
      enableCentralHeating = false;
    }
    else if (digitalRead(WIO_5S_DOWN) == LOW && !chTimeTable && !enableCentralHeating)
    {
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
      SET_DHW_TEMP++;
    }
    else if (digitalRead(WIO_KEY_C) == LOW && SET_DHW_TEMP >= MIN_DHW_TEMP)
    {
      SET_DHW_TEMP--;
    }

    if (digitalRead(WIO_5S_LEFT) == LOW && dhwTimeTable)
    {
      dhwTimeTable = false;
    }
    else if (digitalRead(WIO_5S_RIGHT) == LOW && !dhwTimeTable)
    {
      dhwTimeTable = true;
    }

    if (digitalRead(WIO_5S_UP) == LOW && !dhwTimeTable && enableHotWater)
    {
      enableHotWater = false;
    }
    else if (digitalRead(WIO_5S_DOWN) == LOW && !dhwTimeTable && !enableHotWater)
    {
      enableHotWater = true;
    }
    break;
  }

  if (digitalRead(WIO_KEY_A) == LOW)
  {
    window++;
    if (window > 1)
    {
      window = 0;
    }
    tft.fillScreen(TFT_WHITE);
  }

  Serial.println("***************************************");
}

void connectToWiFi(const char *ssid, const char *pwd)
{
  // delete old config
  WiFi.disconnect(true);

  Serial.println("Waiting for WIFI connection...");

  // Initiate connection
  WiFi.begin(ssid, pwd);

  int timeout = 10000; // 10 seconds timeout
  unsigned long start = millis();

  while (WiFi.status() != WL_CONNECTED && (millis() - start) < timeout)
  {
    delay(500);
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.println("WiFi connected");
  }
  else
  {
    Serial.println("WiFi connection timeout");
  }
}

void setTimeFromNTP()
{
  // Connect to NTP server and set the time
  timeClient.begin();
  timeClient.setTimeOffset(3600);
  timeClient.update();
  while (!timeClient.update())
  {
    Serial.println("Updating time from NTP...");
    delay(1000);
  }
  timeClient.end();
  rtc.adjust(DateTime(timeClient.getEpochTime())); // Set the RTC time
}

static bool isTimeInRange(int currentHour, int startHour, int endHour)
{
  return (currentHour >= startHour && currentHour <= endHour);
}

void updateCentralHeatingStatus(bool chTimeTable, int currentHour)
{
  if (chTimeTable)
  {
    if (isTimeInRange(currentHour, 7, 9) || isTimeInRange(currentHour, 15, 17))
    {
      enableCentralHeating = true;
    }
    else
    {
      enableCentralHeating = false;
    }
  }
}

void updateHotWaterStatus(bool dhwTimeTable, int currentHour)
{
  if (dhwTimeTable)
  {
    if (isTimeInRange(currentHour, 7, 10) || isTimeInRange(currentHour, 20, 21))
    {
      enableHotWater = true;
    }
    else
    {
      enableHotWater = false;
    }
  }
}


// Helper function to check response status
bool isValidResponse(int response, OpenThermResponseStatus responseStatus)
{
  return ot.isValidResponse(response) && responseStatus == OpenThermResponseStatus::SUCCESS && !ot.isFault(response);
}