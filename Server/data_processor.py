import json
from csv_manager import write_row_to_csv
import datetime

# 处理数据的函数
def process_data(data):
    timestamp = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    tag_id = data.get("id")
    anchors = data.get("anchors", [])

    # 准备CSV文件的一行数据
    row = [timestamp, tag_id]
    for i in range(3):  # 假设最多有3个Anchor
        if i < len(anchors):
            anchor = anchors[i]
            row.extend([anchor.get("id"), anchor.get("distance"), anchor.get("rssi")])
        else:
            row.extend([None, None, None])  # 如果没有足够的Anchor数据，填充空值

    write_row_to_csv(row, "uwb_data.csv")

def process_imu_data(data):
    timestamp = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    tag_id = data.get("id")
    acc = data.get("ACC", [None, None, None])  # 加速度
    mag = data.get("MAG", [None, None, None])  # 磁场
    gyr = data.get("GYR", [None, None, None])  # 陀螺仪

    # 准备CSV文件的一行数据
    row = [timestamp, tag_id] + acc + mag + gyr
    write_row_to_csv(row, "imu_data.csv")

def process_temp_data(data):
    timestamp = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    tag_id = data.get("id")
    temp = data.get("TMP")

    # 准备CSV文件的一行数据
    row = [timestamp, tag_id, temp]
    write_row_to_csv(row, "temp_data.csv")
