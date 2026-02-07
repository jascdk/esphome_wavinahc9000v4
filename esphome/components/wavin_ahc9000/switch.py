import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import switch
from esphome.const import CONF_CHANNEL

from . import WavinAHC9000, ns

CONF_PARENT_ID = "wavin_ahc9000_id"
CONF_MEMBERS = "members"
CONF_TYPE = "type"

WavinChildLockSwitch = ns.class_("WavinChildLockSwitch", switch.Switch, cg.Component)
WavinValveMaintenanceSwitch = ns.class_("WavinValveMaintenanceSwitch", switch.Switch, cg.Component)

def validate_switch_config(config):
    switch_type = config[CONF_TYPE]
    if switch_type == "valve_maintenance":
        if CONF_CHANNEL in config or CONF_MEMBERS in config:
            raise cv.Invalid("valve_maintenance switch cannot have 'channel' or 'members'")
    else:
        # child_lock
        if CONF_CHANNEL not in config and CONF_MEMBERS not in config:
            raise cv.Invalid("child_lock switch requires either 'channel' or 'members'")
        if CONF_CHANNEL in config and CONF_MEMBERS in config:
            raise cv.Invalid("child_lock switch cannot have both 'channel' and 'members'")
    return config

CONFIG_SCHEMA = cv.All(
    switch.switch_schema(WavinChildLockSwitch).extend({
        cv.GenerateID(CONF_PARENT_ID): cv.use_id(WavinAHC9000),
        cv.Optional(CONF_TYPE, default="child_lock"): cv.one_of("child_lock", "valve_maintenance", lower=True),
        cv.Optional(CONF_CHANNEL): cv.int_range(min=1, max=16),
        cv.Optional(CONF_MEMBERS): cv.ensure_list(cv.int_range(min=1, max=16)),
    }),
    validate_switch_config,
)

async def to_code(config):
    hub = await cg.get_variable(config[CONF_PARENT_ID])
    switch_type = config[CONF_TYPE]
    
    if switch_type == "valve_maintenance":
        rhs = WavinValveMaintenanceSwitch.new()
        var = cg.Pvariable(config[cv.CONF_ID], rhs, type_=WavinValveMaintenanceSwitch)
        await switch.register_switch(var, config)
        cg.add(var.set_parent(hub))
        cg.add(hub.add_valve_maintenance_switch(var))
        return
    
    var = cg.new_Pvariable(config[cv.CONF_ID])
    await switch.register_switch(var, config)
    cg.add(var.set_parent(hub))
    
    # Handle single channel or members (similar to climate)
    if CONF_CHANNEL in config:
        ch = config[CONF_CHANNEL]
        cg.add(var.set_channel(ch))
        cg.add(hub.add_channel_child_lock_switch(ch, var))
    
    if CONF_MEMBERS in config:
        cg.add(var.set_members(config[CONF_MEMBERS]))
        for ch in config[CONF_MEMBERS]:
            cg.add(hub.add_active_channel(ch))