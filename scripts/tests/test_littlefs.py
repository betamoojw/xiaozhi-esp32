import csv
import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def parse_size(value):
    value = value.strip().upper()
    if value.startswith("0X"):
        return int(value, 16)
    units = {"K": 1024, "M": 1024 * 1024}
    if value[-1:] in units:
        return int(value[:-1]) * units[value[-1]]
    return int(value)


def read_partitions(relative_path):
    partitions = []
    next_offset = None
    with (ROOT / relative_path).open(encoding="utf-8", newline="") as source:
        rows = csv.reader(line for line in source if not line.lstrip().startswith("#"))
        for row in rows:
            if not row or not row[0].strip():
                continue
            name, partition_type, subtype, offset, size = (
                value.strip() for value in row[:5]
            )
            parsed_offset = parse_size(offset) if offset else next_offset
            parsed_size = parse_size(size)
            partitions.append(
                (name, partition_type, subtype, parsed_offset, parsed_size)
            )
            next_offset = (
                parsed_offset + parsed_size if parsed_offset is not None else None
            )
    return partitions


class LittleFsPartitionTests(unittest.TestCase):
    EXPECTED = {
        "partitions/v2/16m.csv": (0x800000, 0x600000, 0xE00000, 0x200000),
        "partitions/v2/16m_c3.csv": (0x800000, 4000 * 1024, 0xC00000, 0x200000),
        "partitions/v2/32m.csv": (0xA00000, 0x1000000, 0x1A00000, 0x200000),
    }

    def test_supported_layouts_keep_assets_and_add_non_overlapping_littlefs(self):
        for path, expected in self.EXPECTED.items():
            with self.subTest(path=path):
                partitions = read_partitions(path)
                assets = next(item for item in partitions if item[0] == "assets")
                littlefs = next(item for item in partitions if item[0] == "littlefs")
                self.assertEqual(assets[1:3], ("data", "spiffs"))
                self.assertEqual(littlefs[1:3], ("data", "littlefs"))
                self.assertEqual(
                    (assets[3], assets[4], littlefs[3], littlefs[4]), expected
                )
                ranges = sorted(
                    (item[3], item[3] + item[4])
                    for item in partitions
                    if item[3] is not None
                )
                self.assertTrue(
                    all(left[1] <= right[0] for left, right in zip(ranges, ranges[1:]))
                )


class LittleFsIntegrationTests(unittest.TestCase):
    def test_mount_uses_fixed_identifiers_without_autoformat(self):
        source = (ROOT / "main/storage/littlefs_storage.cc").read_text(
            encoding="utf-8"
        )
        self.assertIn('kPartitionLabel[] = "littlefs"', source)
        self.assertIn('kMountPoint[] = "/littlefs"', source)
        self.assertRegex(source, r"\.format_if_mount_failed\s*=\s*false")
        mount_position = source.index("esp_vfs_littlefs_register")
        format_position = source.index("esp_littlefs_format")
        self.assertGreater(format_position, mount_position)

    def test_mount_precedes_network_start(self):
        source = (ROOT / "main/application.cc").read_text(encoding="utf-8")
        self.assertLess(
            source.index("LittleFsStorage::GetInstance().Mount()"),
            source.index("board.StartNetwork()"),
        )

    def test_seed_image_is_separate_and_optional(self):
        cmake = (ROOT / "main/CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn('"--partition-name littlefs"', cmake)
        self.assertIn(
            'littlefs_create_partition_image(littlefs "${PROJECT_DIR}/littlefs" FLASH_IN_PROJECT)',
            cmake,
        )
        self.assertNotIn("main/assets", cmake[cmake.index("littlefs_create_partition_image"):])
        self.assertTrue((ROOT / "littlefs/interfaces").is_dir())

    def test_ftp_defaults_to_littlefs_and_uses_local_security_override(self):
        kconfig = (ROOT / "main/Kconfig.projbuild").read_text(encoding="utf-8")
        manifest = (ROOT / "main/idf_component.yml").read_text(encoding="utf-8")
        session = (
            ROOT / "third_party/ftp/include/ftp_client_session.hpp"
        ).read_text(encoding="utf-8")
        root_option = re.search(
            r"config XIAOZHI_FTP_SERVER_ROOT(?P<body>.*?)(?:\n\s*config|\nendmenu)",
            kconfig,
            re.DOTALL,
        )
        self.assertIsNotNone(root_option)
        self.assertIn('default "/littlefs"', root_option.group("body"))
        self.assertIn("override_path: ../third_party/ftp", manifest)
        self.assertIn("resolve_path", session)
        self.assertIn("lexically_normal", session)
        self.assertIn("root_directory_", session)
        self.assertNotRegex(
            session,
            r"std::filesystem::path full_path\s*=\s*current_directory_\s*/",
        )

    def test_knx_factory_runtime_and_staging_paths_are_distinct(self):
        header = (ROOT / "main/knx/knx_config.h").read_text(encoding="utf-8")
        self.assertIn('"interfaces/knxConfig.json"', header)
        self.assertIn('"/littlefs/interfaces/knxConfig.json"', header)
        self.assertIn('"/littlefs/interfaces/knxConfig.json.tmp"', header)


if __name__ == "__main__":
    unittest.main()