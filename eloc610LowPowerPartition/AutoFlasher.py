#!/usr/bin/env python3
"""
ELOC Firmware Auto-Flasher (All-in-One)
----------------------------------------
Flashes bootloader, partition table, firmware, and NVS in a single esptool command.
Only one download mode entry needed.

Additionally:
- Increments the serial number in nvs.csv
- Generates new devEUI, appKey, and nwkKey each time
- Logs the final values from nvs.csv to keyfile.csv for record-keeping
- Prints them out at the end
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
import secrets  # for secure random hex generation
from datetime import datetime  # for timestamp logging


class AutoFlasher:
    def __init__(self, args):
        self.project_dir = args.project_dir or os.getcwd()
        self.port = args.port
        self.env = args.env
        self.baud_rate = args.baud_rate
        self.csv_file = os.path.join(self.project_dir, 'nvs.csv')

        # Where to log updated values
        self.keyfile_csv = os.path.join(self.project_dir, 'keyfile.csv')

        # Hardcoded paths - adjust environment/folder names to match your build output
        self.bootloader_bin_path = os.path.join(
            self.project_dir,
            '.pio', 'build', 'esp32dev-ei',
            'bootloader.bin'
        )
        self.partitions_bin_path = os.path.join(
            self.project_dir,
            '.pio', 'build', 'esp32dev-ei',
            'partitions.bin'
        )
        self.firmware_bin_path = os.path.join(
            self.project_dir,
            '.pio', 'build', 'esp32dev-ei',
            'firmware.bin'
        )
        self.nvs_bin_path = os.path.join(
            self.project_dir,
            '.pio', 'build', 'esp32dev',
            'nvs.bin'
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

    def increment_serial_number(self) -> Tuple[str, str, str, str, str, str]:
        """
        Increments serial number in NVS CSV, generates new devEUI/appKey/nwkKey,
        and also reads hw_gen and hw_rev from the CSV.

        Returns 6 values:
          (serial_num, hw_gen, hw_rev, devEUI, appKey, nwkKey)
        """
        print("\nProcessing serial number and generating LoRa credentials...")
        rows = []
        serial_num = None
        hw_gen = None
        hw_rev = None
        devEUI = None
        appKey = None
        nwkKey = None

        try:
            # Read CSV into memory
            with open(self.csv_file, 'r', newline='') as file:
                reader = csv.DictReader(file)
                fieldnames = reader.fieldnames  # keep track of original columns

                for row in reader:
                    key = row.get('key')

                    if key == 'serial':
                        current_value = row.get('value')
                        if not current_value:
                            raise ValueError("Serial number value is missing")
                        # Increment the serial number
                        serial_num = str(int(current_value) + 1).zfill(len(current_value))
                        row['value'] = serial_num

                    elif key == 'hw_gen':
                        hw_gen = row.get('value')

                    elif key == 'hw_rev':
                        hw_rev = row.get('value')

                    elif key == 'devEUI':
                        # Generate new devEUI (8 bytes => 16 hex chars)
                        devEUI = secrets.token_hex(8).upper()
                        row['value'] = devEUI

                    elif key == 'appKey':
                        # Generate new appKey (16 bytes => 32 hex chars)
                        appKey = secrets.token_hex(16).upper()
                        row['value'] = appKey

                    elif key == 'nwkKey':
                        # Generate new nwkKey (16 bytes => 32 hex chars)
                        nwkKey = secrets.token_hex(16).upper()
                        row['value'] = nwkKey

                    rows.append(row)

            if not serial_num:
                raise ValueError("Serial number key not found in CSV")

            # If devEUI, appKey, or nwkKey were missing entirely, generate & append them
            if devEUI is None:
                devEUI = secrets.token_hex(8).upper()
                rows.append({
                    'key': 'devEUI',
                    'type': 'data',
                    'encoding': 'string',
                    'value': devEUI
                })
            if appKey is None:
                appKey = secrets.token_hex(16).upper()
                rows.append({
                    'key': 'appKey',
                    'type': 'data',
                    'encoding': 'string',
                    'value': appKey
                })
            if nwkKey is None:
                nwkKey = secrets.token_hex(16).upper()
                rows.append({
                    'key': 'nwkKey',
                    'type': 'data',
                    'encoding': 'string',
                    'value': nwkKey
                })

            # Write updated CSV (nvs.csv)
            with open(self.csv_file, 'w', newline='') as file:
                if not fieldnames:
                    fieldnames = ['key', 'type', 'encoding', 'value']
                writer = csv.DictWriter(file, fieldnames=fieldnames)
                writer.writeheader()
                writer.writerows(rows)

            print(f"Serial number incremented to: {serial_num}")
            print(f"hw_gen: {hw_gen if hw_gen else '(none)'}")
            print(f"hw_rev: {hw_rev if hw_rev else '(none)'}")
            print(f"New devEUI: {devEUI}")
            print(f"New appKey: {appKey}")
            print(f"New nwkKey: {nwkKey}")

            return serial_num, hw_gen, hw_rev, devEUI, appKey, nwkKey

        except Exception as e:
            raise RuntimeError(f"Failed to process serial number (or LoRa keys): {e}")

    def log_keys_to_keyfile(
        self,
        serial_num: str,
        hw_gen: str,
        hw_rev: str,
        devEUI: str,
        appKey: str,
        nwkKey: str
    ):
        """
        Logs all updated values to 'keyfile.csv' for record-keeping.

        Required header: Serial,devEUI,appKey,nwkKey,hw_gen,hw_rev,timestamp
        """
        # Make sure each is a string
        hw_gen = hw_gen or ""
        hw_rev = hw_rev or ""

        # Define columns for keyfile.csv in the required order
        fieldnames = [
            'Serial',
            'devEUI',
            'appKey',
            'nwkKey',
            'hw_gen',
            'hw_rev',
            'timestamp'
        ]

        # Create row data
        from datetime import datetime
        row_dict = {
            'Serial': serial_num,
            'devEUI': devEUI,
            'appKey': appKey,
            'nwkKey': nwkKey,
            'hw_gen': hw_gen,
            'hw_rev': hw_rev,
            'timestamp': datetime.now().isoformat(sep=' ', timespec='seconds')
        }

        file_exists = os.path.isfile(self.keyfile_csv)
        with open(self.keyfile_csv, 'a', newline='') as f:
            writer = csv.DictWriter(f, fieldnames=fieldnames)
            # Write header if file is new
            if not file_exists:
                writer.writeheader()
            writer.writerow(row_dict)

        print(f"\nLogged updated keys to: {self.keyfile_csv}")

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

    def flash_all_in_one(self):
        """
        Flash bootloader, partition table, NVS, and firmware in one esptool command.
        This means only one prompt to enter download mode is required.
        """
        # Validate that all files exist
        for path in [
            self.bootloader_bin_path,
            self.partitions_bin_path,
            self.nvs_bin_path,
            self.firmware_bin_path
        ]:
            if not os.path.exists(path):
                raise FileNotFoundError(f"Required binary not found: {path}")

        # Prompt user to put device into download mode (only once)
        print("\nPreparing to flash everything in one go... Please put ESP32 into download mode!")
        for _ in tqdm(range(10), desc="Waiting"):
            time.sleep(0.5)

        # Single esptool command with all offsets and files
        flash_command = [
            sys.executable,
            '-m', 'esptool',
            '--chip', 'esp32',
            '--port', self.port,
            '--baud', str(self.baud_rate),
            'write_flash',
            # offset, file
            '0x1000',   self.bootloader_bin_path,
            '0x8000',   self.partitions_bin_path,
            '0x9000',   self.nvs_bin_path,
            '0x10000',  self.firmware_bin_path
        ]

        print("\nFlashing all binaries...")
        process = subprocess.Popen(
            flash_command,
            stdout=sys.stdout,
            stderr=sys.stderr,
            text=True
        )
        return_code = process.wait()

        if return_code != 0:
            raise RuntimeError(f"Flashing all images failed with return code: {return_code}")

        print("Successfully flashed bootloader, partition table, NVS, and firmware!")

    def run(self):
        """Main execution flow."""
        try:
            # Auto-detect/select COM port if not specified
            if not self.port:
                ports = self.detect_com_ports()
                self.port = self.select_com_port(ports)
                print(f"\nUsing COM port: {self.port}")

            # Increment serial & generate new devEUI/appKey/nwkKey
            serial_num, hw_gen, hw_rev, devEUI, appKey, nwkKey = self.increment_serial_number()

            # Log all updated numbers to keyfile.csv
            self.log_keys_to_keyfile(serial_num, hw_gen, hw_rev, devEUI, appKey, nwkKey)

            # Build the project
            self.build_project()

            # Flash all binaries at once
            self.flash_all_in_one()

            # Print summary
            print("\n=== Flash Complete ===")
            print(f"Serial Number:     {serial_num}")
            print(f"Hardware Gen:      {hw_gen}")
            print(f"Hardware Rev:      {hw_rev}")
            print(f"devEUI:            {devEUI}")
            print(f"appKey:            {appKey}")
            print(f"nwkKey:            {nwkKey}")

        except Exception as e:
            print(f"\nError: {e}")
            sys.exit(1)


def main():
    parser = argparse.ArgumentParser(description="ESP32 Firmware Auto-Flasher (All-in-One)")
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
