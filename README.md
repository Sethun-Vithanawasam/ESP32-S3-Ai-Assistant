# ESP32-AI Assistant – Command Reference

AUTHOR: Sethun Vithanawasam
VERSION: 1.6
PLATFORM: Arduino IDE
LANGUAGE: Arduino / C++

---------------------------------------------------------
PROJECT OVERVIEW
---------------------------------------------------------
NAME: ESP32-AI Assistant
TYPE: Intelligent microcontroller-based assistant
CORE: ESP32 (S3 or standard)
INTERFACE: Arduino Serial Monitor
FUNCTIONS: AI chat, reminders, weather, search, diagnostics, LED feedback

---------------------------------------------------------
KEY FEATURES
---------------------------------------------------------
AI_CHAT:
  - Natural language conversations via Google Gemini
  - Adaptive tone (technical / casual / organized)
  - Retry logic + uncertainty detection

MEMORY_SYSTEM:
  - Persistent fact storage (FATFS)
  - Relevance scoring with access counters

REMINDERS:
  - Natural language parsing
  - Recurrence: once / daily / weekly / monthly
  - Conflict detection + LED alerts

WEATHER_UPDATES:
  - Real-time city weather via API

WEB_SEARCH:
  - Google Custom Search fallback when Gemini uncertain

SYSTEM_DIAGNOSTICS:
  - WiFi status, IP address
  - CPU temperature, heap safety, uptime

TIME_DATE:
  - NTP sync (Sri Lanka UTC+5:30)
  - Local time + date display

LED_NOTIFICATIONS:
  - NeoPixel feedback for AI states
  - Modes: thinking / replied / error / proactive / evolving

RESILIENCE:
  - Watchdog timer
  - Heap guard
  - Self-healing WiFi

---------------------------------------------------------
COMMANDS
---------------------------------------------------------
/help            → Show available commands
/version         → Display current software version
/diag            → Run system diagnostics
/reminders       → List all active reminders
/remove <id>     → Delete reminder by index
/weather <city>  → Fetch weather for city
/search <query>  → Perform Google search
/clear           → Clear memory and history

---------------------------------------------------------
REQUIREMENTS
---------------------------------------------------------
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

---------------------------------------------------------
APIs USED
---------------------------------------------------------
GEMINI_API:
  - Conversational intelligence
  - Requires Google API key

WEATHER_API:
  - Real-time weather data
  - Requires API key

GOOGLE_CUSTOM_SEARCH_API:
  - Summarizes search results
  - Requires Google API key + CX ID

---------------------------------------------------------
SETUP INSTRUCTIONS
---------------------------------------------------------
1. Install Arduino IDE + ESP32 board support
2. Open .ino file
3. Insert API keys (Gemini, Weather, Google Search)
4. Connect ESP32 via USB
5. Select correct board + port
6. Upload code
7. Open Serial Monitor @ 115200 baud
8. Interact via commands or natural language

---------------------------------------------------------
SAFETY & PRIVACY
---------------------------------------------------------
- Keep API keys secure
- Replace with placeholders before sharing

---------------------------------------------------------
CREDITS
---------------------------------------------------------
CREATOR: Sethun Vithanawasam
USES: Google Gemini API, Weather API, Google Custom Search API
POWERED_BY: ESP32 + Arduino IDE
