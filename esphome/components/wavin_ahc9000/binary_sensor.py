import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor
from esphome.const import CONF_CHANNEL

from . import WavinAHC9000

CONF_WAVIN_AHC9000_ID = "wavin_ahc9000_id"

CONFIG_SCHEMA = binary_sensor.binary_sensor_schema().extend({
    cv.GenerateID(CONF_WAVIN_AHC9000_ID): cv.use_id(WavinAHC9000),
    cv.Required(CONF_CHANNEL): cv.int_range(min=1, max=16),
})

async def to_code(config):
    hub = await cg.get_variable(config[CONF_WAVIN_AHC9000_ID])
    var = await binary_sensor.new_binary_sensor(config)
    cg.add(hub.add_channel_online_binary_sensor(config[CONF_CHANNEL], var))
    cg.add(hub.add_active_channel(config[CONF_CHANNEL]))