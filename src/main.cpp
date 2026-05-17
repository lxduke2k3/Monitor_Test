#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>

// ================= WIFI CONFIGURATION =================
const char* ssid       = "wifiname";         
const char* password   = "urwifipassword";             

// ================= (NTP) =================
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 7 * 3600; // GMT+7 cho Việt Nam
const int   daylightOffset_sec = 0;

#define TFT_CS    10
#define TFT_DC    11
#define TFT_RST   12
#define TFT_MOSI  21
#define TFT_SCLK  14
#define BTN_PIN   18
#define TOUCH_PIN 3

Adafruit_ST7789 tft = Adafruit_ST7789(&SPI, TFT_CS, TFT_DC, TFT_RST);

int currentState = 0; 
bool isPetted = false; 

// Debounce & Touch
bool lastButtonState = HIGH;
bool buttonState = HIGH;
unsigned long lastDebounceTime = 0;
unsigned long lastTouchTime = 0;

int TOUCH_THRESHOLD = 70000; 

// Data realtime
String currentTimeStr = "--:--";
String currentWeatherStr = "Updating...";
unsigned long lastWeatherUpdate = 0;
int lastMinute = -1; 
bool forceRedraw = true; 

// Animation 
unsigned long nextActionTime = 3000; 

void drawState();
void updateWeather();
void updateTime();

void setup() {
  Serial.begin(115200);

  pinMode(BTN_PIN, INPUT_PULLUP);
  SPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS); 
  tft.init(240, 320); 
  tft.setRotation(1); 
  tft.invertDisplay(false); 

  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_CYAN);
  tft.setTextSize(2);
  tft.setCursor(60, 110);
  tft.print("Connecting WiFi...");

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected!");

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  
  tft.fillRect(50, 110, 240, 30, ST77XX_BLACK); 
  tft.setCursor(50, 110);
  tft.print("Fetching Weather...");
  updateWeather();
  
  delay(1000);
  forceRedraw = true;
  drawState();
}

void loop() {
  unsigned long currentMillis = millis();

  // 1. weather and time update
  updateTime();
  if (currentMillis - lastWeatherUpdate > 600000) { 
    updateWeather();
    lastWeatherUpdate = currentMillis;
  }

  // 2. button 
  bool reading = digitalRead(BTN_PIN);
  if (reading != lastButtonState) {
    lastDebounceTime = currentMillis;
  }
  if ((currentMillis - lastDebounceTime) > 50) {
    if (reading != buttonState) {
      buttonState = reading;
      if (buttonState == LOW && !isPetted) {
        currentState = (currentState + 1) % 3; 
        forceRedraw = true; 
        drawState(); 
      }
    }
  }
  lastButtonState = reading;

  // 3. Touch sensor 
  int touchValue = touchRead(TOUCH_PIN);
  
  static unsigned long lastPrint = 0;
  if (currentMillis - lastPrint > 100) {
    Serial.printf("Touch Value (Pin 3): %d\n", touchValue);
    lastPrint = currentMillis;
  }

  if (touchValue > TOUCH_THRESHOLD) {
    lastTouchTime = currentMillis; // Liên tục cập nhật thời gian chạm cuối cùng
    
    if (!isPetted) {
      isPetted = true;
      forceRedraw = true;
      drawState(); 
    }
  }
  
  if (isPetted && (currentMillis - lastTouchTime > 500)) {
    isPetted = false;
    forceRedraw = true;
    drawState(); 
  }

  // 4. IDLE ANIMATION
  if (currentState == 0 && !isPetted && (currentMillis > nextActionTime)) {
    int action = random(100); 
    tft.fillScreen(ST77XX_BLACK); 
    
    if (action < 75) {
      tft.fillRoundRect(70, 115, 60, 10, 5, ST77XX_CYAN); 
      tft.fillRoundRect(190, 115, 60, 10, 5, ST77XX_CYAN);
      delay(150); 
    } 
    else if (action < 87) {
      tft.fillRoundRect(40, 80, 60, 80, 20, ST77XX_CYAN);
      tft.fillRoundRect(160, 80, 60, 80, 20, ST77XX_CYAN);
      delay(800); 
    } 
    else {
      tft.fillRoundRect(100, 80, 60, 80, 20, ST77XX_CYAN);
      tft.fillRoundRect(220, 80, 60, 80, 20, ST77XX_CYAN);
      delay(800); 
    }

    nextActionTime = millis() + random(2000, 4500);
    forceRedraw = true; 
    drawState();
  }
}

// ================= TIME UPDATE =================
void updateTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return;

  if (timeinfo.tm_min != lastMinute || forceRedraw) {
    char timeBuffer[10];
    strftime(timeBuffer, sizeof(timeBuffer), "%H:%M", &timeinfo);
    currentTimeStr = String(timeBuffer);
    lastMinute = timeinfo.tm_min;

    if (currentState == 1 && !isPetted) {
      tft.setTextSize(6);
      tft.setTextColor(ST77XX_CYAN, ST77XX_BLACK); 
      
      int16_t x1, y1;
      uint16_t w, h;
      tft.getTextBounds(currentTimeStr, 0, 0, &x1, &y1, &w, &h);
      tft.setCursor((320 - w) / 2, (240 - h) / 2);
      tft.print(currentTimeStr);
    }
  }
}

// ================= OPEN-METEO API =================
void updateWeather() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String url = "https://api.open-meteo.com/v1/forecast?latitude=21.0285&longitude=105.8542&current_weather=true";
    http.begin(url);
    int httpCode = http.GET();

    if (httpCode > 0) {
      String payload = http.getString();
      JsonDocument doc;
      deserializeJson(doc, payload);

      float temp = doc["current_weather"]["temperature"];
      currentWeatherStr = "Ha Noi - " + String((int)temp) + " C";
      
      if (currentState == 2 && !isPetted) forceRedraw = true; 
    }
    http.end();
  }
}

// =================RETURN TO MAIN SCREEN=================
void drawState() {
  if (!forceRedraw) return; 
  
  tft.fillScreen(ST77XX_BLACK); 

  if (isPetted) {
    
    tft.fillCircle(100, 100, 35, ST77XX_CYAN); 
    tft.fillCircle(100, 115, 35, ST77XX_BLACK); 
    
   
    tft.fillCircle(220, 100, 35, ST77XX_CYAN); 
    tft.fillCircle(220, 115, 35, ST77XX_BLACK); 
  } 
  else if (currentState == 0) {
    
    tft.fillRoundRect(70, 80, 60, 80, 20, ST77XX_CYAN);
    tft.fillRoundRect(190, 80, 60, 80, 20, ST77XX_CYAN);
  } 
  else if (currentState == 1) {
   
    lastMinute = -1; 
    updateTime();
  } 
  else if (currentState == 2) {
    tft.setTextColor(ST77XX_CYAN, ST77XX_BLACK); 
    tft.setTextSize(3); 
    int16_t x1, y1;
    uint16_t w, h;
    tft.getTextBounds(currentWeatherStr, 0, 0, &x1, &y1, &w, &h);
    tft.setCursor((320 - w) / 2, (240 - h) / 2);
    tft.print(currentWeatherStr);
  }

  forceRedraw = false; 
}
