#include <WiFi.h>
#include <SPI.h>
#include "DW1000Ranging.h"
#include <PubSubClient.h>
#include <ArduinoJson.h>

// Delay risultante da calibrazione antenna
#define adelay_def 16681

// ID ancora
#define ANCHOR_ADD "87:00:22:EA:82:60:3B:9C"  
const char* anchor_ID = "Lab_anchor_87";


uint16_t adelay = adelay_def;

// SSID e PWD della rete a cui sono connessi client e server
const char *ssid = "Tplink-test";
const char *password = "";

// Indirizzo IP del broker MQTT
const char *mqtt_broker_ip = "192.168.1.200"; // Virtual IP
// Porta del broker MQTT
const int mqtt_broker_port = 1883;

long int refreshDelay = 5000; // [ms] Frequenza invio messaggi di DEBUG
//#############################################

// 添加用于信号量管理的变量
String currentSemaphoreHolder = ""; // 当前持有信号量的TAG ID

unsigned long int runtime = 0;
unsigned long int MSG_COUNT = 1;

unsigned long lastPollTime = 0;
const int pollInterval = 1000; // 轮询间隔，例如1000毫秒

int currentTagIndex = 0; // 当前轮询到的Tag索引
// 假设最大Tag数量为10
String tagList[10];
int tagListSize = 0; // 当前Tag列表中的实际元素数量

WiFiClient espClient;
PubSubClient client(espClient);

// Topic MQTT per il DEBUG
const char *topic_anchor_debug_add = "UWB/ANCHOR/DEBUG/ADD";
const char *topic_anchor_debug_del = "UWB/ANCHOR/DEBUG/DEL";
const char *topic_anchor_debug_range = "UWB/ANCHOR/DEBUG/RANGE";
const char *topic_anchor_debug_alive = "UWB/ANCHOR/DEBUG/ALIVE";

// Connessioni elettrche tra modulo DW1000 e MCU
#define SPI_SCK 18
#define SPI_MISO 19
#define SPI_MOSI 23
#define SPI_CS 4
#define PIN_RST 27 
#define PIN_IRQ 34
#define PIN_WAKEUP 32
#define PIN_EXTON 33


// Inizializza la comunicazione Wi-Fi
void initWiFi()
{
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  Serial.print("Connecting to ");
  Serial.print(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED)
  {
    Serial.print('.');
    delay(500);
  }
  // Stampa l'indirizzo IP assegnato dal DHCP al dispositivo
  Serial.println("");
  Serial.println("WiFi connected.");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.println("\n");
}

// Inizializza la comunicazione con il broker MQTT
void initMQTTserver() {
  client.setServer(mqtt_broker_ip, mqtt_broker_port); 
  // Per aumentare la velocità di risposta
  client.setSocketTimeout(1);
  client.setKeepAlive(1);
}

// Mantiene attiva la connessione al server 
void loopMQTTserver() {
  // Se il client non è connesso al server...
  if (!client.connected())
  {
    //... prova a connettersi ogni 0.5 secondi
    reconnect();
  }

  // Una volta connesso resta in ascolto di nuovi messaggi da parte del server
  client.loop(); 
}

// Per connettersi al broker MQTT
void reconnect()
{
  // Loop fino a quando non si connette al server
  while (!client.connected())
  {
    Serial.print("\nAttempting MQTT connection..");
    // Tenta la connessione al server: connect(<nome_client>, [username, password])
    // Se la connessione con il server riesce
    if(client.connect(anchor_ID)) {
      Serial.print("\nConnected with broker -> ");
      Serial.print("IP: ");
      Serial.print(mqtt_broker_ip);
      Serial.print(":");
      Serial.println(mqtt_broker_port);
      Serial.println("\n");
    }
    else
    {
      Serial.print("Connection failed with state: ");
      Serial.println(client.state());
      Serial.println("try again in 0.5 seconds\n\n");
      // Aspetta 0.5 secondi prima di riporovare a connettersi
      delay(500);
    }
  }
}

// Crea stringa in formato JSON da inviare al broker
void sendJSON(String anchor_ID, String msg, String topic) {
  StaticJsonDocument<300> doc;  
  
  doc["ANCHOR ID"] = anchor_ID;
  doc["MSG_#"] = MSG_COUNT;
  doc["DEBUG MSG"] = msg;

  serializeJson(doc, Serial);
  Serial.println("");

  int str_len = topic.length() + 1; 
  char char_array[str_len];
  topic.toCharArray(char_array, str_len);

  char buffer[256];
  serializeJson(doc, buffer);
  client.publish(char_array, buffer);
  Serial.println("Message published.\n");
  MSG_COUNT++;
}

// Se l'anchor riceve un nuovo messaggio di BLINK da parte di un TAG
// 当 Anchor 接收到新的 Tag 信号时
void newBlink(DW1000Device *device) {
    String tagAddress = String(device->getShortAddress(), HEX);
    Serial.print("New TAG Detected: ");
    Serial.println(tagAddress);

    // 发送到服务器的逻辑
    StaticJsonDocument<200> jsonDoc;
    jsonDoc["event"] = "newTag";
    jsonDoc["tagID"] = tagAddress;
    char buffer[256];
    serializeJson(jsonDoc, buffer);
    client.publish("uwb/tagDetected", buffer);
}

// Rimuove TAG non più attivi (il codice che verifica ciò è implementato nella libreria DW1000)
void inactiveDevice(DW1000Device *device)
{
    Serial.print("Device removed -> (short) ");
    Serial.println(device->getShortAddress(), HEX);
    String myString = "Device removed: ";
    myString += String(device->getShortAddress());
    sendJSON(anchor_ID, myString, topic_anchor_debug_del);

}

// 当从 Tag 收到距离信息时
void newRange() {
    String tagAddress = String(DW1000Ranging.getDistantDevice()->getShortAddress(), HEX);
    float distance = DW1000Ranging.getDistantDevice()->getRange();
    float rssi = DW1000Ranging.getDistantDevice()->getRXPower();

    // 打印信息
    Serial.print("TAG: "); Serial.print(tagAddress);
    Serial.print(", Distance: "); Serial.print(distance); Serial.println(" meters");
    Serial.print(", RSSI: "); Serial.print(rssi); Serial.println(" dBm");

    // 发送到服务器的逻辑
    StaticJsonDocument<300> jsonDoc;
    jsonDoc["tagID"] = tagAddress;
    jsonDoc["distance"] = distance;
    jsonDoc["rssi"] = rssi;
    char buffer[256];
    serializeJson(jsonDoc, buffer);
    client.publish("uwb/tagRange", buffer);
}

//Inizializza il modulo DW1000
void initDW1000() {
  Serial.println("Anchor config and start");
  Serial.print("Antenna delay ");
  Serial.println(adelay);

  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);
  DW1000Ranging.initCommunication(PIN_RST, SPI_CS, PIN_IRQ);

  // Imposta il delay dall'antenna al chip DW1000
  DW1000.setAntennaDelay(adelay);

  DW1000Ranging.attachNewRange(newRange);
  DW1000Ranging.attachBlinkDevice(newBlink);
  DW1000Ranging.attachInactiveDevice(inactiveDevice);

  //Filtro per valori di distanza più regolari
  //DW1000Ranging.useRangeFilter(true);
  
  DW1000Ranging.startAsAnchor(ANCHOR_ADD, DW1000.MODE_LONGDATA_RANGE_LOWPOWER, false);
}

void setup() {
    Serial.begin(115200);

    initWiFi();                     
    initMQTTserver();

    // 设置MQTT消息回调
    client.setCallback(callback);

    initDW1000(); 
    client.subscribe("uwb/poll"); // 订阅轮询主题
}

void loop() {
    loopMQTTserver();
    DW1000Ranging.loop();

    // 定期轮询标签
    if (millis() - lastPollTime > pollInterval) {
        pollNextTag();
        lastPollTime = millis();
    }
    client.subscribe("uwb/poll"); // 订阅轮询主题
    // 可以添加更多订阅，如果需要的话
}


// MQTT消息回调
void callback(char* topic, byte* payload, unsigned int length) {
    String msgTopic = String(topic);

    if (msgTopic == "uwb/poll") {
        handlePollRequest(String((char*)payload));
    } else if (msgTopic == "uwb/semaphore/request") {
        handleSemaphoreRequest(String((char*)payload));
    } else if (msgTopic == "uwb/semaphore/release") {
        handleSemaphoreRelease(String((char*)payload));
    } else if (msgTopic == "uwb/tagListUpdate") {
        updateTagList(String((char*)payload));
    }
}

void handlePollRequest(String payload) {
    StaticJsonDocument<200> doc;
    deserializeJson(doc, payload);
    String polledTagID = doc["tagID"];

    // 检查是否是当前轮询的Tag
    if (polledTagID == tagList[currentTagIndex]) {
        performRanging();
    }
}


void performRanging() {
    // 确保DW1000模块已经初始化并且设置为Anchor模式
    DW1000Ranging.startAsAnchor(ANCHOR_ADD, DW1000.MODE_LONGDATA_RANGE_LOWPOWER, false);
    // 此函数可以包含开始测距的逻辑，比如发送请求信号等
}

void updateTagList(String payload) {
    StaticJsonDocument<200> doc;
    deserializeJson(doc, payload);
    JsonArray tags = doc["tags"];

    tagListSize = 0;
    for (JsonVariant v : tags) {
        if (tagListSize < 10) {
            tagList[tagListSize++] = v.as<String>();
        }
    }
    currentTagIndex = 0;
}


void pollNextTag() {
    if (tagListSize == 0) return; // 如果列表为空，不执行轮询

    String tagID = tagList[currentTagIndex];
    StaticJsonDocument<200> doc;
    doc["action"] = "poll";
    doc["tagID"] = tagID;
    char buffer[256];
    serializeJson(doc, buffer);
    client.publish("uwb/poll", buffer);

    currentTagIndex = (currentTagIndex + 1) % tagListSize; // 通过tagListSize保持索引有效
}

void handleSemaphoreRequest(String payload) {
    Serial.print("Received semaphore request: ");
    Serial.println(payload);  // 收到信号量请求时打印
    StaticJsonDocument<200> doc;
    deserializeJson(doc, payload);
    String tagID = doc["tagID"];

    if (currentSemaphoreHolder == "") {
        currentSemaphoreHolder = tagID;

        StaticJsonDocument<200> response;
        response["tagID"] = tagID;
        response["grant"] = "semaphore";
        char buffer[256];
        serializeJson(response, buffer);
        client.publish("uwb/semaphore/grant", buffer);
    }
  Serial.println("Processing semaphore request.");  // 处理信号量请求后打印
}


void handleSemaphoreRelease(String payload) {
    Serial.print("Received semaphore release: ");
    Serial.println(payload);  // 收到信号量释放时打印
    StaticJsonDocument<200> doc;
    deserializeJson(doc, payload);
    String tagID = doc["tagID"];

    if (currentSemaphoreHolder == tagID) {
        currentSemaphoreHolder = "";
    }
    Serial.println("Processing semaphore release.");  // 处理信号量释放后打印
}


// 处理信号量消息
void handleSemaphoreMessage(String payload) {
    StaticJsonDocument<200> doc;
    deserializeJson(doc, payload);
    String tagID = doc["tagID"];
    String action = doc["action"];

    if (action == "request" && currentSemaphoreHolder.isEmpty()) {
        currentSemaphoreHolder = tagID;

        StaticJsonDocument<200> responseDoc;
        responseDoc["tagID"] = tagID;
        responseDoc["action"] = "grant";
        char responseBuffer[256];
        serializeJson(responseDoc, responseBuffer);
        client.publish("uwb/semaphore", responseBuffer);
    } else if (action == "release" && currentSemaphoreHolder == tagID) {
        currentSemaphoreHolder = "";
    }
}
