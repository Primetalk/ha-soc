#!/usr/bin/env python3
"""Exercise include validation with temporary repositories, not source spelling."""

import tempfile
import unittest
from pathlib import Path

from validate_repository import local_yaml_sources, local_cpp_sources


class LocalSourceTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="battery-include-check-")
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name).resolve()

    def write(self, name, text):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)
        return path

    def test_nested_composition_resolves_relative_to_including_file(self):
        self.write("node.yaml", "packages:\n  shared: !include packages/shared.yaml\n")
        self.write("packages/shared.yaml", "packages:\n  measurement: !include measurement.yaml\n")
        self.write("packages/measurement.yaml", "sensor: []\n")
        sources = local_yaml_sources("node.yaml", self.root)
        self.assertEqual(set(sources), {
            self.root / "node.yaml", self.root / "packages/shared.yaml",
            self.root / "packages/measurement.yaml",
        })

    def test_missing_package_produces_an_actionable_failure(self):
        self.write("node.yaml", "packages:\n  absent: !include missing.yaml\n")
        with self.assertRaisesRegex(ValueError, "missing.yaml"):
            local_yaml_sources("node.yaml", self.root)

    def test_include_cycles_fail_without_recursing_forever(self):
        self.write("node.yaml", "packages:\n  shared: !include shared.yaml\n")
        self.write("shared.yaml", "packages:\n  node: !include node.yaml\n")
        with self.assertRaisesRegex(ValueError, "cycle"):
            local_yaml_sources("node.yaml", self.root)

    def test_reused_package_is_not_an_include_cycle(self):
        self.write("node.yaml", "packages:\n  a: !include common.yaml\n  b: !include common.yaml\n")
        self.write("common.yaml", "sensor: []\n")
        self.assertEqual(len(local_yaml_sources("node.yaml", self.root)), 2)

    def test_includes_cannot_escape_the_repository(self):
        self.write("node.yaml", "packages:\n  outside: !include ../outside.yaml\n")
        with self.assertRaisesRegex(ValueError, "escapes"):
            local_yaml_sources("node.yaml", self.root)

    def test_header_directory_and_sibling_dependencies_are_checked(self):
        self.write("node.yaml", "esphome:\n  includes:\n    - include\n")
        self.write("include/umbrella.h", '#include "math.h"\n')
        self.write("include/math.h", "#pragma once\n")
        sources = local_yaml_sources("node.yaml", self.root)
        self.assertEqual(set(local_cpp_sources(sources, self.root)), {
            self.root / "include/umbrella.h", self.root / "include/math.h",
        })
        (self.root / "include/math.h").unlink()
        with self.assertRaisesRegex(ValueError, "math.h"):
            local_cpp_sources(sources, self.root)

    def test_individual_header_includes_remain_supported(self):
        self.write("node.yaml", "esphome:\n  includes:\n    - include/math.h\n")
        self.write("include/math.h", "#pragma once\n")
        self.assertEqual(len(local_cpp_sources(local_yaml_sources("node.yaml", self.root), self.root)), 1)


if __name__ == "__main__":
    unittest.main()
