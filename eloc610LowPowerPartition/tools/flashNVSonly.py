import os
import subprocess

def flash_nvs_bin(project_root, env):
    print("Starting flash_nvs_bin function...")
    
    # Path to nvs.bin within the project directory
    nvs_bin_path = os.path.join(project_root, '.pio', 'build', 'esp32dev', 'nvs.bin')

    # Verify the existence of nvs.bin
    if not os.path.exists(nvs_bin_path):
        print(f"nvs.bin not found at {nvs_bin_path}. Please check the path and try again.")
        return
    else:
        print(f"nvs.bin found at {nvs_bin_path}. Proceeding with upload...")

    # Command to upload nvs.bin using esptool
    upload_command = [
        'C:\\Users\\edste\\AppData\\Local\\Programs\\Python\\Python311\\python.exe',  # Adjust as necessary
        '-m', 'esptool',
        '--chip', 'esp32',
        '--port', 'COM6',  # Adjust as necessary
        '--baud', '921600',
        'write_flash', '0x9000', 
        nvs_bin_path
    ]

    # Execute the upload command and print real-time output
    try:
        process = subprocess.Popen(upload_command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        
        print("Uploading nvs.bin... Please wait.")
        
        # Capture and print stdout and stderr in real-time
        while True:
            output = process.stdout.readline()
            if output == '' and process.poll() is not None:
                break
            if output:
                print(output.strip())
        _, stderr = process.communicate()
        
        # Check for errors after process completion
        if process.returncode != 0:
            print(f"Upload failed with return code {process.returncode}. Error: {stderr}")
            return
        else:
            print("Upload completed successfully.")
        
        if stderr:
            print(f"Error during upload: {stderr}")
        
    except Exception as e:
        print(f"An error occurred during upload: {e}")

# Adjusted usage example to dynamically calculate project root
try:
    script_dir = os.path.dirname(os.path.realpath(__file__))
    project_root = os.path.abspath(os.path.join(script_dir, os.pardir))
    env = 'esp32dev-ei-windows'  # Adjust environment as necessary
    
    flash_nvs_bin(project_root, env)
except Exception as e:
    print(f"An error occurred: {e}")
