import esphome.codegen as cg
from esphome.components import i2c
from esphome.components.ina219 import sensor as ina219_sensor
import esphome.config_validation as cv
import esphome.final_validate as fv
from esphome.const import CONF_ADDRESS, CONF_I2C_ID, CONF_ID

AUTO_LOAD = ["ina219"]
DEPENDENCIES = ["i2c"]

optional_ina219_ns = cg.esphome_ns.namespace("optional_ina219")
OptionalINA219Component = optional_ina219_ns.class_(
    "OptionalINA219Component", cg.PollingComponent, i2c.I2CDevice
)

CONFIG_SCHEMA = ina219_sensor.CONFIG_SCHEMA.extend(
    {cv.GenerateID(): cv.declare_id(OptionalINA219Component)}
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

# All native configuration setters are forwarded by the composing adapter.
to_code = ina219_sensor.to_code
