# 
# WARNING: This script was made by GPT and modified by ED -> Terrible combo
#
# Before using this script:
# - copy Autoflysher.py to the project root
# - check if nvs.csv (must be in root) has correct data
#
##### HOW TO:
# - Bring the ESP32 into flashing mode
# - Start the script with "python ./Autoflasher.py"
# - This script reads nvs.csv and increments the serial number
# - It then builds the project inlcuding nvs.bin and firmware.bin
# - Then flashes firmware.bin
# - After firmware.bin has been flashed. You must bring the ESPE32 into flashing mode again (You have around 10 seconds :)
# - Then nvs.bin gets flashed and all should be done


import os
import subprocess
import csv
import time

print("Script started")
print("Python interpreter path:", subprocess.run(["where", "python"], capture_output=True, text=True).stdout.strip())

def increment_serial_number(csv_file):
    print("Incrementing serial number...")
    rows = []
    serial_num_key = 'serial'
    serial_num_found = False
    hardware_version = None
    revision = None

    try:
        with open(csv_file, 'r', newline='') as file:
            reader = csv.DictReader(file)
            
            # Print headers for debugging
            print("CSV Headers:", reader.fieldnames)
            
            for row in reader:
                # Print each row for debugging
                print("Row:", row)
                if row.get('key') == serial_num_key:
                    current_value = row.get('value')
                    if current_value is None:
                        raise ValueError(f"Value for serial number (key: {serial_num_key}) is missing.")
                    new_value = str(int(current_value) + 1).zfill(len(current_value))
                    print(f"Serial Number incremented: {new_value}")  # Print right after incrementing
                    row['value'] = new_value
                    serial_num_found = True

                # Extracting hardware_version and revision
                if row.get('key') == 'hardware_version':
                    hardware_version = row.get('value')
                elif row.get('key') == 'revision':
                    revision = row.get('value')

                rows.append(row)
        
        if not serial_num_found:
            raise ValueError(f"Key '{serial_num_key}' not found in {csv_file}")

        # Write the updated content back to the csv
        with open(csv_file, 'w', newline='') as file:
            writer = csv.DictWriter(file, fieldnames=reader.fieldnames)
            writer.writeheader()
            writer.writerows(rows)
        
        # Return the serial number, hardware version, and revision
        return new_value, hardware_version, revision

    except Exception as e:
        print(f"An error occurred while incrementing the serial number: {e}")
        return False


def build_nvs_bin(source, target, env, csv_file_path):
    print("Starting build_nvs_bin function...")
    
    # Define the paths
    nvs_bin_path = os.path.join(project_dir, '.pio', 'build', 'esp32dev', 'nvs.bin')
    firmware_bin_path = os.path.join(project_dir, '.pio', 'build', 'esp32dev-ei-windows', 'firmware.bin')  # Adjust the path as needed

    # Increment the serial number and get details
    details = increment_serial_number(csv_file_path)
    if details is False:
        print("Failed to increment serial number. Aborting operation.")
        return

    serial_num, hardware_version, revision = details
    
    print("Building the project using PlatformIO...")
    build_command = [
        'platformio',
        'run',  # Use 'run' command to build the project
       # '-d', project_dir,  # Specify the project directory
        '-e', env,  # Specify the environment (e.g., 'esp32dev')
    ]

    # Execute the build command and check its return code
    try:
        build_process = subprocess.run(build_command, capture_output=True, text=True, check=True)
        print(build_process.stdout)
        if build_process.returncode != 0:
            print(f"Build failed with return code {build_process.returncode}")
            print(build_process.stderr)
            return
    except subprocess.CalledProcessError as e:
        print(f"An exception occurred during building: {e}")
        return
    # Upload the firmware.bin
    print("Checking if firmware.bin exists...")
    if os.path.exists(firmware_bin_path):
        print("Uploading firmware.bin...")
        firmware_upload_command = [
            'C:\\Users\\edste\\.platformio\\penv\\Scripts\\python.exe',  # Use the specific Python interpreter
            '-m', 'esptool',
            '--chip', 'esp32',
            '--port', 'COM6',  # Change this to your ESP32's serial port
            '--baud', '921600',
            'write_flash', '0x10000',  # Adjust the address if needed
            firmware_bin_path
        ]
        try:
            process = subprocess.Popen(firmware_upload_command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            while True:
                output = process.stdout.readline()
                if process.poll() is not None and output == '':
                    break
                if output:
                    print(output.strip())
            _, stderr = process.communicate()
            if process.returncode != 0:
                print(f"Firmware upload failed with return code {process.returncode}")
                print(stderr)
                return
            if stderr:
                print(stderr)
        except Exception as e:
            print(f"An error occurred during firmware upload: {e}")
            return
    else:
        print("firmware.bin not found. Please check the build process.")
        return

    # Define the command to upload nvs.bin using esptool
    upload_command = [
        'C:\\Users\\edste\\.platformio\\penv\\Scripts\\python.exe',  # Use the specific Python interpreter
        '-m', 'esptool',
        '--chip', 'esp32',
        '--port', 'COM6',  # Change this to your ESP32's serial port
        'write_flash', '0x9000', 
        nvs_bin_path
    ]
    
    for _ in range(10):
        print("Put ESP32 into UPLOAD mode")
        time.sleep(0.4)
        
    print("Checking if nvs.bin exists...")
    if os.path.exists(nvs_bin_path):
        print("Uploading nvs.bin...")
        try:
            process = subprocess.Popen(upload_command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            
            # Print output in real-time
            while True:
                output = process.stdout.readline()
                if process.poll() is not None and output == '':
                    break
                if output:
                    print(output.strip())
            
            # Check if process has ended and print any final output
            _, stderr = process.communicate()
            if process.returncode != 0:
                print(f"Upload failed with return code {process.returncode}")
                print(stderr)
                return
            if stderr:
                print(stderr)
        except Exception as e:
            print(f"An error occurred during upload: {e}")

    else:
        print("nvs.bin not found. Please check the build process.")

    # Print the details at the end of the function
    print(f"Serial Number: {serial_num}")
    print(f"Hardware Version: {hardware_version}")
    print(f"Revision: {revision}")

# Example of calling the build function (you'll need to define project_dir or pass it to the function)
try:
    print("Calling build_nvs_bin...")  # Confirm this part of the script is reached
    project_dir = 'C:\Projects_Local\ELOC\Firmware\ELOC-3.0\ELOC-3.0\eloc610LowPowerPartition'
    csv_file_path = os.path.join(project_dir, 'nvs.csv')
    
    # Define or get these parameters as needed. 
    # Replace 'your_source', 'your_target', 'your_env' with actual values or remove if not needed.
    source = 'your_source'
    target = 'your_target'
    env = 'esp32dev-ei-windows'
    
    # Call the function to build and upload the nvs.bin
    build_nvs_bin(source, target, env, csv_file_path)
except Exception as e:
    print(f"An error occurred: {e}")