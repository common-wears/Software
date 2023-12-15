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
//#define DW_CS 4

#define I2C_SDA 4
#define I2C_SCL 5

// connection pins
const uint8_t PIN_RST = 27; // reset pin
const uint8_t PIN_IRQ = 34; // irq pin
const uint8_t PIN_SS = 21;   // spi select pin

bool isRanging = false; // 表示是否正在进行测距

// WiFi设置
const char* ssid = "Tplink-test";
const char* password = "";

// MQTT服务器设置
const char* mqtt_server = "192.168.1.200";
const int mqtt_broker_port = 1883;

// 定义Tag的唯一ID
const char* tagID = "Lab_Tag_3";  // 请为每个Tag设置不同的ID
#define TAG_ADDR "3A:00:22:EA:82:60:3B:9B"

WiFiClient espClient;
PubSubClient client(espClient);

unsigned long previousMillis = 0;
const long interval = 10;  // 每1000毫秒（即1秒）发送一次
unsigned long sendSequence = 0;  // 发送数据的序列号

unsigned long lastHeartbeatMillis = 0;
const long heartbeatInterval = 10000; // 10秒发送一次心跳


struct AnchorData {
    String id;
    float distance;
    float rssi;
};

AnchorData anchorData[3]; // 存储3个Anchor的数据
int dataCount = 0; // 当前收集到的数据数量

// 添加一个数组来追踪不同Anchor的数据接收次数
int anchorReceptionCounts[3] = {0, 0, 0};

// 当从Anchor接收到数据时调用此函数
void receiveDataFromAnchor(String anchorId, float distance, float rssi) {
    // 查找Anchor在数组中的位置
    int index = -1;
    for (int i = 0; i < dataCount; i++) {
        if (anchorData[i].id == anchorId) {
            index = i;
            break;
        }
    }

    if (index == -1 && dataCount < 3) {
        // 新的Anchor数据
        anchorData[dataCount] = {anchorId, distance, rssi};
        anchorReceptionCounts[dataCount] = 1;
        dataCount++;
    } else if (index != -1) {
        // 更新现有Anchor数据
        anchorData[index].distance = distance;
        anchorData[index].rssi = rssi;

        // 只有在接收到至少两个不同的Anchor数据后，才更新计数
        bool canUpdateCount = false;
        for (int i = 0; i < dataCount; i++) {
            if (i != index && anchorReceptionCounts[i] > 0) {
                canUpdateCount = true;
                break;
            }
        }

        if (canUpdateCount) {
            anchorReceptionCounts[index]++;
        }
    }
}



// 查找Anchor在数组中的索引
int findAnchorIndex(String anchorId) {
    for (int i = 0; i < dataCount; i++) {
        if (anchorData[i].id == anchorId) {
            return i;
        }
    }
    return -1; // 如果未找到，则返回-1
}

// 添加信号量相关变量
bool semaphoreRequested = false;
bool semaphoreAcquired = false;

// 请求信号量
void requestSemaphore() {
    if (!semaphoreRequested) {
        StaticJsonDocument<200> doc;
        doc["tagID"] = tagID;
        doc["action"] = "request";
        char buffer[256];
        serializeJson(doc, buffer);
        client.publish("uwb/semaphore", buffer);
        Serial.println("Semaphore request sent.");
        semaphoreRequested = true;
    }
}


void startRanging() {
    // 实现开始测距的逻辑
    // 例如：发送消息到Anchor或设置测距相关的标志
    isRanging = true; // 假设isRanging变量用于表示是否开始测距
}

void onSemaphoreAcquired() {
    // 当信号量被授予时调用
    semaphoreAcquired = true;
    startRanging();
}


// 释放信号量
void releaseSemaphore() {
    if (semaphoreAcquired) {
        StaticJsonDocument<200> doc;
        doc["tagID"] = tagID;
        doc["action"] = "release";
        char buffer[256];
        serializeJson(doc, buffer);
        client.publish("uwb/semaphore", buffer);
        semaphoreAcquired = false;
        semaphoreRequested = false;
    }
}

// 处理信号量授予
void onSemaphoreGranted() {
    if (!semaphoreAcquired) {
        semaphoreAcquired = true;
        // 开始测距的相关代码
    }
    Serial.println("Semaphore granted.");  // 当信号量授予时打印
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
            client.subscribe("uwb/sync");
        } else {
            delay(5000);
        }
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

void sendHeartbeat() {
    StaticJsonDocument<200> heartbeatDoc;
    heartbeatDoc["tagID"] = tagID;
    char heartbeatBuffer[256];
    serializeJson(heartbeatDoc, heartbeatBuffer);
    client.publish("uwb/tag/heartbeat", heartbeatBuffer);
    Serial.println("Heartbeat sent: ");
    Serial.println(heartbeatBuffer);
}

void setup() {
    Serial.begin(115200);
    // 连接到WiFi
    setup_wifi();
    // 连接到MQTT服务器
    client.setServer(mqtt_server, mqtt_broker_port);
    client.setCallback(callback);

    // 连接到MQTT服务器
    if (client.connect("UWBTagClient")) {
        // 发送标签添加消息
        StaticJsonDocument<200> addTagDoc;
        addTagDoc["tagID"] = tagID;
        char addTagBuffer[256];
        serializeJson(addTagDoc, addTagBuffer);
        client.publish("uwb/tag/add", addTagBuffer);
    }

    // UWB模块的初始化
    SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);
    DW1000Ranging.initCommunication(PIN_RST, PIN_SS, PIN_IRQ);
    DW1000Ranging.attachNewRange(newRange);
    DW1000Ranging.attachNewDevice(newDevice);
    DW1000Ranging.attachInactiveDevice(inactiveDevice);
    DW1000Ranging.startAsTag(TAG_ADDR, DW1000.MODE_LONGDATA_RANGE_LOWPOWER);

    // 初始化IMU
    
    Wire.begin(I2C_SDA, I2C_SCL);
    if(!bno.begin()) {
        Serial.println("Failed to initialize BNO055! Check your wiring or I2C ADDR!");
        while(1);
    } else {
        Serial.println("BNO055 initialized successfully!");
    }
}

void loop() {
    // 维持MQTT连接
    if (!client.connected()) {
        reconnect();
    }
    client.loop();

    // 获取当前时间
    unsigned long currentMillis = millis();

    // 检查是否需要发送心跳
    if (currentMillis - lastHeartbeatMillis >= heartbeatInterval) {
        lastHeartbeatMillis = currentMillis;
        sendHeartbeat();
    }

    // 处理信号量请求
    if (!semaphoreRequested && !semaphoreAcquired) {
        requestSemaphore();
    }

    // 检查是否需要开始测距
    if (semaphoreAcquired && !isRanging) {
        startRanging();
        isRanging = true;
    }

    // 调用DW1000库的循环函数以处理任何正在进行的测距
    DW1000Ranging.loop();

    // 检查是否到了发送数据的时间
    if (currentMillis - previousMillis >= interval) {
        previousMillis = currentMillis;

        if (semaphoreAcquired) {
            // 执行测距和数据发送
            checkAndSendData();
            releaseSemaphore(); // 完成后释放信号量
        }
    }
}

void sendIMUData() {
    float accData[3], magData[3], gyrData[3], tempData;
    readACC(accData); 
    readMAG(magData); 
    readGYR(gyrData);
    tempData = bno.getTemp();
    
    // 发送加速度数据
    sendJSON_for_IMU(tagID, "ACC", accData, 3, "imu/acc");
    // 发送磁场数据
    sendJSON_for_IMU(tagID, "MAG", magData, 3, "imu/mag");
    // 发送陀螺仪数据
    sendJSON_for_IMU(tagID, "GYR", gyrData, 3, "imu/gyr");
    // 发送温度数据
    StaticJsonDocument<100> tempDoc;
    tempDoc["id"] = tagID;
    tempDoc["TMP"] = tempData;
    char tempBuffer[128];
    serializeJson(tempDoc, tempBuffer);
    client.publish("imu/temp", tempBuffer);
}
void newRange() {
    String anchorId = String(DW1000Ranging.getDistantDevice()->getShortAddress(), HEX);
    float distance = DW1000Ranging.getDistantDevice()->getRange();
    float rssi = DW1000Ranging.getDistantDevice()->getRXPower();

    receiveDataFromAnchor(anchorId, distance, rssi);
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
    if (dataCount >= 3) {
        // 发送UWB数据
        StaticJsonDocument<300> uwbDoc;
        uwbDoc["id"] = tagID;
        uwbDoc["seq"] = sendSequence;
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

        // 读取并发送IMU数据
        StaticJsonDocument<300> imuDoc;
        float accData[3], magData[3], gyrData[3];
        readACC(accData); 
        readMAG(magData); 
        readGYR(gyrData);

        imuDoc["id"] = tagID;
        imuDoc["seq"] = sendSequence;

        JsonArray accArray = imuDoc.createNestedArray("ACC");
        for (int i = 0; i < 3; i++) {
            accArray.add(accData[i]);
        }

        JsonArray magArray = imuDoc.createNestedArray("MAG");
        for (int i = 0; i < 3; i++) {
            magArray.add(magData[i]);
        }

        JsonArray gyrArray = imuDoc.createNestedArray("GYR");
        for (int i = 0; i < 3; i++) {
            gyrArray.add(gyrData[i]);
        }

        char imuBuffer[512];
        serializeJson(imuDoc, imuBuffer);
        client.publish("imu/data", imuBuffer);

        // 读取并发送温度数据
        StaticJsonDocument<100> tmpDoc;
        float tempData[1];
        readTMP(tempData);

        tmpDoc["id"] = tagID;
        tmpDoc["seq"] = sendSequence;
        tmpDoc["TMP"] = tempData[0];

        char tmpBuffer[128];
        serializeJson(tmpDoc, tmpBuffer);
        client.publish("tmp/data", tmpBuffer);

        dataCount = 0; // 重置数据计数
        sendSequence++; // 增加序列号
    }
}


void callback(char* topic, byte* payload, unsigned int length) {
    // 处理来自MQTT的消息
    String msgTopic = String(topic);
    if (msgTopic == "uwb/sync") {
        checkAndSendData();
    } else if (msgTopic == "uwb/semaphore/grant" && !semaphoreAcquired) {
        onSemaphoreAcquired();
    }
    if (msgTopic == "uwb/poll") {
        handlePollRequest(String((char*)payload));
    }
}

void handlePollRequest(String payload) {
    StaticJsonDocument<200> doc;
    deserializeJson(doc, payload);
    String polledTagID = doc["tagID"];

    if (polledTagID == tagID) {
        sendData();
    }
}

void sendData() {
    // 如果没有获取信号量，直接返回
    if (!semaphoreAcquired) return;

    float distance = DW1000Ranging.getDistantDevice()->getRange();
    float rssi = DW1000Ranging.getDistantDevice()->getRXPower(); // 获取信号强度

    // 从IMU读取数据
    float accData[3], magData[3], gyrData[3], tempData;
    readACC(accData); 
    readMAG(magData); 
    readGYR(gyrData);
    tempData = bno.getTemp();

    // 创建JSON文档
    StaticJsonDocument<300> doc;
    doc["tagID"] = tagID;
    doc["distance"] = distance;
    doc["rssi"] = rssi;

    // 添加IMU数据
    JsonArray accArray = doc.createNestedArray("ACC");
    for (int i = 0; i < 3; i++) accArray.add(accData[i]);
    JsonArray magArray = doc.createNestedArray("MAG");
    for (int i = 0; i < 3; i++) magArray.add(magData[i]);
    JsonArray gyrArray = doc.createNestedArray("GYR");
    for (int i = 0; i < 3; i++) gyrArray.add(gyrData[i]);

    doc["TMP"] = tempData;

    // 序列化并发送
    char buffer[512];
    serializeJson(doc, buffer);
    client.publish("uwb/data", buffer);

    Serial.println("Data sent: ");
    Serial.println(buffer);
}
