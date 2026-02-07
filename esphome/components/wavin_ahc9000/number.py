import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import number
from esphome.const import CONF_ID

from . import WavinAHC9000, WavinZoneClimate, ns

CONF_PARENT_ID = "wavin_ahc9000_id"
CONF_TYPE = "type"
CONF_MEMBERS = "members"
CONF_CLIMATE_ID = "climate_id"

# Number entity classes
WavinHysteresisNumber = ns.class_("WavinHysteresisNumber", number.Number, cg.Component)
WavinTempLowNumber = ns.class_("WavinTempLowNumber", number.Number, cg.Component)
WavinTempHighNumber = ns.class_("WavinTempHighNumber", number.Number, cg.Component)
WavinFloorMinNumber = ns.class_("WavinFloorMinNumber", number.Number, cg.Component)
WavinFloorMaxNumber = ns.class_("WavinFloorMaxNumber", number.Number, cg.Component)

def validate_number_config(config):
    """Validate number configuration based on type."""
    num_type = config[CONF_TYPE]
    has_climate_id = CONF_CLIMATE_ID in config
    has_members = CONF_MEMBERS in config
    
    if num_type == "hysteresis":
        # hysteresis requires exactly one of climate_id or members
        if not (has_climate_id or has_members):
            raise cv.Invalid("hysteresis requires either 'climate_id' or 'members'")
        if has_climate_id and has_members:
            raise cv.Invalid("hysteresis cannot have both 'climate_id' and 'members'")
    elif num_type in ["temp_low", "temp_high", "floor_min", "floor_max"]:
        # these types require members only
        if not has_members:
            raise cv.Invalid(f"{num_type} requires 'members'")
        if has_climate_id:
            raise cv.Invalid(f"{num_type} cannot have 'climate_id', use 'members' instead")
    
    return config

# Use hysteresis as the base class for schema, but we'll override in to_code
CONFIG_SCHEMA = cv.All(
    number.number_schema(WavinHysteresisNumber).extend(
        {
            cv.GenerateID(CONF_PARENT_ID): cv.use_id(WavinAHC9000),
            cv.Optional(CONF_TYPE, default="hysteresis"): cv.one_of(
                "hysteresis",
                "temp_low",
                "temp_high",
                "floor_min",
                "floor_max",
                lower=True,
            ),
            cv.Optional(CONF_CLIMATE_ID): cv.use_id(WavinZoneClimate),
            cv.Optional(CONF_MEMBERS): cv.ensure_list(cv.int_range(min=1, max=16)),
        }
    ),
    validate_number_config,
)

async def to_code(config):
    hub = await cg.get_variable(config[CONF_PARENT_ID])
    num_type = config[CONF_TYPE]
    
    # Create the appropriate number entity based on type
    if num_type == "hysteresis":
        var = cg.new_Pvariable(config[CONF_ID])
        await number.register_number(var, config, min_value=0.1, max_value=2.0, step=0.1)
    elif num_type == "temp_low":
        # Create with correct type using MockObjClass instance
        rhs = WavinTempLowNumber.new()
        var = cg.Pvariable(config[CONF_ID], rhs, type_=WavinTempLowNumber)
        await number.register_number(var, config, min_value=6.0, max_value=40.0, step=0.5)
    elif num_type == "temp_high":
        # Create with correct type using MockObjClass instance
        rhs = WavinTempHighNumber.new()
        var = cg.Pvariable(config[CONF_ID], rhs, type_=WavinTempHighNumber)
        await number.register_number(var, config, min_value=6.0, max_value=40.0, step=0.5)
    elif num_type == "floor_min":
        rhs = WavinFloorMinNumber.new()
        var = cg.Pvariable(config[CONF_ID], rhs, type_=WavinFloorMinNumber)
        await number.register_number(var, config, min_value=5.0, max_value=35.0, step=0.5)
    elif num_type == "floor_max":
        rhs = WavinFloorMaxNumber.new()
        var = cg.Pvariable(config[CONF_ID], rhs, type_=WavinFloorMaxNumber)
        await number.register_number(var, config, min_value=5.0, max_value=35.0, step=0.5)
    
    await cg.register_component(var, config)
    cg.add(var.set_parent(hub))
    
    # Handle either climate_id (tied to climate entity) or members (direct channel list)
    if CONF_CLIMATE_ID in config:
        # Link to the climate entity (only for hysteresis)
        climate_var = await cg.get_variable(config[CONF_CLIMATE_ID])
        cg.add(var.set_climate(climate_var))
    
    if CONF_MEMBERS in config:
        # Direct member specification
        cg.add(var.set_members(config[CONF_MEMBERS]))
        for ch in config[CONF_MEMBERS]:
            cg.add(hub.add_active_channel(ch))
