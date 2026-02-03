import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor

from . import WavinAHC9000

CONF_PARENT_ID = "wavin_ahc9000_id"
CONF_TYPE = "type"

CONFIG_SCHEMA = binary_sensor.binary_sensor_schema().extend(
    {
        cv.GenerateID(CONF_PARENT_ID): cv.use_id(WavinAHC9000),
        cv.Required(CONF_TYPE): cv.one_of("yaml_ready", lower=True),
    }
)

async def to_code(config):
    hub = await cg.get_variable(config[CONF_PARENT_ID])
    bs = await binary_sensor.new_binary_sensor(config)
    if config[CONF_TYPE] == "yaml_ready":
        cg.add(hub.set_yaml_ready_binary_sensor(bs))
