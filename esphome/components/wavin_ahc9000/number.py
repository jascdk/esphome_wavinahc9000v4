import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import number
from esphome.const import CONF_CHANNEL, CONF_ID

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
            cv.Optional(CONF_CLIMATE_ID): cv.use_id(WavinZoneClimate),
            cv.Optional(CONF_MEMBERS): cv.ensure_list(cv.int_range(min=1, max=16)),
        }
    ),
    cv.has_exactly_one_key(CONF_CLIMATE_ID, CONF_MEMBERS),
)

async def to_code(config):
    hub = await cg.get_variable(config[CONF_PARENT_ID])
    var = cg.new_Pvariable(config[CONF_ID])
    await number.register_number(var, config, min_value=0.1, max_value=1.0, step=0.1)
    await cg.register_component(var, config)
    
    cg.add(var.set_parent(hub))
    
    # Handle either climate_id (tied to climate entity) or members (direct channel list)
    if CONF_CLIMATE_ID in config:
        # Link to the climate entity this hysteresis number controls
        climate_var = await cg.get_variable(config[CONF_CLIMATE_ID])
        cg.add(var.set_climate(climate_var))
    
    if CONF_MEMBERS in config:
        # Direct member specification without climate entity
        cg.add(var.set_members(config[CONF_MEMBERS]))
        for ch in config[CONF_MEMBERS]:
            cg.add(hub.add_active_channel(ch))
