import esphome.codegen as cg
from esphome.components import i2c
from esphome.components.ina219 import sensor as ina219_sensor
import esphome.config_validation as cv
import esphome.final_validate as fv
from esphome.const import (
    CONF_ADDRESS,
    CONF_BUS_VOLTAGE,
    CONF_I2C_ID,
    CONF_ID,
    CONF_POWER,
    CONF_SHUNT_VOLTAGE,
)

AUTO_LOAD = ["ina219"]
DEPENDENCIES = ["i2c"]

optional_ina219_ns = cg.esphome_ns.namespace("optional_ina219")
OptionalINA219Component = optional_ina219_ns.class_(
    "OptionalINA219Component", cg.PollingComponent, i2c.I2CDevice
)

CONFIG_SCHEMA = ina219_sensor.CONFIG_SCHEMA.extend(
    {
        cv.GenerateID(): cv.declare_id(OptionalINA219Component),
        # A single native read must either publish current or fail. Multiple
        # outputs could publish partial data before a later read fails.
        cv.Optional(CONF_BUS_VOLTAGE): cv.invalid("optional_ina219 only supports current"),
        cv.Optional(CONF_SHUNT_VOLTAGE): cv.invalid("optional_ina219 only supports current"),
        cv.Optional(CONF_POWER): cv.invalid("optional_ina219 only supports current"),
    }
)


def _validate_unique_address(config):
    full_config = fv.full_config.get()
    # Sensors and the OLED share this project's bus. Compare resolved bus IDs
    # so an equal address on an independent bus remains valid.
    for domain in ("sensor", "display"):
        for device in full_config.get(domain, []):
            if (
                CONF_I2C_ID in device
                and str(device.get(CONF_ID)) != str(config[CONF_ID])
                and str(device[CONF_I2C_ID]) == str(config[CONF_I2C_ID])
                and device.get(CONF_ADDRESS) == config[CONF_ADDRESS]
            ):
                raise cv.Invalid(
                    f"Address 0x{config[CONF_ADDRESS]:02X} is already used on this I2C bus",
                    path=[CONF_ADDRESS],
                )
    return config


FINAL_VALIDATE_SCHEMA = _validate_unique_address

# Reuse native calibration configuration and current sensor generation.
to_code = ina219_sensor.to_code
