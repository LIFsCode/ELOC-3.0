#!/usr/bin/env python3
"""
ESP32 Firmware Auto-Flasher
--------------------------
Automated tool for flashing ESP32 firmware and NVS data with improved usability.

Features:
- Auto-detects available COM ports
- Configurable via command line arguments
- Progress indicators for long operations
- Robust error handling
"""

import os
import sys
import csv
import time
import argparse
import serial.tools.list_ports
from tqdm import tqdm
import subprocess
from typing import Tuple, Optional

class AutoFlasher:
    def __init__(self, args):
        self.project_dir = args.project_dir or os.getcwd()
        self.port = args.port
        self.env = args.env
        self.baud_rate = args.baud_rate
        self.csv_file = os.path.join(self.project_dir, 'nvs.csv')

        # Hardcode the firmware and NVS paths to the correct directories
        self.firmware_bin_path = os.path.join(self.project_dir, '.pio', 'build', 'esp32dev-ei', 'firmware.bin')
        self.nvs_bin_path = os.path.join(self.project_dir, '.pio', 'build', 'esp32dev', 'nvs.bin')


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
        """Increment serial number in NVS CSV file."""
        print("\nProcessing serial number...")
        rows = []
        serial_num = None
        hardware_version = None
        revision = None

        try:
            with open(self.csv_file, 'r', newline='') as file:
                reader = csv.DictReader(file)
                for row in reader:
                    if row.get('key') == 'serial':
                        current_value = row.get('value')
                        if not current_value:
                            raise ValueError("Serial number value is missing")
                        serial_num = str(int(current_value) + 1).zfill(len(current_value))
                        row['value'] = serial_num
                    elif row.get('key') == 'hardware_version':
                        hardware_version = row.get('value')
                    elif row.get('key') == 'revision':
                        revision = row.get('value')
                    rows.append(row)

            if not serial_num:
                raise ValueError("Serial number key not found in CSV")

            with open(self.csv_file, 'w', newline='') as file:
                writer = csv.DictWriter(file, fieldnames=reader.fieldnames)
                writer.writeheader()
                writer.writerows(rows)

            print(f"Serial number incremented to: {serial_num}")
            return serial_num, hardware_version, revision

        except Exception as e:
            raise RuntimeError(f"Failed to process serial number: {e}")

    def build_project(self):
        """Build the project using PlatformIO."""
        print("\nBuilding project...")
        build_command = ['platformio', 'run', '-e', self.env]

        # Run the process without capturing output, so logs appear in real-time.
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

        # Run esptool command and show output in real-time
        print(f"\nFlashing {os.path.basename(binary_path)}...")
        process = subprocess.Popen(
            flash_command,
            stdout=sys.stdout,
            stderr=sys.stderr,
            text=True
        )
        return_code = process.wait()

        if return_code != 0:
            raise RuntimeError(f"Flashing {binary_path} failed with return code: {return_code}")
        print(f"Successfully flashed {os.path.basename(binary_path)}")


    def run(self):
        """Main execution flow."""
        try:
            # Auto-detect and select COM port if not specified
            if not self.port:
                ports = self.detect_com_ports()
                self.port = self.select_com_port(ports)
                print(f"\nUsing COM port: {self.port}")

            # Process serial number
            serial_num, hw_version, revision = self.increment_serial_number()

            # Build project
            self.build_project()

            # Flash firmware.bin
            print("\nPreparing to flash firmware...")
            self.flash_binary(self.firmware_bin_path, "0x10000", True)
            
            # Prompt the user to put ESP32 into download mode again
            input("\nFirmware flashed successfully! Please put the ESP32 into download mode again and press Enter to continue...")

            # Flash nvs.bin
            print("\nPreparing to flash NVS data...")
            self.flash_binary(self.nvs_bin_path, "0x9000", True)

            print("\n=== Flash Complete ===")
            print(f"Serial Number: {serial_num}")
            print(f"Hardware Version: {hw_version}")
            print(f"Revision: {revision}")

        except Exception as e:
            print(f"\nError: {e}")
            sys.exit(1)

def main():
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
