import csv
import json
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
        source = (ROOT / "main/application.cc").read_text(encoding="utf-8")
        self.assertIn('kLittleFsPartitionLabel[] = "littlefs"', source)
        self.assertIn('kLittleFsMountPoint[] = "/littlefs"', source)
        self.assertRegex(source, r"\.format_if_mount_failed\s*=\s*false")
        self.assertNotIn("esp_littlefs_format", source)

    def test_mount_precedes_network_start(self):
        source = (ROOT / "main/application.cc").read_text(encoding="utf-8")
        self.assertLess(
            source.index("MountLittleFs()"),
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

    def test_ftp_defaults_to_littlefs_and_uses_managed_component(self):
        kconfig = (ROOT / "main/Kconfig.projbuild").read_text(encoding="utf-8")
        manifest = (ROOT / "main/idf_component.yml").read_text(encoding="utf-8")
        root_option = re.search(
            r"config XIAOZHI_FTP_SERVER_ROOT(?P<body>.*?)(?:\n\s*config|\nendmenu)",
            kconfig,
            re.DOTALL,
        )
        self.assertIsNotNone(root_option)
        self.assertIn('default "/littlefs"', root_option.group("body"))
        self.assertRegex(manifest, r"(?m)^\s*espp/ftp:\s*\^1\.3\.1\s*$")
        self.assertNotIn("override_path: ../third_party/ftp", manifest)

    def test_knx_runtime_and_staging_paths_use_littlefs(self):
        header = (ROOT / "main/knx/knx_config.h").read_text(encoding="utf-8")
        self.assertIn('"/littlefs/interfaces/knxConfig.json"', header)
        self.assertIn('"/littlefs/interfaces/knxConfig.json.tmp"', header)
        self.assertNotIn("kKnxFactoryConfigurationAsset", header)

    def test_knx_configuration_is_not_packaged_as_an_asset(self):
        cmake = (ROOT / "main/CMakeLists.txt").read_text(encoding="utf-8")
        self.assertNotIn("KNX_FACTORY_CONFIGURATION", cmake)
        self.assertNotIn("/littlefs/interfaces/knxConfig.json", cmake)
        self.assertFalse((ROOT / "main//littlefs/interfaces/knxConfig.json").exists())

    def test_knx_loads_littlefs_before_legacy_nvs(self):
        manager = (ROOT / "main/knx/knx_manager.cc").read_text(encoding="utf-8")
        self.assertLess(
            manager.index("KnxLoadRuntimeConfiguration("),
            manager.index("KnxLoadLegacyConfiguration("),
        )
        self.assertIn("if (runtime_configuration_found)", manager)
        self.assertNotIn("KnxPersistConfiguration", manager)

    def test_knx_seed_fits_runtime_limits(self):
        seed = ROOT / "littlefs/interfaces/knxConfig.json"
        configuration = json.loads(seed.read_text(encoding="utf-8"))
        header = (ROOT / "main/knx/knx_config.h").read_text(encoding="utf-8")
        kconfig = (ROOT / "main/Kconfig.projbuild").read_text(encoding="utf-8")
        maximum_length = int(
            re.search(r"kKnxMaximumConfigurationLength = (\d+)", header).group(1)
        )
        maximum_objects = int(
            re.search(
                r"config XIAOZHI_KNX_IP_MAX_OBJECTS.*?default (\d+)",
                kconfig,
                re.DOTALL,
            ).group(1)
        )
        self.assertLessEqual(seed.stat().st_size, maximum_length)
        self.assertLessEqual(len(configuration), maximum_objects)


if __name__ == "__main__":
    unittest.main()