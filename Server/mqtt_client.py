import paho.mqtt.client as mqtt
from data_processor import process_data, process_imu_data, process_temp_data
import json
from concurrent.futures import ThreadPoolExecutor
import logging
import time
import threading
from queue import Queue

semaphore_queue = Queue()

MQTT_TOPICS = [("uwb/data", 0), ("imu/data", 0), ("tmp/data", 0), ("uwb/semaphore", 0), ("tdma/slot", 0)]

executor = ThreadPoolExecutor(max_workers=60)
semaphore_holder = None
active_tags = set()

# 全局变量
last_heartbeats = {}

# 设置日志记录
logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)
time_slots = {}

def allocate_time_slot(tag_id, requested_slot):
    # 检查请求的时间槽是否已被分配给该标签
    if time_slots.get(requested_slot) == tag_id:
        return requested_slot  # 如果是，直接返回该时间槽

    # 分配新的时间槽
    if requested_slot not in time_slots:
        time_slots[requested_slot] = tag_id
        return requested_slot
    else:
        for slot in range(1, 5):
            if slot not in time_slots:
                time_slots[slot] = tag_id
                return slot
    return None  # 所有时间槽都已占用

def handle_tdma_slot_request(client, payload):
    try:
        data = json.loads(payload)
        tag_id = data.get("tagID")
        requested_slot = data.get("requestedSlot")

        allocated_slot = allocate_time_slot(tag_id, requested_slot)
        if allocated_slot is not None:
            response = {"tagID": tag_id, "allocatedSlot": allocated_slot, "duration": 100}  # 添加 duration 字段
            client.publish("tdma/slot/allocated", json.dumps(response))
            logger.info(f"Allocated slot {allocated_slot} to tag {tag_id}")
        else:
            logger.warning(f"No available slots for tag {tag_id}")
    except json.JSONDecodeError:
        logger.error("Error decoding JSON in TDMA slot request")

def on_connect(client, userdata, flags, rc):
    logger.info("Connected with result code " + str(rc))
    client.subscribe(MQTT_TOPICS)
    client.subscribe("tdma/slot/request")  # 确保订阅正确的主题
    client.subscribe("uwb/tag/add")
    client.subscribe("uwb/tag/remove")
    client.subscribe("uwb/tag/heartbeat")
    send_active_tags(client)
    check_tag_timeout()  # 启动心跳检查线程


# 在处理心跳消息的函数中更新字典
def handle_heartbeat_message(payload):
    global last_heartbeats, active_tags
    data = json.loads(payload)
    tag_id = data.get("tagID")
    if tag_id:
        last_heartbeats[tag_id] = time.time()
        if tag_id not in active_tags:
            active_tags.add(tag_id)
            #logger.info(f"Heartbeat received. Tag added: {tag_id}")
        #else:
            #logger.info(f"Heartbeat received from existing tag: {tag_id}")
    else:
        logger.error("Heartbeat message without tagID")

# 新增检查超时标签的函数
def check_tag_timeout():
    global active_tags, last_heartbeats
    current_time = time.time()
    timeout = 60  # 60秒超时
    for tag_id, last_time in list(last_heartbeats.items()):
        if current_time - last_time > timeout:
            active_tags.discard(tag_id)  # 安全移除，即使标签不存在也不会出错
            del last_heartbeats[tag_id]
            logger.info(f"Tag timeout. Removed: {tag_id}")
    threading.Timer(30, check_tag_timeout).start()  # 每30秒检查一次

def process_message(client, userdata, msg):
    try:
        payload = msg.payload.decode()
        logger.info("Received message on topic {}: {}".format(msg.topic, payload))
        data = json.loads(payload)

        # 打印接收到的数据
        logger.info(f"Received data on topic {msg.topic}: {data}")

        if msg.topic == "uwb/data":
            process_data(data)
        elif msg.topic == "imu/data":
            process_imu_data(data)
        elif msg.topic == "tmp/data":
            process_temp_data(data)
    except Exception as e:
        logger.error(f"Error processing message: {e}")

def process_semaphore_message(client, userdata, msg):
    global semaphore_holder
    try:
        payload = msg.payload.decode()
        logger.info(f"Received semaphore message on topic {msg.topic}: {payload}")
        data = json.loads(payload)

        if data["action"] == "request":
            semaphore_queue.put(data["tagID"])
            if semaphore_holder is None and not semaphore_queue.empty():
                semaphore_holder = semaphore_queue.get()
                client.publish("uwb/semaphore/grant", json.dumps({"tagID": semaphore_holder}))
        elif data["action"] == "release":
            if semaphore_holder == data["tagID"]:
                semaphore_holder = None
                if not semaphore_queue.empty():
                    semaphore_holder = semaphore_queue.get()
                    client.publish("uwb/semaphore/grant", json.dumps({"tagID": semaphore_holder}))
    except json.JSONDecodeError:
        logger.error("Error decoding JSON")

    logger.info(f"Current active tags: {list(active_tags)}")


def on_message(client, userdata, msg):
    try:
        if msg.topic in ["uwb/data", "imu/data", "tmp/data"]:
            executor.submit(process_message, client, userdata, msg)
        elif msg.topic == "uwb/tag/add":
            handle_tag_addition(msg.payload.decode())
        elif msg.topic == "uwb/tag/remove":
            handle_tag_removal(msg.payload.decode())
        elif msg.topic == "uwb/tag/heartbeat":
            handle_heartbeat_message(msg.payload.decode())
        elif msg.topic == "tdma/slot/request":
            handle_tdma_slot_request(client, msg.payload.decode())
    except Exception as e:
        logger.error(f"Error processing message: {e}")

def create_client(broker, port):
    client = mqtt.Client()
    client.on_connect = on_connect
    client.on_message = on_message
    client.connect(broker, port, 60)
    return client

def handle_tag_addition(payload):
    global active_tags
    try:
        data = json.loads(payload)
        tag_id = data.get("tagID")
        if tag_id:
            active_tags.add(tag_id)
            logger.info(f"Tag added: {tag_id}")
        else:
            logger.error("Received tag addition message without tagID")
    except json.JSONDecodeError as e:
        logger.error(f"Error processing tag addition: {e}")

def handle_tag_removal(payload):
    global active_tags, last_heartbeats
    data = json.loads(payload)
    tag_id = data.get("tagID")
    if tag_id:
        active_tags.discard(tag_id)
        last_heartbeats.pop(tag_id, None)  # 安全移除键值对
        logger.info(f"Tag removed: {tag_id}")
    else:
        logger.error("Tag removal message without tagID")

def send_active_tags(client):
    global active_tags
    data = {"activeTags": list(active_tags)}
    client.publish("uwb/tag/list", json.dumps(data))
    logger.info(f"Active tags list published: {data}")
    # 每隔一段时间发送一次
    threading.Timer(60, send_active_tags, [client]).start()
