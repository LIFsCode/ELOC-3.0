#!/usr/bin/env python3
"""
ELOC Firmware Auto-Flasher
---------------------------
Automated tool for flashing ELOC firmware and NVS data.

Features:
- Auto-detects available COM ports
- Configurable via command line arguments
- Progress indicators for long operations
- Robust error handling
- Randomly generates LoRa keys (devEUI, appKey, nwkKey)
- Logs updated keys/serial number in keyfile.csv
- Reads hw_gen and hw_rev from nvs.csv, saves them in keyfile.csv
"""

import os
import sys
import csv
import time
import argparse
import serial.tools.list_ports
from tqdm import tqdm
import subprocess
from typing import Tuple
import secrets  # for cryptographically secure random hex

class AutoFlasher:
    def __init__(self, args):
        self.project_dir = args.project_dir or os.getcwd()
        self.port = args.port
        self.env = args.env
        self.baud_rate = args.baud_rate
        self.csv_file = os.path.join(self.project_dir, 'nvs.csv')
        
        # This file will store the historical record of all newly generated keys/serials
        self.keyfile_csv = os.path.join(self.project_dir, 'keyfile.csv')

        # Hardcode the firmware and NVS paths to the correct directories
        self.firmware_bin_path = os.path.join(
            self.project_dir, '.pio', 'build', 'esp32dev-ei', 'firmware.bin'
        )
        self.nvs_bin_path = os.path.join(
            self.project_dir, '.pio', 'build', 'esp32dev', 'nvs.bin'
        )

    @staticmethod
    def detect_com_ports() -> list:
        """Auto-detect available COM ports."""
        return [port.device for port in serial.tools.list_ports.comports()]

    @staticmethod
    def select_com_port(ports: list) -> str:
        """Let user select COM port if multiple are available."""
        if not ports:
            raise RuntimeError("No COM ports detected!")
        if len(ports) == 1:
            return ports[0]
        
        print("\nAvailable COM ports:")
        for i, port in enumerate(ports):
            print(f"{i + 1}: {port}")
        while True:
            try:
                choice = int(input("\nSelect COM port number: ")) - 1
                if 0 <= choice < len(ports):
                    return ports[choice]
            except ValueError:
                pass
            print("Invalid selection. Please try again.")

    def increment_serial_number(self) -> Tuple[str, str, str]:
        """
        Increments the 'serial' field in nvs.csv
        Also reads 'hw_gen' and 'hw_rev' from the same CSV.
        Returns:
            serial_num (str): Incremented serial number
            hw_gen (str): hardware generation (from nvs.csv)
            hw_rev (str): hardware revision (from nvs.csv)
        """
        print("\nProcessing serial number...")
        rows = []
        serial_num = None
        hw_gen = None
        hw_rev = None

        try:
            with open(self.csv_file, 'r', newline='') as file:
                reader = csv.DictReader(file)
                fieldnames = reader.fieldnames
                for row in reader:
                    if row.get('key') == 'serial':
                        current_value = row.get('value')
                        if not current_value:
                            raise ValueError("Serial number value is missing")
                        serial_num = str(int(current_value) + 1).zfill(len(current_value))
                        row['value'] = serial_num

                    # Also read hw_gen and hw_rev from the CSV
                    elif row.get('key') == 'hw_gen':
                        hw_gen = row.get('value')
                    elif row.get('key') == 'hw_rev':
                        hw_rev = row.get('value')

                    rows.append(row)

            if not serial_num:
                raise ValueError("Serial number key not found in CSV")

            # Write back updated rows (with new serial number if changed)
            with open(self.csv_file, 'w', newline='') as file:
                writer = csv.DictWriter(file, fieldnames=fieldnames)
                writer.writeheader()
                writer.writerows(rows)

            print(f"Serial number incremented to: {serial_num}")
            print(f"Found hw_gen: {hw_gen}, hw_rev: {hw_rev}")
            return serial_num, hw_gen, hw_rev

        except Exception as e:
            raise RuntimeError(f"Failed to process serial number: {e}")

    def generate_and_update_lora_keys(self) -> Tuple[str, str, str]:
        """
        Generate random devEUI, appKey, and nwkKey, then update them in nvs.csv.
        Returns:
            (devEUI, appKey, nwkKey)
        """
        print("\nGenerating and updating LoRa keys...")

        # Generate random keys:
        # devEUI = 8 bytes in hex => 16 hex chars
        devEUI = secrets.token_hex(8).upper()
        # appKey & nwkKey = 16 bytes in hex => 32 hex chars each
        appKey = secrets.token_hex(16).upper()
        nwkKey = secrets.token_hex(16).upper()

        # Update them in the nvs.csv file
        rows = []
        fieldnames = []
        try:
            with open(self.csv_file, 'r', newline='') as file:
                reader = csv.DictReader(file)
                fieldnames = reader.fieldnames
                for row in reader:
                    if row.get('key') == 'devEUI':
                        row['value'] = devEUI
                    elif row.get('key') == 'appKey':
                        row['value'] = appKey
                    elif row.get('key') == 'nwkKey':
                        row['value'] = nwkKey
                    rows.append(row)

            with open(self.csv_file, 'w', newline='') as file:
                writer = csv.DictWriter(file, fieldnames=fieldnames)
                writer.writeheader()
                writer.writerows(rows)

            print(f"Generated devEUI: {devEUI}")
            print(f"Generated appKey: {appKey}")
            print(f"Generated nwkKey: {nwkKey}")

            return devEUI, appKey, nwkKey

        except Exception as e:
            raise RuntimeError(f"Failed to update LoRa keys in {self.csv_file}: {e}")

    def log_keys_to_keyfile(self, serial_num: str, hw_gen: str, hw_rev: str,
                            devEUI: str, appKey: str, nwkKey: str):
        """
        Append a new line into keyfile.csv with the updated serial and newly generated keys,
        including hw_gen/hw_rev from nvs.csv.
        """
        fieldnames = [
            'Serial',
            'devEUI',
            'appKey',
            'nwkKey',
            'hw_gen',
            'hw_rev',
            'timestamp'
        ]
        
        file_exists = os.path.isfile(self.keyfile_csv)
        current_time = time.strftime("%Y-%m-%d %H:%M:%S")

        try:
            with open(self.keyfile_csv, 'a', newline='') as file:
                writer = csv.DictWriter(file, fieldnames=fieldnames)
                if not file_exists:
                    writer.writeheader()
                
                writer.writerow({
                    'Serial': serial_num,
                    'devEUI': devEUI,
                    'appKey': appKey,
                    'nwkKey': nwkKey,
                    'hw_gen': hw_gen,
                    'hw_rev': hw_rev,
                    'timestamp': current_time
                })

            print(f"\nAppended new keys and serial to {self.keyfile_csv}.")

        except Exception as e:
            raise RuntimeError(f"Failed to log keys to {self.keyfile_csv}: {e}")

    def build_project(self):
        """Build the project using PlatformIO."""
        print("\nBuilding project...")
        build_command = ['platformio', 'run', '-e', self.env]

        process = subprocess.Popen(
            build_command,
            stdout=sys.stdout,
            stderr=sys.stderr,
            text=True
        )
        return_code = process.wait()

        if return_code != 0:
            raise RuntimeError(f"Build failed with return code: {return_code}")
        print("Build successful!")

    def flash_binary(self, binary_path: str, offset: str, wait_for_upload_mode: bool = False):
        """Flash a binary file to ESP32."""
        if not os.path.exists(binary_path):
            raise FileNotFoundError(f"Binary file not found: {binary_path}")

        if wait_for_upload_mode:
            print("\nPreparing to flash... Please put ESP32 into upload mode!")
            for _ in tqdm(range(10), desc="Waiting"):
                time.sleep(0.5)

        flash_command = [
            sys.executable,
            '-m', 'esptool',
            '--chip', 'esp32',
            '--port', self.port,
            '--baud', str(self.baud_rate),
            'write_flash',
            offset,
            binary_path
        ]

        print(f"\nFlashing {os.path.basename(binary_path)}...")
        process = subprocess.Popen(
            flash_command,
            stdout=sys.stdout,
            stderr=sys.stderr,
            text=True
        )
        return_code = process.wait()

        if return_code != 0:
            raise RuntimeError(f"Flashing {binary_path} failed with return_code: {return_code}")
        print(f"Successfully flashed {os.path.basename(binary_path)}")

    def run(self):
        """Main execution flow."""
        print("Starting run() method...")
        try:
            # Auto-detect and select COM port if not specified
            if not self.port:
                ports = self.detect_com_ports()
                self.port = self.select_com_port(ports)
                print(f"\nUsing COM port: {self.port}")

            # 1. Process serial number (also fetches hw_gen and hw_rev)
            serial_num, hw_gen, hw_rev = self.increment_serial_number()

            # 2. Generate random LoRaWAN keys, update them in nvs.csv
            devEUI, appKey, nwkKey = self.generate_and_update_lora_keys()

            # 3. Log the newly generated keys + serial + hw_gen/hw_rev into keyfile.csv
            self.log_keys_to_keyfile(serial_num, hw_gen, hw_rev, devEUI, appKey, nwkKey)

            # 4. Build project
            self.build_project()

            # 5. Flash firmware.bin
            print("\nPreparing to flash firmware...")
            self.flash_binary(self.firmware_bin_path, "0x10000", True)

            # Pause to let user put device back in download mode
            input("\nFirmware flashed successfully! "
                  "Please put the ESP32 into download mode again and press Enter to continue...")

            # 6. Flash nvs.bin
            print("\nPreparing to flash NVS data...")
            self.flash_binary(self.nvs_bin_path, "0x9000", True)

            print("\n=== Flash Complete ===")
            print(f"Serial Number: {serial_num}")
            print(f"hw_gen: {hw_gen}")
            print(f"hw_rev: {hw_rev}")
            print(f"devEUI: {devEUI}")
            print(f"appKey: {appKey}")
            print(f"nwkKey: {nwkKey}")

        except Exception as e:
            print(f"\nError: {e}")
            sys.exit(1)

def main():
    print("Entering main()...")
    parser = argparse.ArgumentParser(description="ESP32 Firmware Auto-Flasher")
    parser.add_argument("--port", help="COM port (auto-detected if not specified)")
    parser.add_argument("--project-dir", help="Project directory (default: current directory)")
    parser.add_argument("--env", default="esp32dev-ei", help="PlatformIO environment (default: esp32dev-ei)")
    parser.add_argument("--baud-rate", type=int, default=921600, help="Baud rate (default: 921600)")
    
    args = parser.parse_args()
    
    try:
        flasher = AutoFlasher(args)
        flasher.run()
    except KeyboardInterrupt:
        print("\nOperation cancelled by user")
        sys.exit(1)
    except Exception as e:
        print(f"\nUnexpected error: {e}")
        sys.exit(1)

if __name__ == "__main__":
    main()
