import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import text_sensor

from . import WavinAHC9000

CONF_PARENT_ID = "wavin_ahc9000_id"

CONFIG_SCHEMA = text_sensor.text_sensor_schema().extend(
    {cv.GenerateID(CONF_PARENT_ID): cv.use_id(WavinAHC9000)}
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_PARENT_ID])
    ts = await text_sensor.new_text_sensor(config)
    cg.add(hub.set_yaml_text_sensor(ts))
