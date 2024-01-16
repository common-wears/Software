#include <SPI.h>
#include <DW1000Ranging.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Adafruit_BNO055.h>
#include <ArduinoJson.h>
#include <Wire.h>

Adafruit_BNO055 bno = Adafruit_BNO055(55);

#define SPI_SCK 18
#define SPI_MISO 19
#define SPI_MOSI 23
#define PIN_RST 27
#define PIN_IRQ 34
#define PIN_SS 21
#define I2C_SDA 4
#define I2C_SCL 5

const char* ssid = "Tplink-test";
const char* password = "";
const char* mqtt_server = "192.168.1.200";
const int mqtt_broker_port = 1883;

const char* tagID = "Lab_Tag_4";
#define TAG_ADDR "4A:00:22:EA:82:60:3B:9B"
String myTimeSlot = "Tag_slot_4";  // 分配给该Tag的时间槽
//com9 = 3
//com13 = 4
//com12 = 2
WiFiClient espClient;
PubSubClient client(espClient);

bool isTimeSlotActive = false;
unsigned long previousMillis = 0;
const long interval = 1000;  // 发送数据的间隔时间（毫秒）
unsigned long sendSequence = 0;  // 发送数据的序列号
unsigned long lastHeartbeatMillis = 0;
const long heartbeatInterval = 10000; // 10秒发送一次心跳
unsigned long timeSlotActiveUntil = 0; // 用于记录时间槽活跃直至的时间

struct AnchorData {
    String id;
    float distance;
    float rssi;
};
AnchorData anchorData[3];
int dataCount = 0;

void setup() {
    Serial.begin(115200);
    setup_wifi();
    client.setServer(mqtt_server, mqtt_broker_port);
    client.setCallback(callback);

    if (client.connect("UWBTagClient")) {
        StaticJsonDocument<200> addTagDoc;
        addTagDoc["tagID"] = tagID;
        char addTagBuffer[256];
        serializeJson(addTagDoc, addTagBuffer);
        client.publish("uwb/tag/add", addTagBuffer);
        // 订阅时间槽分配消息
        client.subscribe("tdma/slot/allocated");
    }

    sendTimeSlotRequest();
    initUWBModule();
    initIMUSensor();
    //client.subscribe("tdma/slot");
    //client.subscribe("tdma/slot/allocated");
}


void loop() {
    maintainMQTTConnection();
    static unsigned long lastRequestMillis = 0;

    // 每30秒发送时间槽请求
    if (millis() - lastRequestMillis > 10000) {
        sendTimeSlotRequest();
        lastRequestMillis = millis();
    }

    // 检查时间槽是否活跃
    if (millis() < timeSlotActiveUntil) {
        isTimeSlotActive = true;
    } else {
        isTimeSlotActive = false;
    }

    // 处理心跳
    handleHeartbeat();

    // 检查并发送数据（如果时间槽活跃）
    /*if (isTimeSlotActive && millis() - previousMillis >= interval) {
        // 处理 UWB 模块的通信
        DW1000Ranging.loop();
        previousMillis = millis();
        checkAndSendData();
    }
    */
    if (isTimeSlotActive) {
        // 在时间槽活跃期间更频繁地调用 DW1000Ranging.loop()
        for (int i = 0; i < 1000; i++) {
            DW1000Ranging.loop();
        }

        if (millis() - previousMillis >= interval) {
            previousMillis = millis();
            checkAndSendData();
        }
    } else {
        // 当不在时间槽时依然调用，但可能频率较低
        //DW1000Ranging.loop();
    }

}

void handleHeartbeat() {
    unsigned long currentMillis = millis();
    if (currentMillis - lastHeartbeatMillis >= heartbeatInterval) {
        lastHeartbeatMillis = currentMillis;
        sendHeartbeat(); // 发送心跳
    }
}

void sendTimeSlotRequest() {
    StaticJsonDocument<200> doc;
    doc["tagID"] = tagID;
    doc["requestedSlot"] = myTimeSlot;  // 或者根据您的逻辑动态指定

    char buffer[256];
    serializeJson(doc, buffer);
    client.publish("tdma/slot/request", buffer);
    Serial.println("Time slot request sent");
}

void maintainMQTTConnection() {
    if (!client.connected()) {
        reconnect();
    }
    client.loop();
}

void sendDataIfTimeSlot() {
    unsigned long currentMillis = millis();
    if (currentMillis - previousMillis >= interval) {
        previousMillis = currentMillis;
        Serial.println("Sending data in allocated time slot...");
        checkAndSendData();
        isTimeSlotActive = false;
    } else {
        Serial.println("Waiting for the next time slot...");
    }
}

void callback(char* topic, byte* payload, unsigned int length) {
    String msgTopic = String(topic);
    if (msgTopic == "tdma/slot/allocated") {
        Serial.println("Time slot allocation message received");
        handleTimeSlot(String((char*)payload));
    }
    //String msgTopic = String(topic);
    if (msgTopic == "tdma/slot") {
        handleTimeSlot(String((char*)payload));
    }
}

void handleTimeSlot(String payload) {
    Serial.println("Received time slot message: " + payload);
    StaticJsonDocument<200> doc;
    deserializeJson(doc, payload);
    String allocatedSlot = doc["allocatedSlot"];
    String receivedTagID = doc["tagID"];
    int duration = doc["duration"]; // 解析 duration 字段

    if (receivedTagID == tagID && allocatedSlot == myTimeSlot) {
        isTimeSlotActive = true;
        timeSlotActiveUntil = millis() + duration;  // 根据分配的时间槽持续时间设置
    } else {
        isTimeSlotActive = false;}

    Serial.println("Allocated Slot: " + allocatedSlot);
    if (receivedTagID == tagID && allocatedSlot == myTimeSlot) {
        isTimeSlotActive = true;
        // 可以在这里设置时间槽的持续时间
        timeSlotActiveUntil = millis() + 1000; // 假设时间槽持续1秒
        Serial.println("Time slot activated.");
    } else {
        isTimeSlotActive = false;
        Serial.println("Time slot not for this tag.");
    }
}


void setup_wifi() {
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.println("Connecting to WiFi..");
    }
    Serial.println("Connected to WiFi");
}

void reconnect() {
    while (!client.connected()) {
        if (client.connect("UWBTagClient")) {
            client.subscribe("tdma/slot");
        } else {
            delay(5000);
        }
    }
}

void initUWBModule() {
    SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);
    DW1000Ranging.initCommunication(PIN_RST, PIN_SS, PIN_IRQ);
    DW1000Ranging.attachNewRange(newRange);
    DW1000Ranging.attachNewDevice(newDevice);
    DW1000Ranging.attachInactiveDevice(inactiveDevice);
    DW1000Ranging.startAsTag(TAG_ADDR, DW1000.MODE_LONGDATA_RANGE_LOWPOWER);
}

/*
void newRange() {
    String anchorId = String(DW1000Ranging.getDistantDevice()->getShortAddress(), HEX);
    float distance = DW1000Ranging.getDistantDevice()->getRange();
    float rssi = DW1000Ranging.getDistantDevice()->getRXPower();

    if (dataCount < 3) {
        anchorData[dataCount++] = {anchorId, distance, rssi};
    }
    Serial.print("Range data from Anchor: ");
    Serial.print(anchorId);
    Serial.print(", Distance: ");
    Serial.print(distance);
    Serial.print(", RSSI: ");
    Serial.println(rssi);
}
*/

void newRange() {
    String anchorId = String(DW1000Ranging.getDistantDevice()->getShortAddress(), HEX);
    float distance = DW1000Ranging.getDistantDevice()->getRange();
    float rssi = DW1000Ranging.getDistantDevice()->getRXPower();

    Serial.println("[DEBUG] newRange function called");

    if (distance > 0 && distance < 30) {
        bool found = false;
        for (int i = 0; i < dataCount; i++) {
            if (anchorData[i].id == anchorId) {
                // 更新现有 Anchor 数据
                anchorData[i].distance = distance;
                anchorData[i].rssi = rssi;
                found = true;
                break;
            }
        }
        if (!found && dataCount < 3) {
            // 添加新的 Anchor 数据
            anchorData[dataCount++] = {anchorId, distance, rssi};
            Serial.println("[DEBUG] Data added to anchorData array.");
        }
    } else {
        Serial.println("[ERROR] Invalid range data");
    }
}



void newDevice(DW1000Device *device) {
    Serial.print("New device: ");
    Serial.println(device->getShortAddress(), HEX);
}

void inactiveDevice(DW1000Device *device) {
    Serial.print("Inactive device: ");
    Serial.println(device->getShortAddress(), HEX);
}

void checkAndSendData() {
    Serial.println("Checking data to send...");
    if (dataCount > 0) {
        Serial.println("Sending UWB data...");
        // 发送UWB数据
        StaticJsonDocument<300> uwbDoc;
        uwbDoc["id"] = tagID;
        uwbDoc["seq"] = sendSequence++;
        JsonArray anchors = uwbDoc.createNestedArray("anchors");

        for (int i = 0; i < dataCount; i++) {
            JsonObject anchor = anchors.createNestedObject();
            anchor["id"] = anchorData[i].id;
            anchor["distance"] = anchorData[i].distance;
            anchor["rssi"] = anchorData[i].rssi;
        }

        char uwbBuffer[512];
        serializeJson(uwbDoc, uwbBuffer);
        client.publish("uwb/data", uwbBuffer);
        Serial.println("UWB data sent");
        dataCount = 0;
    } else {
        Serial.println("Not enough data to send.");
    }
    sendIMUData();
}

void sendIMUData() {
    float accData[3], magData[3], gyrData[3], tempData;
    readACC(accData); 
    readMAG(magData); 
    readGYR(gyrData);
    readTMP(&tempData);
    
    sendJSON_for_IMU(tagID, "ACC", accData, 3, "imu/acc");  // 修改参数顺序
    sendJSON_for_IMU(tagID, "MAG", magData, 3, "imu/mag");  // 修改参数顺序
    sendJSON_for_IMU(tagID, "GYR", gyrData, 3, "imu/gyr");  // 修改参数顺序
    sendTMPData(tempData, "imu/temp");
}

void sendJSON_for_IMU(const char* tag_ID, const char* sensor_type, float* data_array, int data_size, const char* topic) {
    StaticJsonDocument<300> doc;
    doc["TAG ID"] = tag_ID;
    doc["Sensor"] = sensor_type;
    
    JsonArray data = doc.createNestedArray("data");
    for (int i = 0; i < data_size; i++) {
        data.add(data_array[i]);
    }

    char buffer[256];
    serializeJson(doc, buffer);
    client.publish(topic, buffer);

    #ifdef SERIAL_DEBUG
    Serial.print("Published to ");
    Serial.print(topic);
    Serial.print(": ");
    Serial.println(buffer);
    #endif
}

void sendTMPData(float temp, const char* topic) {
    StaticJsonDocument<100> tempDoc;  // Create a JSON document
    tempDoc["tagID"] = tagID;         // Add the tag ID to the document
    tempDoc["TMP"] = temp;            // Add the temperature data to the document

    char buffer[128];                 // Create a buffer to hold the serialized JSON
    serializeJson(tempDoc, buffer);   // Serialize the JSON document to the buffer
    client.publish(topic, buffer);    // Publish the buffer to the specified MQTT topic

    #ifdef SERIAL_DEBUG
    Serial.print("Published to ");
    Serial.print(topic);
    Serial.print(": ");
    Serial.println(buffer);
    #endif
}

void initIMUSensor() {
    Wire.begin(I2C_SDA, I2C_SCL);
    if (!bno.begin()) {
        Serial.println("Failed to initialize BNO055! Check your wiring or I2C ADDR!");
        while (1);
    } else {
        Serial.println("BNO055 initialized successfully!");
    }
}

void readACC(float* ACC) {
    imu::Vector<3> acc = bno.getVector(Adafruit_BNO055::VECTOR_ACCELEROMETER);
    ACC[0] = acc.x();
    ACC[1] = acc.y();
    ACC[2] = acc.z();
}

void readMAG(float* MAG) {
    imu::Vector<3> mag = bno.getVector(Adafruit_BNO055::VECTOR_MAGNETOMETER);
    MAG[0] = mag.x();
    MAG[1] = mag.y();
    MAG[2] = mag.z();
}

void readGYR(float* GYR) {
    imu::Vector<3> gyr = bno.getVector(Adafruit_BNO055::VECTOR_GYROSCOPE);
    GYR[0] = gyr.x();
    GYR[1] = gyr.y();
    GYR[2] = gyr.z();
}

void readTMP(float* TMP) {
    TMP[0] = bno.getTemp();
}

/*
void receiveDataFromAnchor(String anchorId, float distance, float rssi) {
    // Find if the anchor ID already exists in the array
    int index = -1;
    for (int i = 0; i < dataCount; i++) {
        if (anchorData[i].id == anchorId) {
            index = i;
            break;
        }
    }

    // If new anchor, add to the array
    if (index == -1 && dataCount < 3) {
        anchorData[dataCount] = {anchorId, distance, rssi};
        dataCount++;
    } 
    // If existing anchor, update its data
    else if (index != -1) {
        anchorData[index].distance = distance;
        anchorData[index].rssi = rssi;
    }
}
*/

void sendHeartbeat() {
    StaticJsonDocument<200> heartbeatDoc;
    heartbeatDoc["tagID"] = tagID;
    char heartbeatBuffer[256];
    serializeJson(heartbeatDoc, heartbeatBuffer);
    client.publish("uwb/tag/heartbeat", heartbeatBuffer);
    Serial.println("Heartbeat sent: ");
    Serial.println(heartbeatBuffer);
}
