from mqtt_client import create_client, logger, send_active_tags, check_tag_timeout
import time
from csv_manager import close_csv_file
import configparser
import threading

config = configparser.ConfigParser()
config.read('config.ini')

MQTT_BROKER = config['MQTT']['Broker']
MQTT_PORT = config['MQTT'].getint('Port')

def main():
    client = create_client(MQTT_BROKER, MQTT_PORT)
    try:
        client.loop_start()  # 使用非阻塞的循环启动
        while True:
            time.sleep(10)  # 主线程继续其他任务或保持空闲
    except KeyboardInterrupt:
        logger.info("Stopping MQTT client")
    finally:
        client.loop_stop()  # 清理并停止网络循环
        client.disconnect()

if __name__ == "__main__":
    main()
