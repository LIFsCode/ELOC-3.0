import csv
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace


PROJECT_DIR = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT_DIR))

from AutoFlasher import AutoFlasher, format_device_name


def make_flasher(project_dir: str) -> AutoFlasher:
    return AutoFlasher(SimpleNamespace(
        project_dir=project_dir,
        port="COM1",
        env="esp32dev-ei",
        baud_rate=921600,
        factory_reset_config=False,
    ))


class AutoFlasherTests(unittest.TestCase):
    def test_device_name_uses_last_five_serial_digits(self):
        self.assertEqual("ELOC_00000", format_device_name("0"))
        self.assertEqual("ELOC_00123", format_device_name("123"))
        self.assertEqual("ELOC_00250", format_device_name("250700250"))
        self.assertEqual("ELOC_23456", format_device_name("123456"))

    def test_negative_serial_is_rejected(self):
        with self.assertRaises(ValueError):
            format_device_name("-1")

    def test_existing_keyfile_gets_device_name_column(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            keyfile = Path(temp_dir) / "keyfile.csv"
            keyfile.write_text(
                "Serial,devEUI,appKey,nwkKey,hw_gen,hw_rev,timestamp\n"
                "12,OLD_EUI,OLD_APP,OLD_NWK,3,6,2026-01-01 00:00:00\n",
                encoding="utf-8",
            )

            flasher = make_flasher(temp_dir)
            flasher.log_keys_to_keyfile(
                "123", "ELOC_00123", "3", "7",
                "NEW_EUI", "NEW_APP", "NEW_NWK",
            )

            with keyfile.open(newline="", encoding="utf-8") as file:
                rows = list(csv.DictReader(file))

            self.assertEqual("", rows[0]["DeviceName"])
            self.assertEqual("ELOC_00123", rows[1]["DeviceName"])
            self.assertEqual("NEW_EUI", rows[1]["devEUI"])

    def test_spiffs_region_comes_from_partition_table(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            partition_table = Path(temp_dir) / "elocPartitions.csv"
            partition_table.write_text(
                "# Name, Type, SubType, Offset, Size, Flags\n"
                "spiffs,data,spiffs,0xFD0000,0x30000,\n",
                encoding="utf-8",
            )

            flasher = make_flasher(temp_dir)
            self.assertEqual((0xFD0000, 0x30000), flasher.get_partition_region("spiffs"))

    def test_flash_command_resets_otadata(self):
        # Uses the project's real partition table: the offsets must be the ones the device uses.
        flasher = make_flasher(str(PROJECT_DIR))
        command = flasher.build_flash_command()
        pairs = command[command.index("write_flash") + 1:]
        images = dict(zip(pairs[0::2], (Path(p).name for p in pairs[1::2])))

        self.assertEqual({
            "0x1000": "bootloader.bin",
            "0x8000": "partitions.bin",
            "0x9000": "nvs.bin",
            "0xe000": "ota_data_initial.bin",
            "0x10000": "firmware.bin",
        }, images)
        self.assertIn(str(Path(".pio") / "build" / "esp32dev-ei"), pairs[7])

    def test_missing_ota_data_image_stops_before_flashing(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            Path(temp_dir, "elocPartitions.csv").write_text(
                (PROJECT_DIR / "elocPartitions.csv").read_text(encoding="utf-8"),
                encoding="utf-8",
            )
            flasher = make_flasher(temp_dir)
            build_dir = Path(temp_dir) / ".pio" / "build" / "esp32dev-ei"
            build_dir.mkdir(parents=True)
            for name in ("bootloader.bin", "partitions.bin", "nvs.bin", "firmware.bin"):
                (build_dir / name).write_bytes(b"\xff")

            with self.assertRaises(FileNotFoundError) as raised:
                flasher.flash_all_in_one()
            self.assertIn("ota_data_initial.bin", str(raised.exception))


if __name__ == "__main__":
    unittest.main()
