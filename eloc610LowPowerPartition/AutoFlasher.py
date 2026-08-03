#!/usr/bin/env python3
"""
ELOC Firmware Auto-Flasher (All-in-One)
----------------------------------------
Flashes bootloader, partition table, firmware, and NVS in a single esptool command.
Only one download mode entry needed.

Additionally:
- Increments the serial number in nvs.csv
- Derives the Bluetooth/config name from that serial (e.g. 123 -> ELOC_00123)
- Generates new devEUI, appKey, and nwkKey each time
- Logs successfully flashed values to keyfile.csv for record-keeping
- Prints them out at the end
"""

import os
import sys
import csv
import time
import argparse
import subprocess
from typing import Tuple
import secrets  # for secure random hex generation
from datetime import datetime  # for timestamp logging


def format_device_name(serial_num: str) -> str:
    """Format an NVS serial as ELOC_<last-five-digits>."""
    serial_value = int(serial_num)
    if serial_value < 0:
        raise ValueError("Serial number cannot be negative")
    return f"ELOC_{serial_value % 100000:05d}"


class AutoFlasher:
    def __init__(self, args):
        self.project_dir = args.project_dir or os.getcwd()
        self.port = args.port
        self.env = args.env
        self.baud_rate = args.baud_rate
        self.factory_reset_config = args.factory_reset_config
        self.csv_file = os.path.join(self.project_dir, 'nvs.csv')
        self.partition_table_csv = os.path.join(self.project_dir, 'elocPartitions.csv')

        # Where to log updated values
        self.keyfile_csv = os.path.join(self.project_dir, 'keyfile.csv')

        # Build output paths - derived from the selected PlatformIO environment
        build_dir = os.path.join(self.project_dir, '.pio', 'build', self.env)
        self.bootloader_bin_path = os.path.join(build_dir, 'bootloader.bin')
        self.partitions_bin_path = os.path.join(build_dir, 'partitions.bin')
        self.firmware_bin_path = os.path.join(build_dir, 'firmware.bin')
        self.nvs_bin_path = os.path.join(build_dir, 'nvs.bin')

    @staticmethod
    def detect_com_ports() -> list:
        """Auto-detect available COM ports."""
        import serial.tools.list_ports
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

    def increment_serial_number(self) -> Tuple[str, str, str, str, str, str, str]:
        """
        Increments serial number in NVS CSV, generates new devEUI/appKey/nwkKey,
        and also reads hw_gen and hw_rev from the CSV.

        Returns 7 values:
          (serial_num, device_name, hw_gen, hw_rev, devEUI, appKey, nwkKey)
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

            device_name = format_device_name(serial_num)

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
            print(f"Device name: {device_name}")
            print(f"hw_gen: {hw_gen if hw_gen else '(none)'}")
            print(f"hw_rev: {hw_rev if hw_rev else '(none)'}")
            print(f"New devEUI: {devEUI}")
            print(f"New appKey: {appKey}")
            print(f"New nwkKey: {nwkKey}")

            return serial_num, device_name, hw_gen, hw_rev, devEUI, appKey, nwkKey

        except Exception as e:
            raise RuntimeError(f"Failed to process serial number (or LoRa keys): {e}")

    def log_keys_to_keyfile(
        self,
        serial_num: str,
        device_name: str,
        hw_gen: str,
        hw_rev: str,
        devEUI: str,
        appKey: str,
        nwkKey: str
    ):
        """
        Logs all updated values to 'keyfile.csv' for record-keeping.

        Existing keyfiles are migrated in place to add the DeviceName column.
        """
        # Make sure each is a string
        hw_gen = hw_gen or ""
        hw_rev = hw_rev or ""

        # Define columns for keyfile.csv in the required order
        fieldnames = [
            'Serial',
            'DeviceName',
            'devEUI',
            'appKey',
            'nwkKey',
            'hw_gen',
            'hw_rev',
            'timestamp'
        ]

        # Create row data
        row_dict = {
            'Serial': serial_num,
            'DeviceName': device_name,
            'devEUI': devEUI,
            'appKey': appKey,
            'nwkKey': nwkKey,
            'hw_gen': hw_gen,
            'hw_rev': hw_rev,
            'timestamp': datetime.now().isoformat(sep=' ', timespec='seconds')
        }

        file_exists = os.path.isfile(self.keyfile_csv) and os.path.getsize(self.keyfile_csv) > 0
        if file_exists:
            with open(self.keyfile_csv, 'r', newline='') as f:
                reader = csv.DictReader(f)
                existing_fieldnames = reader.fieldnames or []
                existing_rows = list(reader)

            # Preserve any extra columns while inserting DeviceName into older keyfiles. Rewriting
            # is done through a sibling temporary file so interruption cannot corrupt the log.
            merged_fieldnames = fieldnames + [
                name for name in existing_fieldnames if name not in fieldnames
            ]
            if existing_fieldnames != merged_fieldnames:
                temp_keyfile = self.keyfile_csv + '.tmp'
                with open(temp_keyfile, 'w', newline='') as f:
                    writer = csv.DictWriter(f, fieldnames=merged_fieldnames)
                    writer.writeheader()
                    writer.writerows(existing_rows)
                os.replace(temp_keyfile, self.keyfile_csv)
            fieldnames = merged_fieldnames

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

    def wait_for_download_mode(self):
        """Prompt once before any optional erase and the final all-in-one flash."""
        from tqdm import tqdm
        print("\nPreparing to flash... Please put ESP32 into download mode!")
        for _ in tqdm(range(10), desc="Waiting"):
            time.sleep(0.5)

    def get_partition_region(self, partition_name: str) -> Tuple[int, int]:
        """Read a partition offset and size from the project's active partition CSV."""
        with open(self.partition_table_csv, 'r', newline='') as file:
            for row in csv.reader(file):
                if not row or row[0].strip().startswith('#'):
                    continue
                if row[0].strip() == partition_name:
                    if len(row) < 5:
                        raise ValueError(f"Malformed partition row for {partition_name}")
                    return int(row[3].strip(), 0), int(row[4].strip(), 0)
        raise ValueError(
            f"Partition '{partition_name}' not found in {self.partition_table_csv}"
        )

    def erase_config_partition(self):
        """Erase SPIFFS so the next boot creates a config from the compiled defaults."""
        offset, size = self.get_partition_region('spiffs')
        erase_command = [
            sys.executable,
            '-m', 'esptool',
            '--chip', 'esp32',
            '--port', self.port,
            '--baud', str(self.baud_rate),
            '--after', 'no_reset',
            'erase_region', hex(offset), hex(size)
        ]

        print(f"\nFactory reset requested: erasing SPIFFS at {hex(offset)} ({hex(size)} bytes)...")
        process = subprocess.Popen(
            erase_command,
            stdout=sys.stdout,
            stderr=sys.stderr,
            text=True
        )
        return_code = process.wait()
        if return_code != 0:
            raise RuntimeError(f"Config partition erase failed with return code: {return_code}")
        print("Configuration partition erased; compiled defaults will be created on first boot.")

    def flash_all_in_one(self):
        """
        Flash bootloader, partition table, NVS, and firmware in one esptool command.
        The caller has already prompted for download mode, allowing an optional SPIFFS erase first.
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
            serial_num, device_name, hw_gen, hw_rev, devEUI, appKey, nwkKey = \
                self.increment_serial_number()

            # Build the project
            self.build_project()

            # Only one manual download-mode entry is required, including an optional config reset.
            self.wait_for_download_mode()
            if self.factory_reset_config:
                self.erase_config_partition()

            # Flash all binaries at once.
            self.flash_all_in_one()

            # Only successful flashes are recorded as provisioned devices. A logging problem must
            # not misreport the already-completed device flash as a flash failure.
            try:
                self.log_keys_to_keyfile(
                    serial_num, device_name, hw_gen, hw_rev, devEUI, appKey, nwkKey
                )
            except Exception as log_error:
                print(f"\nWARNING: Flash succeeded, but keyfile logging failed: {log_error}")

            # Print summary
            print("\n=== Flash Complete ===")
            print(f"Serial Number:     {serial_num}")
            print(f"Device Name:       {device_name}")
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
    parser.add_argument(
        "--factory-reset-config",
        action="store_true",
        help="Erase SPIFFS before flashing so the requested compiled defaults replace stored config"
    )

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
