import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import number
from esphome.const import CONF_CHANNEL

from . import WavinAHC9000, WavinZoneClimate, ns

CONF_PARENT_ID = "wavin_ahc9000_id"
CONF_TYPE = "type"
CONF_MEMBERS = "members"
CONF_CLIMATE_ID = "climate_id"

# Hysteresis number entity
WavinHysteresisNumber = ns.class_("WavinHysteresisNumber", number.Number, cg.Component)

CONFIG_SCHEMA = cv.All(
    number.number_schema(WavinHysteresisNumber).extend(
        {
            cv.GenerateID(CONF_PARENT_ID): cv.use_id(WavinAHC9000),
            cv.Optional(CONF_TYPE, default="hysteresis"): cv.one_of("hysteresis", lower=True),
            cv.Required(CONF_CLIMATE_ID): cv.use_id(WavinZoneClimate),
        }
    ),
)

async def to_code(config):
    hub = await cg.get_variable(config[CONF_PARENT_ID])
    var = cg.new_Pvariable(config[cg.CONF_ID])
    await number.register_number(var, config, min_value=0.1, max_value=1.0, step=0.1)
    await cg.register_component(var, config)
    
    # Link to the climate entity this hysteresis number controls
    climate_var = await cg.get_variable(config[CONF_CLIMATE_ID])
    cg.add(var.set_climate(climate_var))
    cg.add(var.set_parent(hub))
