import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import switch
from esphome.const import CONF_CHANNEL

from . import WavinAHC9000, ns

CONF_PARENT_ID = "wavin_ahc9000_id"
CONF_MEMBERS = "members"

WavinChildLockSwitch = ns.class_("WavinChildLockSwitch", switch.Switch, cg.Component)

CONFIG_SCHEMA = cv.All(
    switch.switch_schema(WavinChildLockSwitch).extend({
        cv.GenerateID(CONF_PARENT_ID): cv.use_id(WavinAHC9000),
        cv.Optional(CONF_CHANNEL): cv.int_range(min=1, max=16),
        cv.Optional(CONF_MEMBERS): cv.ensure_list(cv.int_range(min=1, max=16)),
    }),
    cv.has_exactly_one_key(CONF_CHANNEL, CONF_MEMBERS),
)

async def to_code(config):
    hub = await cg.get_variable(config[CONF_PARENT_ID])
    
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