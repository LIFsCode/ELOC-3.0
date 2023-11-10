
import os
import csv
import subprocess
import cryptography

#TODO: this script is not working properly with the paths. 
# works if executed from an ESP-IDF instlal
# does not work when running from platformio

idf_path = os.environ['IDF_PATH']
IDF_PYTHON_ENV_PATH = os.environ['IDF_PYTHON_ENV_PATH']
partition_file = "../partitions_example.csv"

python = IDF_PYTHON_ENV_PATH + "/Scripts/python.exe"
esp_tool = idf_path + "/components/esptool_py/esptool/esptool.py"
nvs_tool = idf_path + "/components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py"
subprocess.call([python, '-V'])

with open(partition_file) as csv_file:
    csv_reader = csv.reader(csv_file, delimiter=',')
    nvs_base = 0
    nvs_size = 0
    for row in csv_reader:
        print(row)
        if (row[0] == "nvs"):
            print(f'NVS base: {row[3]}')
            nvs_base = row[3]
            nvs_size = row[4]
        if (row[0] == "partition0"):
            print(f'NVS base: {row[3]}')
            partition_base = row[3]
    subprocess.call([python, nvs_tool,'generate', '../nvs.csv','../nvs.bin', nvs_size])
    subprocess.call([python, -m esptool,'write_flash', nvs_base, '../nvs.bin'])
    #TODO: add other partitions (bootloader.bin, ota_data_initial.bin, partitions.bin, firmware.bin
    print("Done")
    exit(0)
    
print("Failed to read partitions")
exit(1)

