import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import text_sensor

from . import WavinAHC9000

CONF_PARENT_ID = "wavin_ahc9000_id"
CONF_TYPE = "type"

# Icons for different device info types
ICON_ADDRESS = "mdi:identifier"
ICON_HARDWARE = "mdi:chip"
ICON_SOFTWARE = "mdi:application"
ICON_DEVICE = "mdi:devices"

CONFIG_SCHEMA = text_sensor.text_sensor_schema().extend(
    {
        cv.GenerateID(CONF_PARENT_ID): cv.use_id(WavinAHC9000),
        cv.Required(CONF_TYPE): cv.one_of(
            "control_unit_address",
            "hw_version",
            "sw_version",
            "device_name",
            lower=True,
        ),
    }
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_PARENT_ID])
    sens = await text_sensor.new_text_sensor(config)

    # Apply defaults based on sensor type
    sensor_type = config[CONF_TYPE]

    if sensor_type == "control_unit_address":
        cg.add(sens.set_icon(ICON_ADDRESS))
        cg.add(hub.add_control_unit_address_text_sensor(sens))
    elif sensor_type == "hw_version":
        cg.add(sens.set_icon(ICON_HARDWARE))
        cg.add(hub.add_hw_version_text_sensor(sens))
    elif sensor_type == "sw_version":
        cg.add(sens.set_icon(ICON_SOFTWARE))
        cg.add(hub.add_sw_version_text_sensor(sens))
    elif sensor_type == "device_name":
        cg.add(sens.set_icon(ICON_DEVICE))
        cg.add(hub.add_device_name_text_sensor(sens))