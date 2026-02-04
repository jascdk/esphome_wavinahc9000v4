import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import number
from esphome.const import CONF_CHANNEL, CONF_ID

from . import WavinAHC9000, WavinZoneClimate, ns

CONF_PARENT_ID = "wavin_ahc9000_id"
CONF_TYPE = "type"
CONF_MEMBERS = "members"
CONF_CLIMATE_ID = "climate_id"

# Number entity classes
WavinHysteresisNumber = ns.class_("WavinHysteresisNumber", number.Number, cg.Component)
WavinTempLowNumber = ns.class_("WavinTempLowNumber", number.Number, cg.Component)
WavinTempHighNumber = ns.class_("WavinTempHighNumber", number.Number, cg.Component)

def validate_config(config):
    """Validate that members or climate_id is provided based on type."""
    num_type = config.get(CONF_TYPE, "hysteresis")
    has_climate_id = CONF_CLIMATE_ID in config
    has_members = CONF_MEMBERS in config
    
    # For hysteresis, require exactly one of climate_id or members
    if num_type == "hysteresis":
        if not (has_climate_id or has_members):
            raise cv.Invalid("Either 'climate_id' or 'members' must be specified for hysteresis")
        if has_climate_id and has_members:
            raise cv.Invalid("Only one of 'climate_id' or 'members' can be specified for hysteresis")
    # For temp_low and temp_high, members is required
    elif num_type in ["temp_low", "temp_high"]:
        if not has_members:
            raise cv.Invalid(f"'members' must be specified for {num_type}")
        if has_climate_id:
            raise cv.Invalid(f"'climate_id' is not supported for {num_type}, use 'members' instead")
    
    return config

CONFIG_SCHEMA = cv.All(
    cv.typed_schema({
        "hysteresis": number.number_schema(WavinHysteresisNumber).extend(
            {
                cv.GenerateID(CONF_PARENT_ID): cv.use_id(WavinAHC9000),
                cv.Required(CONF_TYPE): cv.one_of("hysteresis", lower=True),
                cv.Optional(CONF_CLIMATE_ID): cv.use_id(WavinZoneClimate),
                cv.Optional(CONF_MEMBERS): cv.ensure_list(cv.int_range(min=1, max=16)),
            }
        ),
        "temp_low": number.number_schema(WavinTempLowNumber).extend(
            {
                cv.GenerateID(CONF_PARENT_ID): cv.use_id(WavinAHC9000),
                cv.Required(CONF_TYPE): cv.one_of("temp_low", lower=True),
                cv.Required(CONF_MEMBERS): cv.ensure_list(cv.int_range(min=1, max=16)),
            }
        ),
        "temp_high": number.number_schema(WavinTempHighNumber).extend(
            {
                cv.GenerateID(CONF_PARENT_ID): cv.use_id(WavinAHC9000),
                cv.Required(CONF_TYPE): cv.one_of("temp_high", lower=True),
                cv.Required(CONF_MEMBERS): cv.ensure_list(cv.int_range(min=1, max=16)),
            }
        ),
    }, key=CONF_TYPE, default_type="hysteresis"),
    validate_config,
)

async def to_code(config):
    hub = await cg.get_variable(config[CONF_PARENT_ID])
    var = cg.new_Pvariable(config[CONF_ID])
    
    num_type = config[CONF_TYPE]
    
    # Register with appropriate min/max/step based on type
    if num_type == "hysteresis":
        await number.register_number(var, config, min_value=0.1, max_value=2.0, step=0.1)
    elif num_type == "temp_low":
        await number.register_number(var, config, min_value=6.0, max_value=40.0, step=0.5)
    elif num_type == "temp_high":
        await number.register_number(var, config, min_value=6.0, max_value=40.0, step=0.5)
    
    await cg.register_component(var, config)
    cg.add(var.set_parent(hub))
    
    # Handle either climate_id (tied to climate entity) or members (direct channel list)
    if CONF_CLIMATE_ID in config:
        # Link to the climate entity (only for hysteresis)
        climate_var = await cg.get_variable(config[CONF_CLIMATE_ID])
        cg.add(var.set_climate(climate_var))
    
    if CONF_MEMBERS in config:
        # Direct member specification without climate entity
        cg.add(var.set_members(config[CONF_MEMBERS]))
        for ch in config[CONF_MEMBERS]:
            cg.add(hub.add_active_channel(ch))
