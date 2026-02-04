import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor
from esphome.const import (
    DEVICE_CLASS_CONNECTIVITY,
)

from . import WavinAHC9000

CONF_PARENT_ID = "wavin_ahc9000_id"
CONF_CHANNEL = "channel"
CONF_TYPE = "type"

CONFIG_SCHEMA = binary_sensor.binary_sensor_schema(
    device_class=DEVICE_CLASS_CONNECTIVITY
).extend(
    {
        cv.GenerateID(CONF_PARENT_ID): cv.use_id(WavinAHC9000),
        cv.Required(CONF_CHANNEL): cv.int_range(min=1, max=16),
        cv.Required(CONF_TYPE): cv.one_of(
            "thermostat_connected",
            lower=True,
        ),
    }
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_PARENT_ID])
    sens = await binary_sensor.new_binary_sensor(config)
    
    if config[CONF_TYPE] == "thermostat_connected":
        cg.add(hub.add_channel_thermostat_connected_binary_sensor(config[CONF_CHANNEL], sens))
        cg.add(hub.add_active_channel(config[CONF_CHANNEL]))
