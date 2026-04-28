// ╔══════════════════════════════════════════════════════════════════╗
// ║                      ESP32-AI v1.7-GROQ                          ║
// ║    Full drop-in replacement: Gemini → Groq (OpenAI-compatible)   ║
// ║    Model: llama-3.1-8b-instant                                   ║
// ║    All helper calls (sentiment, auto-learn, reminders) updated   ║
// ║    groqSimpleCall() helper replaces repeated boilerplate         ║
// ║    Groq API website: https://console.groq.com/keys               ║
// ╚══════════════════════════════════════════════════════════════════╝

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <TimeLib.h>
#include <FFat.h>
#include <FS.h>
#include <vector>
#include <algorithm>
#include <Adafruit_NeoPixel.h>
#include <esp_task_wdt.h>

// ═══════════════════════════════════════════════════════
// SECTION 1 ── CONFIGURATION
// ═══════════════════════════════════════════════════════
namespace Config {
  // WiFi
  constexpr const char* SSID       = "Enter_WiFi_SSID";
  constexpr const char* PASSWORD   = "Enter_WiFi_Password";

  // ── Groq API (replaces Gemini) ──────────────────────
  constexpr const char* GROQ_KEY      = "Enter_Groq_API";
  constexpr const char* GROQ_ENDPOINT = "https://api.groq.com/openai/v1/chat/completions";
  constexpr const char* GROQ_MODEL    = "llama-3.1-8b-instant";

  // Web search (Google Custom Search — unchanged)
  constexpr const char* WEATHER_KEY    = "939rjzks15cpa5b3umy5gqmw2mzrujqcllx4h8lh";
  constexpr const char* GOOGLE_API_KEY = "AIzaSyCf173a4BlwwaSIgnVh9JlMLPuRlEYQFew";
  constexpr const char* GOOGLE_CX     = "a2b354bd768bf4b57";

  // Timing
  constexpr int    NTP_OFFSET_SEC         = 19800;
  constexpr int    NTP_UPDATE_INTERVAL_MS = 60000;
  constexpr int    WIFI_RETRY_LIMIT       = 20;
  constexpr int    HTTP_TIMEOUT_MS        = 30000;
  constexpr int    SENTIMENT_TIMEOUT_MS   = 10000; 
  constexpr int    REMINDER_TIMEOUT_MS    = 15000;
  constexpr unsigned long PROACTIVE_INTERVAL_MS = 2700000UL;
  constexpr unsigned long REMINDER_ALERT_MS     = 10000UL;
  constexpr unsigned long REPLIED_FLASH_MS      = 2000UL;

  // Memory limits
  constexpr int    MAX_CHAT_TOKENS   = 1400;
  constexpr int    MAX_CHAT_MESSAGES = 20;
  constexpr int    MAX_MEMORY_FACTS  = 60;
  constexpr int    MAX_SENTIMENT_LOG = 30;
  constexpr int    MAX_REMINDERS     = 30;
  constexpr size_t CHAT_MSG_LEN     = 600;
  constexpr size_t ROLE_LEN        = 10;

  // AI generation params
  constexpr float  AI_TEMPERATURE  = 0.75f;
  constexpr int    AI_MAX_TOKENS   = 600;
  constexpr int    AI_MAX_RETRIES  = 3;

  // LED
  constexpr float  LED_MAX_BRIGHTNESS = 0.25f;
  constexpr float  LED_MIN_BRIGHTNESS = 0.05f;
  constexpr int    NEOPIXEL_PIN       = 48;
  constexpr int    NUMPIXELS          = 1;

  // Safety
  constexpr uint32_t HEAP_SAFE_BYTES = 30000;
  constexpr int      WDT_TIMEOUT_S   = 30;

  constexpr const char* VERSION = "ESP32-S3-AI v1.7-GROQ (Llama 3.1 8B)";
}

// ═══════════════════════════════════════════════════════
// SECTION 2 ── TYPES & DATA STRUCTURES  (unchanged)
// ═══════════════════════════════════════════════════════

enum RecurrenceType : uint8_t { ONCE, DAILY, WEEKLY, MONTHLY };

struct Reminder {
  String        message;
  uint8_t       hour, minute, dayOfWeek, dayOfMonth;
  RecurrenceType recurrence;
  bool          triggered;
  uint32_t      triggerCount;
};

struct Fact {
  String key, value;
  uint32_t     accessCount;
  unsigned long lastAccess;
};

struct ChatMessage {
  char role[Config::ROLE_LEN];
  char content[Config::CHAT_MSG_LEN];
};

struct SentimentLog {
  String sentiment;
  float  score;
  unsigned long timestamp;
};

struct UserPattern {
  int           totalInteractions = 0;
  int           morningChats      = 0;
  int           eveningChats      = 0;
  String        favoriteTopics[5];
  unsigned long lastInteraction   = 0;
  String        recentMood        = "neutral";
  int           techQuestions     = 0;
  int           casualMessages    = 0;
  int           reminderUsage     = 0;
};

struct KnowledgeArea {
  String  domain;
  int     experiencePoints;
  float   confidenceLevel;
  uint8_t colorR, colorG, colorB;
};

enum AIState : uint8_t {
  AI_IDLE, AI_THINKING, AI_REPLIED, AI_ERROR,
  AI_ALERT, AI_EXCITED, AI_CONCERNED, AI_PROACTIVE,
  AI_LEARNING, AI_EVOLVING
};

// ═══════════════════════════════════════════════════════
// SECTION 3 ── GLOBAL STATE  (unchanged)
// ═══════════════════════════════════════════════════════

WiFiUDP   ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", Config::NTP_OFFSET_SEC, Config::NTP_UPDATE_INTERVAL_MS);

String inputString    = "";
bool   stringComplete = false;

std::vector<Fact>         memory;
std::vector<Reminder>     reminders;
std::vector<ChatMessage>  chatHistory;
std::vector<SentimentLog> sentimentHistory;
std::vector<KnowledgeArea>knowledgeDomains;

UserPattern   userPattern;
int           consecutivePositive = 0;
int           consecutiveNegative = 0;
int           thinkingComplexity  = 0;

unsigned long bootTime            = 0;
unsigned long lastProactiveCheck  = 0;
bool          morningBriefingGiven = false;

// Runtime-overridable API settings (now points to Groq key)
String customApiKey      = Config::GROQ_KEY;
float  customTemperature = Config::AI_TEMPERATURE;
int    customMaxTokens   = Config::AI_MAX_TOKENS;

Adafruit_NeoPixel strip(Config::NUMPIXELS, Config::NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);
AIState       aiState        = AI_IDLE;
unsigned long stateChangeTime = 0;
unsigned long lastBlink       = 0;
int           blinkCount      = 0;
float         pulseBrightness = Config::LED_MIN_BRIGHTNESS;
bool          pulseIncreasing = true;

// ═══════════════════════════════════════════════════════
// SECTION 4 ── FORWARD DECLARATIONS
// ═══════════════════════════════════════════════════════

// Core Groq API
String groqSimpleCall(const String& prompt, float temp = 0.1f, int maxTok = 128);
String buildSystemPrompt();
String buildLiveContext();
String sendToGroq(const String& userMessage, const String& extraContext = "");
String sendToGroqWithRetry(const String& message, int maxRetries = Config::AI_MAX_RETRIES, const String& extraContext = "");
bool   aiIsUncertain(const String& reply);
String buildSearchQuery(const String& message);
void   autoLearnFromMessage(const String& userMsg);
void   calculateThinkingComplexity(const String& message);

// Web / tools
String fetchWebSearchResults(const String& query);
String httpGetWithRetry(const String& url, int maxRetries = 3, int delayMs = 2000);
void   getWeather(String city);
void   searchWeb(const String& query);
String urlEncode(const String& str);

// Memory
void   rememberFact(const String& key, const String& value);
String recallFact(const String& key);
void   removeFact(const String& key = "");
void   loadMemory();   void saveMemory();

// Reminders
bool   tryParseNaturalReminder(const String& message);
void   addReminder(const String& msg, int h, int m, RecurrenceType recur, int dow = 0, int dom = 0);
void   listReminders();
void   removeReminder(int index);
bool   shouldReminderTrigger(const Reminder& r);
String formatReminderTime(int hour, int minute);
String getRecurrenceText(RecurrenceType recur, int dow, int dom);
void   processReminders();
void   loadReminders();  void saveReminders();

// Chat history
void   addUserMessage(const String& msg);
void   addAssistantMessage(const String& msg);
void   limitChatHistoryByTokens(int maxTokens = Config::MAX_CHAT_TOKENS);
void   summarizeChatHistory();
void   loadChatHistory();  void saveChatHistory();

// Sentiment / mood
String detectSentiment(const String& message);
void   trackSentiment(const String& sentiment, float score);
void   respondToMood();
void   celebratePositiveVibes();
void   offerComfort();
void   smartResponseEnhancement(String& response);

// User patterns
void   updateUserPattern(const String& message);
String analyzeConversationTopic(const String& message);
void   loadUserPattern();  void saveUserPattern();
void   loadSentimentData();void saveSentimentData();

// Knowledge domains
void          initializeKnowledgeDomains();
void          updateKnowledgeDomain(const String& domain, int xpGain);
KnowledgeArea* getDominantKnowledge();
void          loadKnowledgeDomains(); void saveKnowledgeDomains();

// Proactive / briefing
bool   autoMorningBriefing();
void   checkProactiveOpportunity();
void   generateMorningBriefing();

// Misc handlers
void   handleInput(const String& input);
void   processConversation(const String& userMsg);
void   printHelp();
void   printVersion();
void   clearAll();

// LED
void   updateLED();
void   setLEDColor(uint8_t r, uint8_t g, uint8_t b, float brightness = 1.0f);
void   rainbowWave(int durationMs);

// Utilities
float  getCpuTemp();
void   systemDiagnostics();
bool   heapOk();
int    estimateTokens(const char* text);

// ═══════════════════════════════════════════════════════
// SECTION 5 ── SETUP
// ═══════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  delay(800);

  // Watchdog — must call init before reconfigure on some SDK versions
  esp_task_wdt_config_t wdt_config = {
    .timeout_ms    = Config::WDT_TIMEOUT_S * 1000,
    .idle_core_mask = (1 << 0),
    .trigger_panic  = true
  };
  esp_task_wdt_reconfigure(&wdt_config);
  esp_task_wdt_add(NULL);

  Serial.println("\n🚀 " + String(Config::VERSION) + " STARTING...");

  if (!FFat.begin(true)) {
    Serial.println("❌ FATFS mount failed — running without persistence");
  } else {
    Serial.println("✅ FATFS ready");
    loadMemory();
    loadReminders();
    loadChatHistory();
    loadUserPattern();
    loadSentimentData();
    loadKnowledgeDomains();
  }

  if (knowledgeDomains.empty()) initializeKnowledgeDomains();

  WiFi.begin(Config::SSID, Config::PASSWORD);
  Serial.print("📶 Connecting");
  for (int i = 0; i < Config::WIFI_RETRY_LIMIT && WiFi.status() != WL_CONNECTED; i++) {
    delay(500); Serial.print(".");
  }
  Serial.println(WiFi.status() == WL_CONNECTED
    ? "\n✅ WiFi connected — " + WiFi.localIP().toString()
    : "\n⚠️  WiFi failed — offline mode");

  timeClient.begin();
  timeClient.update();
  setTime(timeClient.getEpochTime());
  bootTime = millis();
  Serial.println("✅ Time synced");

  strip.begin();
  strip.clear();
  strip.show();
  rainbowWave(1800);
  strip.clear();
  strip.show();

  Serial.println("\n💡 " + String(Config::VERSION) + " READY");
  Serial.println("Model : " + String(Config::GROQ_MODEL));
  Serial.println("Type /help for commands\n");
}

// ═══════════════════════════════════════════════════════
// SECTION 6 ── MAIN LOOP
// ═══════════════════════════════════════════════════════
void loop() {
  esp_task_wdt_reset();

  timeClient.update();
  int nowHour   = hour();
  int nowMinute = minute();

  // WiFi auto-reconnect
  static unsigned long lastWifiCheck = 0;
  if (millis() - lastWifiCheck > 15000) {
    lastWifiCheck = millis();
    if (WiFi.status() != WL_CONNECTED) {
      WiFi.disconnect();
      WiFi.begin(Config::SSID, Config::PASSWORD);
    }
  }

  // Morning briefing at 07:30
  if (autoMorningBriefing() && nowHour == 7 && nowMinute == 30 && !morningBriefingGiven) {
    generateMorningBriefing();
    morningBriefingGiven = true;
  }
  if (nowHour != 7 || nowMinute != 30) morningBriefingGiven = false;

  // Proactive suggestions
  if (millis() - lastProactiveCheck > Config::PROACTIVE_INTERVAL_MS) {
    checkProactiveOpportunity();
    lastProactiveCheck = millis();
  }

  // Reminder check (once per minute)
  static int lastCheckedMinute = -1;
  if (nowMinute != lastCheckedMinute) {
    lastCheckedMinute = nowMinute;
    processReminders();
  }

  // Serial input
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') { stringComplete = true; break; }
    if (c != '\r') inputString += c;
  }

  if (stringComplete) {
    inputString.trim();
    if (inputString.length() > 0) handleInput(inputString);
    inputString    = "";
    stringComplete = false;
  }

  updateLED();
}

// ═══════════════════════════════════════════════════════
// SECTION 7 ── INPUT HANDLER
// ═══════════════════════════════════════════════════════
void handleInput(const String& input) {
  String lower = input;
  lower.toLowerCase();

  Serial.println("\nYou: " + input);
  updateUserPattern(input);

  if      (lower == "/help")    { printHelp(); }
  else if (lower == "/version") { printVersion(); }
  else if (lower == "/diag")    { systemDiagnostics(); }
  else if (lower == "/reminders" || lower == "/list") { listReminders(); }
  else if (lower.startsWith("/remove") || lower.startsWith("/delete")) {
    int idx = input.substring(input.indexOf(' ') + 1).toInt();
    if (idx >= 0 && idx < (int)reminders.size()) {
      Serial.println("✅ Removed: " + reminders[idx].message);
      removeReminder(idx);
    } else {
      Serial.println("❌ Invalid index. Use /reminders to see the list.");
    }
  }
  else if (lower.startsWith("/weather")) { getWeather(input.substring(8)); }
  else if (lower.startsWith("/search"))  { searchWeb(input.substring(8)); }
  else if (lower == "/clear")            { clearAll(); }
  else {
    bool wasReminder = tryParseNaturalReminder(input);
    if (!wasReminder) processConversation(input);
  }
}

// ═══════════════════════════════════════════════════════
// SECTION 8 ── CONVERSATION ENGINE
// ═══════════════════════════════════════════════════════
void processConversation(const String& userMsg) {
  if (!heapOk()) {
    Serial.println("⚠️  Low memory — skipping AI call. Try /clear to free space.");
    return;
  }

  addUserMessage(userMsg);
  calculateThinkingComplexity(userMsg);
  aiState = AI_THINKING;
  updateLED();

  // Sentiment detection
  String sentiment    = "neutral";
  float  sentimentScore = 0.5f;
  String sentResult   = detectSentiment(userMsg);
  int    scoreStart   = sentResult.indexOf('(');
  if (scoreStart > 0) {
    sentimentScore = sentResult.substring(scoreStart + 1, sentResult.indexOf(')')).toFloat();
    sentiment      = sentResult.substring(0, scoreStart - 1);
  } else {
    sentiment = sentResult;
  }
  trackSentiment(sentiment, sentimentScore);

  // Primary AI call
  String aiReply = sendToGroqWithRetry(userMsg);

  // Web search fallback when AI is uncertain
  if (aiIsUncertain(aiReply) && WiFi.status() == WL_CONNECTED) {
    Serial.println("🔍 Searching web for better answer...");
    String webCtx = fetchWebSearchResults(buildSearchQuery(userMsg));
    if (webCtx.length() > 0) {
      aiReply = sendToGroqWithRetry(userMsg, Config::AI_MAX_RETRIES, webCtx);
    }
  }

  autoLearnFromMessage(userMsg);
  String topic = analyzeConversationTopic(userMsg);
  if (topic.length() > 0) updateKnowledgeDomain(topic, 10);

  smartResponseEnhancement(aiReply);

  Serial.println("\n 🤖AI: " + aiReply);
  addAssistantMessage(aiReply);

  respondToMood();

  aiState         = AI_REPLIED;
  stateChangeTime = millis();
}

// ═══════════════════════════════════════════════════════
// SECTION 9 ── SYSTEM PROMPT  (unchanged logic)
// ═══════════════════════════════════════════════════════
String buildSystemPrompt() {
  String tone = "balanced and helpful";
  if (userPattern.techQuestions > userPattern.casualMessages * 2)
    tone = "precise, technical, and concise";
  else if (userPattern.casualMessages > userPattern.techQuestions * 2)
    tone = "warm, conversational, and friendly";
  else if (userPattern.reminderUsage > 5)
    tone = "organised, time-aware, and proactive";

  String factSummary = "";
  std::vector<Fact*> sortedFacts;
  for (auto& f : memory) sortedFacts.push_back(&f);
  std::sort(sortedFacts.begin(), sortedFacts.end(),
    [](Fact* a, Fact* b){ return a->accessCount > b->accessCount; });
  int factLimit = min((int)sortedFacts.size(), 8);
  for (int i = 0; i < factLimit; i++)
    factSummary += "  • " + sortedFacts[i]->key + ": " + sortedFacts[i]->value + "\n";

  String topicsStr = "";
  for (int i = 0; i < 5; i++)
    if (userPattern.favoriteTopics[i].length() > 0)
      topicsStr += userPattern.favoriteTopics[i] + " ";

  String prompt =
    "You are ESP32-S3-AI v1.7, an intelligent embedded assistant running on an ESP32-S3 microcontroller.\n"
    "You are powered by Llama 3.1 8B via Groq.\n"
    "Your personality tone: " + tone + ".\n\n"

    "## Core Reasoning Rules\n"
    "1. Think step-by-step before answering complex questions.\n"
    "2. If you are uncertain about a fact, say so honestly — don't hallucinate.\n"
    "3. Keep responses concise (1-4 sentences for simple questions, up to 8 for complex ones).\n"
    "4. When the user sets a reminder, confirm it clearly with time and recurrence.\n"
    "5. Adapt your language complexity to match the user's messages.\n"
    "6. Reference previously learned facts about the user to feel personalised.\n\n"

    "## Capabilities You Have\n"
    "- Answer general knowledge questions\n"
    "- Set/manage reminders (one-time, daily, weekly, monthly)\n"
    "- Check weather for any city\n"
    "- Search the web when uncertain\n"
    "- Remember personal facts across sessions\n\n"

    "## What You Do NOT Do\n"
    "- Do not make up specific facts, dates, or numbers you are unsure of.\n"
    "- Do not roleplay as a different AI.\n"
    "- Do not produce harmful content.\n\n"

    "## User Profile\n"
    "  Interactions: " + String(userPattern.totalInteractions) + "\n"
    "  Current mood: " + userPattern.recentMood + "\n"
    "  Interests: " + (topicsStr.length() > 0 ? topicsStr : "not yet known") + "\n"
    "  Tech questions asked: " + String(userPattern.techQuestions) + "\n"
    "  Reminder usage count: " + String(userPattern.reminderUsage) + "\n\n";

  if (factSummary.length() > 0)
    prompt += "## Known User Facts\n" + factSummary + "\n";

  return prompt;
}

String buildLiveContext() {
  int  h = hour(), m = minute();
  String ampm   = (h >= 12) ? "PM" : "AM";
  int   displayH = (h > 12) ? h - 12 : (h == 0 ? 12 : h);

  char timeBuf[12], dateBuf[12];
  sprintf(timeBuf, "%d:%02d %s", displayH, m, ampm.c_str());
  sprintf(dateBuf, "%02d/%02d/%04d", day(), month(), year());

  const char* dayNames[] = {"","Sun","Mon","Tue","Wed","Thu","Fri","Sat"};

  String ctx = "\n## Live Context\n";
  ctx += "  Time: " + String(timeBuf) + " (" + String(dayNames[weekday()]) + " " + String(dateBuf) + ")\n";
  ctx += "  Free heap: " + String(ESP.getFreeHeap()) + " bytes\n";

  if (!reminders.empty()) {
    ctx += "  Active reminders: " + String(reminders.size()) + "\n";
    ctx += "  Next: \"" + reminders[0].message + "\" at " +
           formatReminderTime(reminders[0].hour, reminders[0].minute) + "\n";
  }
  return ctx;
}

// ═══════════════════════════════════════════════════════
// SECTION 10 ── GROQ API  (replaces all Gemini calls)
// ═══════════════════════════════════════════════════════

// ── groqSimpleCall ──────────────────────────────────────
// Lightweight single-turn call used by sentiment, auto-learn,
// and reminder parsing. Returns raw text or "" on failure.
String groqSimpleCall(const String& prompt, float temp, int maxTok) {
  if (!heapOk() || WiFi.status() != WL_CONNECTED) return "";

  HTTPClient http;
  http.setTimeout(Config::SENTIMENT_TIMEOUT_MS);
  http.begin(Config::GROQ_ENDPOINT);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", "Bearer " + customApiKey);

  DynamicJsonDocument reqDoc(4096);
  reqDoc["model"]       = Config::GROQ_MODEL;
  reqDoc["temperature"] = temp;
  reqDoc["max_tokens"]  = maxTok;

  JsonArray msgs    = reqDoc.createNestedArray("messages");
  JsonObject userMsg = msgs.createNestedObject();
  userMsg["role"]    = "user";
  userMsg["content"] = prompt;

  String body; serializeJson(reqDoc, body);
  String result = "";

  int code = http.POST(body);
  if (code > 0) {
    DynamicJsonDocument resp(4096);
    if (!deserializeJson(resp, http.getString()) && resp.containsKey("choices")) {
      result = resp["choices"][0]["message"]["content"].as<String>();
      result.trim();
    }
  }
  http.end();
  return result;
}

// ── sendToGroq ──────────────────────────────────────────
// Full multi-turn chat call with system prompt + history.
String sendToGroq(const String& userMessage, const String& extraContext) {
  String reply = "";

  HTTPClient http;
  http.setTimeout(Config::HTTP_TIMEOUT_MS);
  http.begin(Config::GROQ_ENDPOINT);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", "Bearer " + customApiKey);

  DynamicJsonDocument doc(32768);
  doc["model"]       = Config::GROQ_MODEL;
  doc["temperature"] = customTemperature;
  doc["max_tokens"]  = customMaxTokens;
  doc["top_p"]       = 0.9;

  JsonArray messages = doc.createNestedArray("messages");

  // System message (first in the array — OpenAI-compatible format)
  String sysText = buildSystemPrompt() + buildLiveContext();
  JsonObject sysEntry = messages.createNestedObject();
  sysEntry["role"]    = "system";
  sysEntry["content"] = sysText;

  // Chat history — map stored "model" role → "assistant" for Groq
  for (const ChatMessage& cm : chatHistory) {
    String role = String(cm.role);
    if (role == "system") continue;
    if (role == "model")  role = "assistant"; // Gemini compat shim
    JsonObject entry    = messages.createNestedObject();
    entry["role"]       = role;
    entry["content"]    = cm.content;
  }

  // Current user turn (with optional web search context)
  String finalMsg = userMessage;
  if (extraContext.length() > 0) {
    finalMsg = "[Web Search Results]\n" + extraContext +
               "\n\n[User Question]\n" + userMessage +
               "\n\nPlease answer using the search results above where relevant.";
  }
  JsonObject userEntry = messages.createNestedObject();
  userEntry["role"]    = "user";
  userEntry["content"] = finalMsg;

  String body;
  serializeJson(doc, body);

  int code = http.POST(body);
  if (code > 0) {
    DynamicJsonDocument resp(32768);
    DeserializationError err = deserializeJson(resp, http.getString());
    if (!err && resp.containsKey("choices")) {
      reply = resp["choices"][0]["message"]["content"].as<String>();
      reply.trim();
      aiState         = AI_REPLIED;
      stateChangeTime = millis();
    } else if (!err && resp.containsKey("error")) {
      reply   = "❌ Groq: " + resp["error"]["message"].as<String>();
      aiState = AI_ERROR; blinkCount = 0; lastBlink = millis();
    } else {
      reply   = "❌ Parse error";
      aiState = AI_ERROR; blinkCount = 0; lastBlink = millis();
    }
  } else {
    reply   = "❌ HTTP error: " + String(code);
    aiState = AI_ERROR; blinkCount = 0; lastBlink = millis();
  }

  http.end();
  return reply;
}

// ── sendToGroqWithRetry ─────────────────────────────────
String sendToGroqWithRetry(const String& message, int maxRetries, const String& extraContext) {
  String reply;
  for (int i = 1; i <= maxRetries; i++) {
    reply = sendToGroq(message, extraContext);
    if (!reply.startsWith("❌")) return reply;
    delay(1500 * i);
  }
  return reply;
}

// ── aiIsUncertain ───────────────────────────────────────
bool aiIsUncertain(const String& reply) {
  String r = reply; r.toLowerCase();
  static const char* phrases[] = {
    "i don't know","i do not know","i'm not sure","i am not sure",
    "i'm not certain","i don't have","i cannot find","i can't find",
    "my knowledge","knowledge cutoff","i'm unable to","no information",
    "couldn't find","could not find","beyond my knowledge", nullptr
  };
  for (int i = 0; phrases[i]; i++) if (r.indexOf(phrases[i]) >= 0) return true;
  return false;
}

// ── buildSearchQuery ────────────────────────────────────
String buildSearchQuery(const String& message) {
  String q = message, lq = message;
  lq.toLowerCase();
  static const char* fillers[] = {
    "can you tell me","tell me about","what is","what are","who is",
    "who are","please","could you","i want to know","look up",
    "search for","find","google","?", nullptr
  };
  for (int i = 0; fillers[i]; i++) {
    int idx = lq.indexOf(fillers[i]);
    if (idx >= 0) { q.remove(idx, strlen(fillers[i])); lq.remove(idx, strlen(fillers[i])); }
  }
  q.trim();
  return (q.length() == 0) ? message : q;
}

// ═══════════════════════════════════════════════════════
// SECTION 11 ── AUTO-LEARN  (now uses groqSimpleCall)
// ═══════════════════════════════════════════════════════
void autoLearnFromMessage(const String& userMsg) {
  if (!heapOk()) return;

  String prompt =
    "Analyze this message for a single important personal fact worth remembering long-term.\n"
    "Message: \"" + userMsg + "\"\n\n"
    "Rules:\n"
    "- Only extract proper personal info: name, age, job, city, hobby, preference, relationship.\n"
    "- Ignore questions, reminders, weather queries, or generic statements.\n"
    "- Key must be a short lowercase label (e.g. name, city, job).\n"
    "- Value must be specific (e.g. 'Colombo', not 'a city').\n\n"
    "If a clear fact exists: {\"learned\":true,\"key\":\"city\",\"value\":\"Colombo\"}\n"
    "Otherwise: {\"learned\":false}\n"
    "JSON only, no explanation, no markdown.";

  String raw = groqSimpleCall(prompt, 0.1f, 64);
  if (raw.length() == 0) return;

  // Strip markdown fences if Llama adds them
  if (raw.startsWith("```")) {
    int s = raw.indexOf('\n') + 1, e = raw.lastIndexOf("```");
    if (e > s) raw = raw.substring(s, e);
    raw.trim();
  }

  DynamicJsonDocument parsed(256);
  if (deserializeJson(parsed, raw) || !parsed["learned"].as<bool>()) return;

  String key = parsed["key"].as<String>();
  String val = parsed["value"].as<String>();
  if (key.length() > 1 && key.length() < 30 && val.length() > 0 && val.length() < 100) {
    rememberFact(key, val);
    Serial.println("💾 Learned: " + key + " = " + val);
  }
}

// ═══════════════════════════════════════════════════════
// SECTION 12 ── REMINDER SYSTEM  (parser uses groqSimpleCall)
// ═══════════════════════════════════════════════════════
String formatReminderTime(int h, int m) {
  String ap = (h >= 12) ? "PM" : "AM";
  int dh    = (h > 12) ? h - 12 : (h == 0 ? 12 : h);
  char buf[12]; sprintf(buf, "%d:%02d %s", dh, m, ap.c_str());
  return String(buf);
}

String getRecurrenceText(RecurrenceType r, int dow, int dom) {
  static const char* days[] = {"","Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"};
  switch (r) {
    case ONCE:    return "(one-time)";
    case DAILY:   return "(daily)";
    case WEEKLY:  return String("(every ") + days[dow] + ")";
    case MONTHLY: return String("(monthly on the ") + String(dom) + ")";
    default:      return "";
  }
}

bool shouldReminderTrigger(const Reminder& r) {
  if (r.hour != (uint8_t)hour() || r.minute != (uint8_t)minute()) return false;
  switch (r.recurrence) {
    case ONCE: case DAILY: return true;
    case WEEKLY:  return r.dayOfWeek  == (uint8_t)weekday();
    case MONTHLY: return r.dayOfMonth == (uint8_t)day();
    default:      return false;
  }
}

void processReminders() {
  for (auto& r : reminders) {
    if (shouldReminderTrigger(r) && !r.triggered) {
      Serial.println("\n⏰ ═══════════════════════════════════════");
      Serial.println("   REMINDER: " + r.message);
      Serial.println("   " + formatReminderTime(r.hour, r.minute) + " " +
                     getRecurrenceText(r.recurrence, r.dayOfWeek, r.dayOfMonth));
      Serial.println("   ════════════════════════════════════════\n");
      aiState         = AI_ALERT;
      pulseBrightness = Config::LED_MIN_BRIGHTNESS;
      pulseIncreasing = true;
      r.triggered     = true;
      r.triggerCount++;
      stateChangeTime = millis();
      saveReminders();
    } else if (r.hour != (uint8_t)hour() || r.minute != (uint8_t)minute()) {
      r.triggered = false;
    }
  }
  reminders.erase(std::remove_if(reminders.begin(), reminders.end(),
    [](const Reminder& r){ return r.recurrence == ONCE && r.triggered; }), reminders.end());
}

void addReminder(const String& msg, int h, int m, RecurrenceType recur, int dow, int dom) {
  if (reminders.size() >= Config::MAX_REMINDERS) {
    Serial.println("⚠️  Reminder limit reached (" + String(Config::MAX_REMINDERS) + "). Remove one first.");
    return;
  }
  Reminder r;
  r.message = msg; r.hour = h; r.minute = m;
  r.recurrence = recur; r.dayOfWeek = dow; r.dayOfMonth = dom;
  r.triggered = false; r.triggerCount = 0;
  reminders.push_back(r);
  saveReminders();
  userPattern.reminderUsage++;
}

void listReminders() {
  if (reminders.empty()) { Serial.println("⏰ No reminders set."); return; }
  Serial.println("\n⏰ Reminders (" + String(reminders.size()) + "):");
  Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
  for (size_t i = 0; i < reminders.size(); i++) {
    const Reminder& r = reminders[i];
    Serial.printf("[%d] %s  @ %s %s", (int)i, r.message.c_str(),
      formatReminderTime(r.hour, r.minute).c_str(),
      getRecurrenceText(r.recurrence, r.dayOfWeek, r.dayOfMonth).c_str());
    if (r.triggerCount > 0) Serial.print(" [fired " + String(r.triggerCount) + "x]");
    Serial.println();
  }
  Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
  Serial.println("💡 /remove [number] to delete");
}

void removeReminder(int idx) {
  if (idx < 0 || idx >= (int)reminders.size()) return;
  reminders.erase(reminders.begin() + idx);
  saveReminders();
}

// ── Natural Language Reminder Parser ────────────────────
bool tryParseNaturalReminder(const String& message) {
  String lMsg = message; lMsg.toLowerCase();
  if (lMsg.indexOf("remind") < 0 && lMsg.indexOf("alarm") < 0 &&
      lMsg.indexOf("alert") < 0 && lMsg.indexOf("don't forget") < 0 &&
      lMsg.indexOf("notify") < 0) return false;

  char nowBuf[40];
  sprintf(nowBuf, "%02d:%02d Day:%d Date:%d", hour(), minute(), weekday(), day());

  String extractPrompt =
    "Now: " + String(nowBuf) + " (1=Sunday..7=Saturday)\n"
    "User: \"" + message + "\"\n\n"
    "Extract reminder. Support: 3PM, 2:30pm, 15:00, daily, weekly+day, monthly+date.\n"
    "Existing reminders count: " + String(reminders.size()) + "\n\n"
    "Output JSON only (no markdown, no explanation):\n"
    "{\"isReminder\":true,\"message\":\"short action\",\"hour\":15,\"minute\":0,"
    "\"recurrence\":\"once|daily|weekly|monthly\",\"dayOfWeek\":0,\"dayOfMonth\":0}\n"
    "or {\"isReminder\":false}\n"
    "hour is 24-format (0-23). dayOfWeek 1=Sunday. dayOfMonth 1-31.";

  String raw = groqSimpleCall(extractPrompt, 0.05f, 128);
  if (raw.length() == 0) return false;

  // Strip markdown fences
  if (raw.startsWith("```")) {
    int s = raw.indexOf('\n') + 1, e = raw.lastIndexOf("```");
    if (e > s) raw = raw.substring(s, e);
    raw.trim();
  }

  DynamicJsonDocument parsed(512);
  if (deserializeJson(parsed, raw) || !parsed["isReminder"].as<bool>()) return false;

  String msg      = parsed["message"].as<String>();
  int    h        = parsed["hour"].as<int>();
  int    m        = parsed["minute"].as<int>();
  String recurStr = parsed["recurrence"] | "once";
  int    dow      = parsed["dayOfWeek"]  | 0;
  int    dom      = parsed["dayOfMonth"] | 0;

  if (msg.length() == 0 || h < 0 || h > 23 || m < 0 || m > 59) {
    Serial.println("⚠️  Could not parse reminder details. Please be more specific.");
    return false;
  }

  RecurrenceType recur = ONCE;
  if (recurStr == "daily")   recur = DAILY;
  else if (recurStr == "weekly")  { recur = WEEKLY;  if (!dow) dow = weekday(); }
  else if (recurStr == "monthly") { recur = MONTHLY; if (!dom) dom = day(); }

  addReminder(msg, h, m, recur, dow, dom);
  Serial.println("✅ Reminder set!");
  Serial.println("   📌 " + msg);
  Serial.println("   🕐 " + formatReminderTime(h, m) + " " + getRecurrenceText(recur, dow, dom));
  return true;
}

// ── Reminder persistence ─────────────────────────────────
void saveReminders() {
  DynamicJsonDocument doc(16384);
  JsonArray arr = doc.createNestedArray("reminders");
  for (const auto& r : reminders) {
    JsonObject o = arr.createNestedObject();
    o["message"] = r.message; o["hour"] = r.hour; o["minute"] = r.minute;
    o["recurrence"] = (int)r.recurrence;
    o["dayOfWeek"] = r.dayOfWeek; o["dayOfMonth"] = r.dayOfMonth;
    o["triggerCount"] = r.triggerCount;
  }
  File f = FFat.open("/reminders.json", FILE_WRITE);
  if (f) { serializeJson(doc, f); f.close(); }
}

void loadReminders() {
  if (!FFat.exists("/reminders.json")) return;
  File f = FFat.open("/reminders.json", FILE_READ);
  if (!f) return;
  DynamicJsonDocument doc(16384);
  if (deserializeJson(doc, f)) { f.close(); return; }
  reminders.clear();
  for (JsonObject o : doc["reminders"].as<JsonArray>()) {
    Reminder r;
    r.message    = o["message"].as<String>();
    r.hour       = o["hour"] | 0; r.minute    = o["minute"] | 0;
    r.recurrence = (RecurrenceType)(o["recurrence"] | 0);
    r.dayOfWeek  = o["dayOfWeek"] | 0; r.dayOfMonth = o["dayOfMonth"] | 0;
    r.triggerCount = o["triggerCount"] | 0;
    r.triggered = false;
    reminders.push_back(r);
  }
  f.close();
}

// ═══════════════════════════════════════════════════════
// SECTION 13 ── MEMORY  (unchanged)
// ═══════════════════════════════════════════════════════
void rememberFact(const String& key, const String& value) {
  if (key.length() == 0 || value.length() == 0) return;
  for (auto& f : memory) {
    if (f.key.equalsIgnoreCase(key)) {
      f.value = value; f.lastAccess = millis(); f.accessCount++;
      saveMemory(); return;
    }
  }
  if (memory.size() >= Config::MAX_MEMORY_FACTS) {
    auto it = std::min_element(memory.begin(), memory.end(),
      [](const Fact& a, const Fact& b){ return a.accessCount < b.accessCount; });
    memory.erase(it);
  }
  memory.push_back({key, value, 1, millis()});
  saveMemory();
}

String recallFact(const String& key) {
  for (auto& f : memory) {
    if (f.key.equalsIgnoreCase(key)) {
      f.accessCount++; f.lastAccess = millis();
      return f.value;
    }
  }
  return "";
}

void removeFact(const String& key) {
  if (key.length() == 0) memory.clear();
  else memory.erase(std::remove_if(memory.begin(), memory.end(),
    [&](const Fact& f){ return f.key.equalsIgnoreCase(key); }), memory.end());
  saveMemory();
}

void saveMemory() {
  DynamicJsonDocument doc(32768);
  JsonArray arr = doc.createNestedArray("memory");
  for (const auto& f : memory) {
    JsonObject o = arr.createNestedObject();
    o["key"] = f.key; o["value"] = f.value;
    o["accessCount"] = f.accessCount; o["lastAccess"] = f.lastAccess;
  }
  File file = FFat.open("/memory.json", FILE_WRITE);
  if (file) { serializeJson(doc, file); file.close(); }
}

void loadMemory() {
  if (!FFat.exists("/memory.json")) return;
  File file = FFat.open("/memory.json", FILE_READ);
  if (!file) return;
  DynamicJsonDocument doc(32768);
  if (deserializeJson(doc, file)) { file.close(); return; }
  memory.clear();
  for (JsonObject f : doc["memory"].as<JsonArray>())
    memory.push_back({f["key"].as<String>(), f["value"].as<String>(),
      f["accessCount"] | 1, f["lastAccess"] | 0UL});
  file.close();
}

// ═══════════════════════════════════════════════════════
// SECTION 14 ── SENTIMENT & MOOD  (uses groqSimpleCall)
// ═══════════════════════════════════════════════════════
String detectSentiment(const String& message) {
  String lower = message; lower.toLowerCase();
  int pos = 0, neg = 0;

  struct KW { const char* word; int score; };
  static const KW positive[] = {
    {"great",2},{"awesome",2},{"love",2},{"happy",2},{"excellent",2},
    {"wonderful",2},{"fantastic",2},{"thank",1},{"good",1},{"nice",1},
    {"!",1},{"😊",2},{"😄",2},{"😍",2},{nullptr,0}
  };
  static const KW negative[] = {
    {"terrible",2},{"awful",2},{"hate",2},{"sad",2},{"angry",2},
    {"frustrated",2},{"horrible",2},{"bad",1},{"worst",2},{"annoying",1},
    {"😞",2},{"😢",2},{"😡",2},{nullptr,0}
  };
  for (int i = 0; positive[i].word; i++) if (lower.indexOf(positive[i].word) >= 0) pos += positive[i].score;
  for (int i = 0; negative[i].word; i++) if (lower.indexOf(negative[i].word) >= 0) neg += negative[i].score;

  if (pos >= 3 && neg == 0) return "positive (0.85)";
  if (neg >= 3 && pos == 0) return "negative (0.85)";
  if (pos == 0 && neg == 0) return "neutral (0.5)";

  // Ambiguous — quick Groq call
  String prompt =
    "Classify the sentiment of this message in JSON only (no markdown):\n"
    "\"" + message + "\"\n"
    "Respond only with: {\"s\":\"positive\",\"c\":0.8} or {\"s\":\"negative\",\"c\":0.75} or {\"s\":\"neutral\",\"c\":0.5}";

  String raw = groqSimpleCall(prompt, 0.1f, 32);
  if (raw.length() > 0) {
    // Strip fences
    if (raw.startsWith("```")) {
      int s = raw.indexOf('\n') + 1, e = raw.lastIndexOf("```");
      if (e > s) raw = raw.substring(s, e); raw.trim();
    }
    DynamicJsonDocument p(128);
    if (!deserializeJson(p, raw))
      return p["s"].as<String>() + " (" + String(p["c"] | 0.5f, 2) + ")";
  }
  return "neutral (0.5)";
}

void trackSentiment(const String& sentiment, float score) {
  sentimentHistory.push_back({sentiment, score, millis()});
  if (sentimentHistory.size() > Config::MAX_SENTIMENT_LOG)
    sentimentHistory.erase(sentimentHistory.begin());

  if      (sentiment == "positive") { consecutivePositive++; consecutiveNegative = 0; }
  else if (sentiment == "negative") { consecutiveNegative++; consecutivePositive = 0; }
  else                              { consecutivePositive = 0; consecutiveNegative = 0; }

  userPattern.recentMood = sentiment;
  saveSentimentData();
  saveUserPattern();
}

void respondToMood() {
  if (consecutivePositive >= 3) { celebratePositiveVibes(); consecutivePositive = 0; }
  if (consecutiveNegative >= 2) { offerComfort();           consecutiveNegative = 0; }
}

void celebratePositiveVibes() {
  Serial.println("\n✨ Great energy today! 🌟");
  aiState = AI_EXCITED; stateChangeTime = millis();
}

void offerComfort() {
  Serial.println("\n💙 Seems like things might be tough. I'm here if you want to talk.");
  aiState = AI_CONCERNED; stateChangeTime = millis();
}

void smartResponseEnhancement(String& response) {
  if (userPattern.recentMood == "negative" && response.indexOf("?") < 0)
    if (random(100) < 30) response += " Let me know if there's anything else I can help with.";
}

// ═══════════════════════════════════════════════════════
// SECTION 15 ── USER PATTERNS & LEARNING  (unchanged)
// ═══════════════════════════════════════════════════════
void updateUserPattern(const String& message) {
  userPattern.totalInteractions++;
  userPattern.lastInteraction = millis();

  int h = hour();
  if (h >= 5  && h < 12) userPattern.morningChats++;
  if (h >= 18 && h < 24) userPattern.eveningChats++;

  String lower = message; lower.toLowerCase();
  if (lower.indexOf("code") >= 0 || lower.indexOf("program") >= 0 ||
      lower.indexOf("error") >= 0 || lower.indexOf("function") >= 0)
    userPattern.techQuestions++;
  else if (lower.length() < 60 && lower.indexOf("?") < 0)
    userPattern.casualMessages++;

  String topic = analyzeConversationTopic(message);
  if (topic.length() > 0) {
    bool exists = false;
    for (int i = 0; i < 5; i++) if (userPattern.favoriteTopics[i] == topic) { exists = true; break; }
    if (!exists) for (int i = 0; i < 5; i++) {
      if (userPattern.favoriteTopics[i].length() == 0) { userPattern.favoriteTopics[i] = topic; break; }
    }
  }
}

String analyzeConversationTopic(const String& message) {
  String lower = message; lower.toLowerCase();
  if (lower.indexOf("weather")  >= 0) return "weather";
  if (lower.indexOf("remind")   >= 0) return "reminders";
  if (lower.indexOf("code")     >= 0 || lower.indexOf("program") >= 0) return "technical";
  if (lower.indexOf("news")     >= 0) return "news";
  if (lower.indexOf("joke")     >= 0) return "entertainment";
  if (lower.indexOf("help")     >= 0) return "help";
  return "";
}

void calculateThinkingComplexity(const String& message) {
  int c = 1;
  if (message.length() > 100) c += 3; else if (message.length() > 50) c += 1;
  String lower = message; lower.toLowerCase();
  if (lower.indexOf("why")     >= 0) c += 2;
  if (lower.indexOf("how")     >= 0) c += 1;
  if (lower.indexOf("explain") >= 0) c += 2;
  if (lower.indexOf("compare") >= 0) c += 3;
  if (lower.indexOf("analyze") >= 0) c += 3;
  if (lower.indexOf("code")    >= 0) c += 2;
  thinkingComplexity = min(10, c);
}

void saveUserPattern() {
  DynamicJsonDocument doc(2048);
  doc["total"]    = userPattern.totalInteractions;
  doc["morning"]  = userPattern.morningChats;
  doc["evening"]  = userPattern.eveningChats;
  doc["lastTime"] = userPattern.lastInteraction;
  doc["mood"]     = userPattern.recentMood;
  doc["tech"]     = userPattern.techQuestions;
  doc["casual"]   = userPattern.casualMessages;
  doc["remUsage"] = userPattern.reminderUsage;
  JsonArray topics = doc.createNestedArray("topics");
  for (int i = 0; i < 5; i++) if (userPattern.favoriteTopics[i].length()) topics.add(userPattern.favoriteTopics[i]);
  File f = FFat.open("/pattern.json", FILE_WRITE);
  if (f) { serializeJson(doc, f); f.close(); }
}

void loadUserPattern() {
  if (!FFat.exists("/pattern.json")) return;
  File f = FFat.open("/pattern.json", FILE_READ);
  if (!f) return;
  DynamicJsonDocument doc(2048);
  if (deserializeJson(doc, f)) { f.close(); return; }
  userPattern.totalInteractions = doc["total"]    | 0;
  userPattern.morningChats      = doc["morning"]  | 0;
  userPattern.eveningChats      = doc["evening"]  | 0;
  userPattern.lastInteraction   = doc["lastTime"] | 0UL;
  userPattern.recentMood        = doc["mood"]     | "neutral";
  userPattern.techQuestions     = doc["tech"]     | 0;
  userPattern.casualMessages    = doc["casual"]   | 0;
  userPattern.reminderUsage     = doc["remUsage"] | 0;
  if (doc.containsKey("topics")) {
    int i = 0;
    for (JsonVariant t : doc["topics"].as<JsonArray>())
      if (i < 5) userPattern.favoriteTopics[i++] = t.as<String>();
  }
  f.close();
}

void saveSentimentData() {
  DynamicJsonDocument doc(8192);
  JsonArray arr = doc.createNestedArray("history");
  for (const auto& s : sentimentHistory) {
    JsonObject o = arr.createNestedObject();
    o["s"] = s.sentiment; o["c"] = s.score; o["t"] = s.timestamp;
  }
  File f = FFat.open("/sentiment.json", FILE_WRITE);
  if (f) { serializeJson(doc, f); f.close(); }
}

void loadSentimentData() {
  if (!FFat.exists("/sentiment.json")) return;
  File f = FFat.open("/sentiment.json", FILE_READ);
  if (!f) return;
  DynamicJsonDocument doc(8192);
  if (deserializeJson(doc, f)) { f.close(); return; }
  sentimentHistory.clear();
  for (JsonObject o : doc["history"].as<JsonArray>())
    sentimentHistory.push_back({o["s"].as<String>(), o["c"] | 0.5f, o["t"] | 0UL});
  f.close();
}

// ═══════════════════════════════════════════════════════
// SECTION 16 ── KNOWLEDGE DOMAINS  (unchanged)
// ═══════════════════════════════════════════════════════
void initializeKnowledgeDomains() {
  knowledgeDomains = {
    {"personal",     0, 0.3f, 255, 200, 100},
    {"technical",    0, 0.3f, 100, 150, 255},
    {"weather",      0, 0.3f, 150, 220, 255},
    {"reminders",    0, 0.3f, 180, 100, 255},
    {"general",      0, 0.3f, 100, 255, 150},
    {"news",         0, 0.3f, 255, 150,  50},
    {"entertainment",0, 0.3f, 255, 100, 200},
  };
  saveKnowledgeDomains();
}

void updateKnowledgeDomain(const String& domain, int xpGain) {
  for (auto& kd : knowledgeDomains) {
    if (kd.domain == domain) {
      kd.experiencePoints += xpGain;
      kd.confidenceLevel   = min(0.95f, 0.3f + kd.experiencePoints / 500.0f);
      saveKnowledgeDomains(); return;
    }
  }
  for (auto& kd : knowledgeDomains)
    if (kd.domain == "general") { kd.experiencePoints += xpGain; saveKnowledgeDomains(); return; }
}

KnowledgeArea* getDominantKnowledge() {
  if (knowledgeDomains.empty()) return nullptr;
  return &*std::max_element(knowledgeDomains.begin(), knowledgeDomains.end(),
    [](const KnowledgeArea& a, const KnowledgeArea& b){ return a.experiencePoints < b.experiencePoints; });
}

void saveKnowledgeDomains() {
  DynamicJsonDocument doc(4096);
  JsonArray arr = doc.createNestedArray("domains");
  for (const auto& kd : knowledgeDomains) {
    JsonObject o = arr.createNestedObject();
    o["domain"] = kd.domain; o["xp"] = kd.experiencePoints; o["conf"] = kd.confidenceLevel;
    o["r"] = kd.colorR; o["g"] = kd.colorG; o["b"] = kd.colorB;
  }
  File f = FFat.open("/knowledge.json", FILE_WRITE);
  if (f) { serializeJson(doc, f); f.close(); }
}

void loadKnowledgeDomains() {
  if (!FFat.exists("/knowledge.json")) return;
  File f = FFat.open("/knowledge.json", FILE_READ);
  if (!f) return;
  DynamicJsonDocument doc(4096);
  if (deserializeJson(doc, f)) { f.close(); return; }
  knowledgeDomains.clear();
  for (JsonObject o : doc["domains"].as<JsonArray>())
    knowledgeDomains.push_back({o["domain"].as<String>(), o["xp"]|0, o["conf"]|0.3f,
      (uint8_t)(o["r"]|255),(uint8_t)(o["g"]|255),(uint8_t)(o["b"]|255)});
  f.close();
  Serial.println("🧬 Loaded " + String(knowledgeDomains.size()) + " knowledge domains");
}

// ═══════════════════════════════════════════════════════
// SECTION 17 ── WEB / WEATHER  (unchanged)
// ═══════════════════════════════════════════════════════
String fetchWebSearchResults(const String& query) {
  if (query.length() == 0) return "";
  String url = "https://www.googleapis.com/customsearch/v1?key=" + String(Config::GOOGLE_API_KEY)
             + "&cx=" + String(Config::GOOGLE_CX) + "&q=" + urlEncode(query);
  HTTPClient http; http.begin(url);
  int code = http.GET();
  if (code <= 0) { http.end(); return ""; }
  DynamicJsonDocument doc(8192);
  if (deserializeJson(doc, http.getString()) || !doc.containsKey("items")) { http.end(); return ""; }
  http.end();
  String result = "Search: \"" + query + "\"\n";
  for (int i = 0; i < 3 && i < (int)doc["items"].size(); i++)
    result += "- " + doc["items"][i]["title"].as<String>() + ": " + doc["items"][i]["snippet"].as<String>() + "\n";
  return result;
}

String httpGetWithRetry(const String& url, int maxRetries, int delayMs) {
  for (int i = 1; i <= maxRetries; i++) {
    HTTPClient http; http.begin(url);
    int code = http.GET();
    if (code > 0) { String r = http.getString(); http.end(); return r; }
    http.end(); delay(delayMs * i);
  }
  return "";
}

void getWeather(String city) {
  city.trim(); if (city.length() == 0) city = "Colombo";
  String url = "https://www.meteosource.com/api/v1/free/point?place_id=" + city
             + "&sections=current&units=metric&key=" + String(Config::WEATHER_KEY);
  HTTPClient http; http.begin(url);
  int code = http.GET();
  if (code > 0) {
    DynamicJsonDocument doc(4096);
    if (!deserializeJson(doc, http.getString()) && doc.containsKey("current")) {
      float temp  = doc["current"]["temperature"];
      float feels = doc["current"]["feels_like"];
      String summary = doc["current"]["summary"] | "";
      Serial.printf("🌤️  %s: %.1f°C (feels %.1f°C) %s\n", city.c_str(), temp, feels, summary.c_str());
    } else {
      Serial.println("⚠️  Weather data unavailable for: " + city);
    }
  } else {
    Serial.println("❌ Weather request failed (HTTP " + String(code) + ")");
  }
  http.end();
}

void searchWeb(const String& query) {
  String q = query; q.trim(); if (q.length() == 0) return;
  String results = fetchWebSearchResults(q);
  if (results.length() > 0) Serial.println("\n🔎 " + results);
  else Serial.println("⚠️  No results found.");
}

String urlEncode(const String& str) {
  String enc = "";
  for (char c : str) {
    if (isalnum(c) || c=='-'||c=='_'||c=='.'||c=='~') enc += c;
    else if (c == ' ') enc += '+';
    else { char buf[4]; sprintf(buf, "%%%.2X", (unsigned char)c); enc += buf; }
  }
  return enc;
}

// ═══════════════════════════════════════════════════════
// SECTION 18 ── CHAT HISTORY  (unchanged)
// ═══════════════════════════════════════════════════════
int estimateTokens(const char* text) { return strlen(text) / 4; }

void summarizeChatHistory() {
  if (chatHistory.size() < 10) return;
  String summary = "[Previous conversation summary]: ";
  for (size_t i = 0; i < chatHistory.size() - 6; i++)
    summary += String(chatHistory[i].role) + ": " + String(chatHistory[i].content) + " | ";
  chatHistory.erase(chatHistory.begin(), chatHistory.end() - 6);
  ChatMessage s;
  strlcpy(s.role, "user", sizeof(s.role));
  summary.toCharArray(s.content, sizeof(s.content));
  chatHistory.insert(chatHistory.begin(), s);
}

void limitChatHistoryByTokens(int maxTokens) {
  int total = 0;
  for (auto& m : chatHistory) total += estimateTokens(m.content);
  while (total > maxTokens && chatHistory.size() > 2) {
    total -= estimateTokens(chatHistory[0].content);
    chatHistory.erase(chatHistory.begin());
  }
}

void addUserMessage(const String& msg) {
  ChatMessage m; strlcpy(m.role, "user", sizeof(m.role));
  msg.toCharArray(m.content, sizeof(m.content));
  chatHistory.push_back(m);
  summarizeChatHistory(); limitChatHistoryByTokens(); saveChatHistory();
}

void addAssistantMessage(const String& msg) {
  // Store as "assistant" (OpenAI-compatible) going forward
  ChatMessage m; strlcpy(m.role, "assistant", sizeof(m.role));
  msg.toCharArray(m.content, sizeof(m.content));
  chatHistory.push_back(m);
  summarizeChatHistory(); limitChatHistoryByTokens(); saveChatHistory();
}

void saveChatHistory() {
  DynamicJsonDocument doc(32768);
  JsonArray arr = doc.createNestedArray("history");
  for (const auto& m : chatHistory) {
    JsonObject o = arr.createNestedObject();
    o["role"] = m.role; o["content"] = m.content;
  }
  File f = FFat.open("/chat.json", FILE_WRITE);
  if (f) { serializeJson(doc, f); f.close(); }
}

void loadChatHistory() {
  if (!FFat.exists("/chat.json")) return;
  File f = FFat.open("/chat.json", FILE_READ);
  if (!f) return;
  DynamicJsonDocument doc(32768);
  if (deserializeJson(doc, f)) { f.close(); return; }
  chatHistory.clear();
  for (JsonObject o : doc["history"].as<JsonArray>()) {
    ChatMessage m;
    strlcpy(m.role,    o["role"],    sizeof(m.role));
    strlcpy(m.content, o["content"], sizeof(m.content));
    chatHistory.push_back(m);
  }
  f.close();
}

// ═══════════════════════════════════════════════════════
// SECTION 19 ── PROACTIVE & BRIEFING  (unchanged)
// ═══════════════════════════════════════════════════════
bool autoMorningBriefing() { return true; }

void generateMorningBriefing() {
  Serial.println("\n☀️  ═══════════ GOOD MORNING! ══════════════");
  Serial.println("📅 " + String(day()) + "/" + String(month()) + "/" + String(year()));
  getWeather("Colombo");

  int todayDay  = weekday();
  int todayDate = day();
  int shown     = 0;
  for (const auto& r : reminders) {
    bool today = (r.recurrence == ONCE || r.recurrence == DAILY) ||
                 (r.recurrence == WEEKLY  && r.dayOfWeek  == todayDay)  ||
                 (r.recurrence == MONTHLY && r.dayOfMonth == todayDate);
    if (today) {
      if (shown == 0) Serial.println("⏰ Today's reminders:");
      Serial.println("   • " + r.message + " at " + formatReminderTime(r.hour, r.minute));
      shown++;
    }
  }
  if (shown == 0) Serial.println("📅 No reminders for today.");

  if (userPattern.recentMood == "negative")
    Serial.println("💙 Yesterday was tough — today is a fresh start!");
  else if (userPattern.recentMood == "positive")
    Serial.println("😊 You've been in great spirits — let's keep it going!");

  Serial.println("════════════════════════════════════════\n");
}

void checkProactiveOpportunity() {
  if ((millis() - userPattern.lastInteraction) / 1000 < 2700) return;
  int h = hour();
  String msg = "";
  if (h >= 7 && h < 9 && userPattern.morningChats > 0)
    msg = "Good morning! Need weather info or help planning your day?";
  else if (h >= 12 && h < 13)
    msg = "Hey! Lunchtime — want to set a reminder for anything this afternoon?";
  else if (h >= 18 && h < 20 && userPattern.eveningChats > 0)
    msg = "Evening! Want a recap or help planning tomorrow?";
  else if (!reminders.empty())
    msg = "Just a check-in — you have " + String(reminders.size()) + " reminder(s) active. All good?";

  if (msg.length() > 0) {
    Serial.println("\n💡 " + msg);
    aiState = AI_PROACTIVE; stateChangeTime = millis();
  }
}

// ═══════════════════════════════════════════════════════
// SECTION 20 ── UTILITY / DIAGNOSTICS  (unchanged)
// ═══════════════════════════════════════════════════════
bool heapOk() { return ESP.getFreeHeap() > Config::HEAP_SAFE_BYTES; }
float getCpuTemp() { return (temperatureRead() - 32) / 1.8f; }

void systemDiagnostics() {
  Serial.println("\n📊 ═══ SYSTEM DIAGNOSTICS ═══");
  Serial.printf("  Version:     %s\n", Config::VERSION);
  Serial.printf("  Model:       %s\n", Config::GROQ_MODEL);
  Serial.printf("  Uptime:      %lu s\n", (millis() - bootTime) / 1000);
  Serial.printf("  CPU Temp:    %.1f °C\n", getCpuTemp());
  Serial.printf("  Free heap:   %d bytes %s\n", ESP.getFreeHeap(), heapOk() ? "✅" : "⚠️ LOW");
  Serial.printf("  WiFi:        %s\n", WiFi.status() == WL_CONNECTED ? "Connected" : "Offline");
  Serial.printf("  Interactions:%d\n", userPattern.totalInteractions);
  Serial.printf("  Mood:        %s\n", userPattern.recentMood.c_str());
  Serial.printf("  Reminders:   %d\n", (int)reminders.size());
  Serial.printf("  Memory facts:%d\n", (int)memory.size());
  Serial.printf("  Chat msgs:   %d\n", (int)chatHistory.size());
  KnowledgeArea* dom = getDominantKnowledge();
  if (dom) Serial.printf("  Top domain:  %s (XP:%d conf:%.0f%%)\n",
    dom->domain.c_str(), dom->experiencePoints, dom->confidenceLevel * 100);
  Serial.println("══════════════════════════════");
}

void clearAll() {
  memory.clear(); chatHistory.clear(); reminders.clear();
  sentimentHistory.clear(); userPattern = UserPattern();
  knowledgeDomains.clear();
  const char* files[] = {"/memory.json","/chat.json","/reminders.json",
                          "/pattern.json","/sentiment.json","/knowledge.json", nullptr};
  for (int i = 0; files[i]; i++) FFat.remove(files[i]);
  aiState = AI_IDLE;
  Serial.println("✅ All data cleared. Restarting...");
  delay(1000); ESP.restart();
}

void printHelp() {
  Serial.println("\n📖 ═══ " + String(Config::VERSION) + " HELP ═══");
  Serial.println("Commands:");
  Serial.println("  /help           — Show this");
  Serial.println("  /version        — Version + stats");
  Serial.println("  /diag           — Full diagnostics");
  Serial.println("  /reminders      — List reminders");
  Serial.println("  /remove N       — Delete reminder N");
  Serial.println("  /weather [city] — Weather info");
  Serial.println("  /search [query] — Web search");
  Serial.println("  /clear          — Wipe all data & restart");
  Serial.println("\nReminder examples:");
  Serial.println("  \"Remind me to call mom at 3 PM\"");
  Serial.println("  \"Daily reminder for water at 8 AM\"");
  Serial.println("  \"Weekly meeting every Monday at 10 AM\"");
  Serial.println("  \"Monthly rent reminder on the 1st at 9 AM\"");
  Serial.println("═══════════════════════════════════════════");
}

void printVersion() {
  Serial.println("\n" + String(Config::VERSION));
  Serial.println("Model:     " + String(Config::GROQ_MODEL));
  Serial.println("Reminders: " + String(reminders.size()) + "/" + String(Config::MAX_REMINDERS));
  Serial.println("Facts:     " + String(memory.size()) + "/" + String(Config::MAX_MEMORY_FACTS));
  Serial.println("Chat msgs: " + String(chatHistory.size()));
  Serial.println("Heap:      " + String(ESP.getFreeHeap()) + " bytes");
  Serial.println("Uptime:    " + String((millis()-bootTime)/1000) + " s");
}

// ═══════════════════════════════════════════════════════
// SECTION 21 ── LED  (unchanged)
// ═══════════════════════════════════════════════════════
void setLEDColor(uint8_t r, uint8_t g, uint8_t b, float brightness) {
  brightness = constrain(brightness, 0.0f, 1.0f);
  strip.setPixelColor(0, strip.Color(uint8_t(r*brightness), uint8_t(g*brightness), uint8_t(b*brightness)));
  strip.show();
}

void updateLED() {
  unsigned long now = millis();
  auto pulse = [&](uint8_t r, uint8_t g, uint8_t b, float speed, float maxB) {
    if (now - lastBlink > (unsigned long)(1000.0f / (speed * 60))) {
      pulseBrightness += pulseIncreasing ? 0.02f : -0.02f;
      if (pulseBrightness >= maxB) pulseIncreasing = false;
      if (pulseBrightness <= Config::LED_MIN_BRIGHTNESS) pulseIncreasing = true;
      setLEDColor(r, g, b, pulseBrightness);
      lastBlink = now;
    }
  };

  switch (aiState) {
    case AI_IDLE:
      setLEDColor(0, 0, 0, 0); break;

    case AI_THINKING:
      pulse(0, 0, 255, 1.0f + thinkingComplexity * 0.1f, Config::LED_MAX_BRIGHTNESS); break;

    case AI_REPLIED:
      setLEDColor(0, 255, 0, Config::LED_MAX_BRIGHTNESS);
      if (now - stateChangeTime > Config::REPLIED_FLASH_MS) aiState = AI_IDLE;
      break;

    case AI_ERROR:
      if (now - lastBlink > 300) {
        setLEDColor(255, 0, 0, (blinkCount % 2) ? 0 : Config::LED_MAX_BRIGHTNESS);
        if (++blinkCount >= 6) { blinkCount = 0; aiState = AI_IDLE; }
        lastBlink = now;
      }
      break;

    case AI_ALERT:
      pulse(255, 50, 0, 1.2f, Config::LED_MAX_BRIGHTNESS);
      if (now - stateChangeTime > Config::REMINDER_ALERT_MS) aiState = AI_IDLE;
      break;

    case AI_EXCITED:
      if (now - lastBlink > 80) {
        setLEDColor(255, 215, 0, Config::LED_MIN_BRIGHTNESS + random(100) / 100.0f *
                    (Config::LED_MAX_BRIGHTNESS - Config::LED_MIN_BRIGHTNESS));
        lastBlink = now;
      }
      if (now - stateChangeTime > 3000) aiState = AI_IDLE;
      break;

    case AI_CONCERNED:
      setLEDColor(100, 150, 255, Config::LED_MAX_BRIGHTNESS * 0.7f);
      if (now - stateChangeTime > 5000) aiState = AI_IDLE;
      break;

    case AI_PROACTIVE:
      pulse(128, 0, 255, 0.8f, Config::LED_MAX_BRIGHTNESS * 0.8f);
      if (now - stateChangeTime > 5000) aiState = AI_IDLE;
      break;

    case AI_LEARNING:
      pulse(0, 255, 255, 1.5f, Config::LED_MAX_BRIGHTNESS * 0.9f);
      if (now - stateChangeTime > 2000) aiState = AI_IDLE;
      break;

    case AI_EVOLVING: {
      KnowledgeArea* dom = getDominantKnowledge();
      if (dom && now - lastBlink > 100) {
        float b = Config::LED_MIN_BRIGHTNESS + (Config::LED_MAX_BRIGHTNESS - Config::LED_MIN_BRIGHTNESS) * 0.6f;
        setLEDColor(dom->colorR, dom->colorG, dom->colorB, b);
        lastBlink = now;
      }
      if (now - stateChangeTime > 1500) aiState = AI_IDLE;
      break;
    }
  }
}

void rainbowWave(int durationMs) {
  unsigned long start = millis();
  while (millis() - start < (unsigned long)durationMs) {
    for (int i = 0; i < 256 && millis() - start < (unsigned long)durationMs; i += 4) {
      uint8_t r = (i<85) ? 255-i*3 : (i<170) ? 0           : (i-170)*3;
      uint8_t g = (i<85) ? i*3     : (i<170) ? 255-(i-85)*3 : 0;
      uint8_t b = (i<85) ? 0       : (i<170) ? (i-85)*3     : 255-(i-170)*3;
      setLEDColor(r, g, b, 0.25f);
      delay(8);
    }
  }
  setLEDColor(0, 0, 0, 0);
}
