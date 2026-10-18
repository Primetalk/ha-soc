#!/usr/bin/env python3
"""Check the generated ESP8266 preference allocation ABI before OTA.

Run after ESPHome source generation with the generated main.cpp path. ESPHome
2026.8.0 uses stable component setup ordering. Restored numbers/globals allocate
at HARDWARE (800), Wi-Fi at WIFI (250). Safe-mode RTC state is allocated before
App.setup(); this test covers the flash allocations in App.setup().
"""
import re
import sys
from pathlib import Path


def allocation_order(source):
    objects = []
    for match in re.finditer(
        r"new\((\w+)\) (globals::RestoringGlobalsComponent<[^;]+|"
        r"template_::TemplateNumber\(\)|wifi::WiFiComponent\(\))", source
    ):
        name, kind = match.groups()
        if kind.startswith("template_") and not re.search(
            rf"{name}->set_restore_value\(true\)", source
        ):
            continue
        priority = 250.0 if kind.startswith("wifi::") else 800.0
        override = re.search(rf"{name}->set_setup_priority\((-?[\d.]+)f?\)", source)
        if override:
            priority = float(override[1])
        objects.append((name, priority))
    return [name for name, _ in sorted(objects, key=lambda item: -item[1])]


def main():
    source = Path(sys.argv[1]).read_text()
    order = allocation_order(source)
    # Existing order is an OTA storage contract, not a freely reorderable list.
    legacy = [
        "rated_capacity_ah", "charge_efficiency_percent", "stop_charge_level",
        "stop_charge_hysteresis", "capacity_warning_level", "capacity_warning_hysteresis",
        "stop_load_level", "stop_load_hysteresis", "soc_checkpoint",
        "soc_rule_latches", "soc_event_journal", "wifi_wificomponent_id",
    ]
    assert order == legacy + ["energy_checkpoint"], (
        "ESP8266 preference layout changed: new allocations must follow all legacy "
        f"allocations for upgrade/rollback compatibility. Actual order: {order}"
    )
    energy_priority = re.search(
        r"energy_checkpoint->set_setup_priority\((-?[\d.]+)f?\)", source
    )
    assert energy_priority and float(energy_priority[1]) > -100, (
        "Energy preference must load before the -100 energy restore callback"
    )
    shutdown_priorities = [
        float(match[1]) for match in re.finditer(
            r"new\(\w+\) ShutdownTrigger\((-?[\d.]+)f?\);"
            r"(.*?->add_actions\([^\n]+\);)", source, re.S
        ) if "(checkpoint_energy_totals)" in match[2]
    ]
    assert len(shutdown_priorities) == 1 and shutdown_priorities[0] < float(energy_priority[1]), (
        "Reverse shutdown order must stage energy before the restoring global saves it"
    )
    # These schemas and name hashes determine legacy record lengths/checksums.
    for name, words, name_hash in (
        ("soc_checkpoint", 5, 4242466115),
        ("soc_rule_latches", 3, 3433295804),
        ("soc_event_journal", 9, 690000866),
    ):
        assert re.search(
            rf"new\({name}\) globals::RestoringGlobalsComponent<uint32_t\[{words}\]>", source
        ), f"Legacy record size changed: {name}"
        assert re.search(
            rf"{name}->set_name_hash\({name_hash}(?:UL)?\)", source
        ), f"Legacy record checksum key changed: {name}"
    print("ESP8266 preference layout passed: legacy allocation prefix, sizes and keys preserved")


if __name__ == "__main__":
    main()
