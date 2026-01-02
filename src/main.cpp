/*
 Copyright (C) 2020 chester4444@wolke7.net
 GPLv3

Modifications:
Modified by erikxson, 2026:
- FlowIQ 2200 support (volume + month start + flow in l/h)
- MQTT Home Assistant discovery support
- Robust MQTT availability/heartbeat topics
- Removed unused temperature fields
*/

#if defined(ESP8266)
  #include <ESP8266WiFi.h>
  #include <ESP8266mDNS.h>
#elif defined(ESP32)
  #include <WiFi.h>
  #include <ESPmDNS.h>
  #include <esp_timer.h>   // esp_timer_get_time()
#endif

#include <PubSubClient.h>
#include <ArduinoOTA.h>

#include "credentials.h"
#include "WaterMeter.h"
#include "hwconfig.h"

#define ESP_NAME "WaterMeter"

// === Build/metadata ===
#define FW_NAME      "WaterMeter-FlowIQ2200"
#define FW_VERSION   "0.3.0"
#define FW_PUBLISHER "github.com/erikxson"

static const char* TOPIC_FW_INFO = "watermeter/0/fw";
static const char* TOPIC_FW_VER  = "watermeter/0/fw_version";

// === MQTT topics ===
static const char* TOPIC_ONLINE     = "watermeter/0/online";
static const char* TOPIC_ONLINE_TS  = "watermeter/0/online_ts";   // seconds since boot (non-retained)
static const char* TOPIC_IP         = "watermeter/0/ipaddr";
static const char* TOPIC_MYDATA     = "watermeter/0/sensor/mydata";
static const char* TOPIC_MYDATAJS   = "watermeter/0/sensor/mydatajson";

// === MQTT Discovery base ===
static const char* DISCOVERY_PREFIX = "homeassistant";

// === MQTT payloads ===
static const char* PAYLOAD_TRUE  = "true";
static const char* PAYLOAD_FALSE = "false";

// === Globals ===
WaterMeter waterMeter;

WiFiClient espMqttClient;
PubSubClient mqttClient(espMqttClient);

static int  cred = -1;
static char mqttClientId[48];
static char ipBuf[16];
static char tsBuf[24];

static unsigned long lastOnlinePublishMs = 0;
static const unsigned long ONLINE_PUBLISH_INTERVAL_MS = 30000; // 30s heartbeat

// ---------- Helpers ----------------------------------------------------

static void buildMqttClientId()
{
#if defined(ESP32)
  uint64_t mac = ESP.getEfuseMac();
  uint32_t low  = (uint32_t)(mac & 0xFFFFFFFFULL);
  uint16_t high = (uint16_t)((mac >> 32) & 0xFFFFULL);
  snprintf(mqttClientId, sizeof(mqttClientId), "%s_%04X%08X", ESP_NAME, high, low);
#else
  snprintf(mqttClientId, sizeof(mqttClientId), "%s_%06X", ESP_NAME, ESP.getChipId());
#endif
}

static int getWifiToConnect(int numSsid)
{
  for (int i = 0; i < NUM_SSID_CREDENTIALS; i++)
  {
    for (int j = 0; j < numSsid; j++)
    {
      if (strcmp(WiFi.SSID(j).c_str(), credentials[i][0]) == 0)
      {
        return i;
      }
    }
  }
  return -1;
}

static bool ConnectWifi()
{
  int numSsid = WiFi.scanNetworks();
  if (numSsid <= 0) return false;

  cred = getWifiToConnect(numSsid);
  if (cred < 0) return false;

  WiFi.begin(credentials[cred][0], credentials[cred][1]);

  int i = 0;
  while (WiFi.status() != WL_CONNECTED)
  {
    digitalWrite(PIN_LED_BUILTIN, LOW);
    delay(300);
    digitalWrite(PIN_LED_BUILTIN, HIGH);
    delay(300);
    if (i++ > 30) return false;
  }

#if defined(ESP32)
  // Stabilare WiFi på ESP32
  WiFi.setSleep(false);
#endif

  return true;
}

// Implementeras/anropas från WMBusFrame.cpp
void mqttMyData(const char* str)
{
  mqttClient.publish(TOPIC_MYDATA, str, true); // retained
}
void mqttMyDataJson(const char* str)
{
  mqttClient.publish(TOPIC_MYDATAJS, str, true); // retained
}

static bool payloadEqualsIgnoreCase(const byte* payload, unsigned int len, const char* s)
{
  size_t sl = strlen(s);
  if (len != sl) return false;

  for (unsigned int i = 0; i < len; i++)
  {
    char a = (char)payload[i];
    char b = s[i];
    if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
    if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
    if (a != b) return false;
  }
  return true;
}

static void mqttCallback(char* topic, byte* payload, unsigned int len)
{
  if (strcmp(topic, "espmeter/reset") == 0 && payloadEqualsIgnoreCase(payload, len, "true"))
  {
    mqttClient.publish("espmeter/reset/status", PAYLOAD_FALSE, true);
    mqttClient.loop();
    delay(200);
    ESP.restart();
  }
}

// 64-bit uptime i sekunder (startar på 0 vid boot, wrappar i praktiken inte)
static uint64_t uptimeSeconds()
{
#if defined(ESP32)
  return (uint64_t)(esp_timer_get_time() / 1000000ULL);
#else
  return (uint64_t)(millis() / 1000UL);
#endif
}

static void mqttPublishOnlineHeartbeat()
{
  // Online retained (HA availability)
  mqttClient.publish(TOPIC_ONLINE, PAYLOAD_TRUE, true);

  // online_ts non-retained (för att se “nya” meddelanden i realtid)
  uint64_t up = uptimeSeconds();
  snprintf(tsBuf, sizeof(tsBuf), "%llu", (unsigned long long)up);
  mqttClient.publish(TOPIC_ONLINE_TS, tsBuf, false);

  lastOnlinePublishMs = millis();
}

static void mqttPublishIp()
{
  IPAddress ip = WiFi.localIP();
  snprintf(ipBuf, sizeof(ipBuf), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
  mqttClient.publish(TOPIC_IP, ipBuf, true);
}

static void mqttSubscribe()
{
  mqttClient.subscribe("espmeter/reset");
  mqttClient.subscribe("watermeter/0/liveData");
  mqttClient.subscribe("/smarthomeNG/start");
}

static bool mqttConnect()
{
  mqttClient.setServer(credentials[cred][2], 1883);
  mqttClient.setCallback(mqttCallback);

  // Discovery payloads blir större → öka buffer
  mqttClient.setBufferSize(1024);

  mqttClient.setKeepAlive(60);
  mqttClient.setSocketTimeout(10);

  // LWT: om klienten dör -> broker retained "false"
  return mqttClient.connect(
    mqttClientId,
    mqtt_user,
    mqtt_pass,
    TOPIC_ONLINE,
    0,
    true,
    PAYLOAD_FALSE
  );
}

static void setupOTA()
{
  ArduinoOTA.setHostname(mqttClientId);
  ArduinoOTA.begin();
}

static void waterMeterLoop()
{
  (void)waterMeter.isFrameAvailable();
}

// ---------- MQTT Discovery --------------------------------------------

// Publicerar en HA discovery-sensor (retained config)
static void publishDiscoverySensor(
  const char* objectId,
  const char* name,
  const char* stateTopic,
  const char* valueTemplate,
  const char* unit,
  const char* deviceClass,
  const char* stateClass,
  const char* icon
)
{
  char topic[160];
  snprintf(topic, sizeof(topic), "%s/sensor/watermeter0_%s/config", DISCOVERY_PREFIX, objectId);

  // device identifiers ska vara stabilt över tid (inte IP). Vi använder en fast “watermeter0”.
  // Om du vill separera flera mätare i framtiden: byt watermeter0 till watermeter1 osv.
  char payload[900];

  // Bygg JSON med valfria fält (state_class, icon, device_class, unit kan vara null/utelämnas)
  // För robusthet: skriv in dem bara om de finns.
  int n = snprintf(
    payload, sizeof(payload),
    "{"
      "\"name\":\"%s\","
      "\"unique_id\":\"watermeter0_%s\","
      "\"state_topic\":\"%s\","
      "\"value_template\":\"%s\","
      "\"availability_topic\":\"%s\","
      "\"payload_available\":\"%s\","
      "\"payload_not_available\":\"%s\","
      "\"device\":{"
        "\"identifiers\":[\"watermeter0\"],"
        "\"name\":\"Water Meter\","
        "\"manufacturer\":\"Kamstrup\","
        "\"model\":\"FlowIQ 2200 (wM-Bus)\","
        "\"sw_version\":\"%s\""
      "}"
    "}",
    name,
    objectId,
    stateTopic,
    valueTemplate,
    TOPIC_ONLINE,
    PAYLOAD_TRUE,
    PAYLOAD_FALSE,
    FW_VERSION
  );

  if (n <= 0 || (size_t)n >= sizeof(payload))
  {
    // Om payload blev för stor: publicera inget (hellre tyst fail än trasig JSON)
    return;
  }

  // Lägg till optional fields genom enkel “replace-strategi” (append före sista '}')
  // Vi gör detta utan dynamisk allokering.
  auto appendField = [&](const char* key, const char* val, bool quote) {
    if (!val || !val[0]) return;
    size_t L = strlen(payload);
    if (L < 2) return;
    // ta bort sista "}"
    if (payload[L - 1] != '}') return;
    payload[L - 1] = '\0';

    char extra[180];
    if (quote)
      snprintf(extra, sizeof(extra), ",\"%s\":\"%s\"}", key, val);
    else
      snprintf(extra, sizeof(extra), ",\"%s\":%s}", key, val);

    strncat(payload, extra, sizeof(payload) - strlen(payload) - 1);
  };

  appendField("unit_of_measurement", unit, true);
  appendField("device_class", deviceClass, true);
  appendField("state_class", stateClass, true);
  appendField("icon", icon, true);

  mqttClient.publish(topic, payload, true);
}

static void publishDiscoveryAll()
{
  // Usage (m³, total_increasing)
  publishDiscoverySensor(
    "usage",
    "Water Meter Usage",
    TOPIC_MYDATAJS,
    "{{ value_json.CurrentValue | float(0) }}",
    "m³",
    "water",
    "total_increasing",
    ""
  );

  // Month start (m³) – ingen state_class för att undvika “total_increasing”-logik här
  publishDiscoverySensor(
    "month_start",
    "Water Meter Month Start",
    TOPIC_MYDATAJS,
    "{{ value_json.MonthStartValue | float(0) }}",
    "m³",
    "water",
    "",
    ""
  );

  // Flow (l/h, heltal)
  publishDiscoverySensor(
    "flow",
    "Water Meter Flow",
    TOPIC_MYDATAJS,
    "{{ value_json.FlowLph | int(0) }}",
    "l/h",
    "",
    "measurement",
    "mdi:water-pump"
  );
}

// ---------- State machine ---------------------------------------------

enum ControlStateType
{
  StateInit,
  StateWifiConnect,
  StateMqttConnect,
  StateOperating
};

static ControlStateType state = StateInit;

void setup()
{
  pinMode(PIN_LED_BUILTIN, OUTPUT);
  digitalWrite(PIN_LED_BUILTIN, HIGH);

  Serial.begin(115200);
  Serial.printf("\n%s %s (%s)\n", FW_NAME, FW_VERSION, FW_PUBLISHER);

  buildMqttClientId();
  WiFi.mode(WIFI_STA);

  // Starta radion direkt
  waterMeter.begin();

  state = StateWifiConnect;
}

void loop()
{
  switch (state)
  {
    case StateWifiConnect:
      if (ConnectWifi())
      {
        setupOTA();
        state = StateMqttConnect;
      }
      else
      {
        ESP.restart();
      }
      break;

    case StateMqttConnect:
      if (WiFi.status() != WL_CONNECTED)
      {
        state = StateWifiConnect;
        break;
      }

      if (mqttConnect())
      {
        // Basinfo
        mqttPublishIp();
        mqttPublishOnlineHeartbeat();

        mqttClient.publish(TOPIC_FW_INFO, FW_NAME, true);
        mqttClient.publish(TOPIC_FW_VER,  FW_VERSION, true);

        // MQTT Discovery (skapar enheten + sensorer i HA)
        publishDiscoveryAll();

        mqttSubscribe();

        digitalWrite(PIN_LED_BUILTIN, LOW);
        state = StateOperating;
      }
      else
      {
        delay(1000);
      }

      ArduinoOTA.handle();
      break;

    case StateOperating:
      if (WiFi.status() != WL_CONNECTED)
      {
        state = StateWifiConnect;
        break;
      }

      if (!mqttClient.connected())
      {
        // LWT sköter false vid disconnect
        state = StateMqttConnect;
        break;
      }

      if (millis() - lastOnlinePublishMs >= ONLINE_PUBLISH_INTERVAL_MS)
      {
        mqttPublishOnlineHeartbeat();
      }

      waterMeterLoop();
      mqttClient.loop();
      ArduinoOTA.handle();
      break;

    default:
      state = StateWifiConnect;
      break;
  }
}
