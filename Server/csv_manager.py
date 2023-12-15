import csv
import datetime

# 全局变量来管理CSV文件
current_hour = None
csv_files = {}
csv_writers = {}
csv_filename = None

def write_row_to_csv(row, filename):
    global csv_writers, current_hour

    # 检查是否需要创建新的CSV文件
    maybe_open_new_csv_file(filename)

    # 写入数据行
    if filename in csv_writers:
        csv_writers[filename].writerow(row)

def open_new_csv_file(data_type):
    global csv_files, csv_writers, current_hour, csv_filename
    current_hour = datetime.datetime.now().hour
    timestamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    csv_filename = f"{data_type}_DATA_{timestamp}.csv"
    csv_file = open(csv_filename, 'w', newline='', encoding='utf-8')
    csv_writers[data_type] = csv.writer(csv_file)
    csv_files[data_type] = csv_file
    # 定义CSV文件的列头
    if data_type == "imu_data":
        csv_writers[data_type].writerow(["Timestamp", "Tag ID", "ACC X", "ACC Y", "ACC Z", "MAG X", "MAG Y", "MAG Z", "GYR X", "GYR Y", "GYR Z"])
    elif data_type == "temp_data":
        csv_writers[data_type].writerow(["Timestamp", "Tag ID", "Temperature"])
    elif data_type == "uwb_data":
        csv_writers[data_type].writerow(["Timestamp", "Tag ID", "Anchor ID 1", "Distance 1", "RSSI 1", "Anchor ID 2", "Distance 2", "RSSI 2", "Anchor ID 3", "Distance 3", "RSSI 3"])

def maybe_open_new_csv_file(filename):
    global csv_files, csv_writers, current_hour
    if datetime.datetime.now().hour != current_hour or filename not in csv_files:
        close_csv_file(filename)  # 关闭旧文件
        current_hour = datetime.datetime.now().hour
        timestamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
        csv_filename = f"{filename}_{timestamp}.csv"
        csv_files[filename] = open(csv_filename, 'w', newline='', encoding='utf-8')
        csv_writers[filename] = csv.writer(csv_files[filename])

def close_csv_file(filename=None):
    global csv_files
    if filename:
        if filename in csv_files and csv_files[filename]:
            csv_files[filename].close()
            del csv_files[filename]
    else:
        for file in list(csv_files.keys()):
            if csv_files[file]:
                csv_files[file].close()
                del csv_files[file]
