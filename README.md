#########################################################
# ESP32-AI Assistant – Command Reference
#########################################################

AUTHOR      = Sethun Vithanawasam
VERSION     = 1.6
PLATFORM    = Arduino IDE
LANGUAGE    = Arduino / C++

#########################################################
PROJECT_OVERVIEW
#########################################################
NAME        = ESP32-AI Assistant
TYPE        = Intelligent microcontroller-based assistant
CORE        = ESP32 (S3 or standard)
INTERFACE   = Arduino Serial Monitor
FUNCTIONS   = AI chat, reminders, weather, search, diagnostics, LED feedback

#########################################################
KEY_FEATURES
#########################################################
[AI_CHAT]
  ENGINE    = Google Gemini
  FEATURES  = Natural language, adaptive tone, retry logic, uncertainty detection

[MEMORY_SYSTEM]
  STORAGE   = FATFS persistent memory
  FEATURES  = Fact storage, relevance scoring, access counters

[REMINDERS]
  INPUT     = Natural language parsing
  RECURRENCE= once, daily, weekly, monthly
  SAFETY    = Conflict detection, LED alerts

[WEATHER_UPDATES]
  SOURCE    = Weather API
  FEATURES  = Real-time city weather

[WEB_SEARCH]
  FALLBACK  = Google Custom Search
  TRIGGER   = Gemini uncertainty

[SYSTEM_DIAGNOSTICS]
  METRICS   = WiFi status, IP address, CPU temperature, heap safety, uptime

[TIME_DATE]
  SYNC      = NTP (Sri Lanka UTC+5:30)
  DISPLAY   = Local time + date

[LED_NOTIFICATIONS]
  HARDWARE  = NeoPixel LED
  STATES    = thinking, replied, error, proactive, evolving

[RESILIENCE]
  SAFETY    = Watchdog timer, heap guard, self-healing WiFi

#########################################################
COMMANDS
#########################################################
/help            → Show available commands
/version         → Display current software version
/diag            → Run system diagnostics
/reminders       → List all active reminders
/remove <id>     → Delete reminder by index
/weather <city>  → Fetch weather for city
/search <query>  → Perform Google search
/clear           → Clear memory and history

#########################################################
REQUIREMENTS
#########################################################
HARDWARE:
  - ESP32-S3 or ESP32 DevKit

SOFTWARE:
  - Arduino IDE
  - ESP32 board support

LIBRARIES:
  - WiFi.h
  - HTTPClient.h
  - ArduinoJson.h
  - WiFiUdp.h
  - NTPClient.h
  - TimeLib.h
  - FFat / FS.h
  - Adafruit_NeoPixel
  - vector, algorithm

#########################################################
APIs_USED
#########################################################
[GEMINI_API]
  PURPOSE   = Conversational intelligence
  KEY       = Google API key required

[WEATHER_API]
  PURPOSE   = Real-time weather data
  KEY       = API key required

[GOOGLE_CUSTOM_SEARCH_API]
  PURPOSE   = Summarize search results
  KEY       = Google API key + CX ID required

#########################################################
SETUP_INSTRUCTIONS
#########################################################
1. Install Arduino IDE + ESP32 board support
2. Open .ino file
3. Insert API keys (Gemini, Weather, Google Search)
4. Connect ESP32 via USB
5. Select correct board + port
6. Upload code
7. Open Serial Monitor @ 115200 baud
8. Interact via commands or natural language

#########################################################
SAFETY_PRIVACY
#########################################################
- Keep API keys secure
- Replace with placeholders before sharing

#########################################################
CREDITS
#########################################################
CREATOR     = Sethun Vithanawasam
USES        = Google Gemini API, Weather API, Google Custom Search API
POWERED_BY  = ESP32 + Arduino IDE
