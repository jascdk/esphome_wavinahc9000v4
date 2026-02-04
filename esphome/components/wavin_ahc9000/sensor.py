import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import (
    UNIT_PERCENT,
    DEVICE_CLASS_BATTERY,
    ICON_BATTERY,
    DEVICE_CLASS_TEMPERATURE,
    UNIT_CELSIUS,
)

from . import WavinAHC9000

CONF_PARENT_ID = "wavin_ahc9000_id"
CONF_CHANNEL = "channel"
CONF_TYPE = "type"
CONF_MEMBERS = "members"


def validate_sensor_config(config):
    """Validate sensor configuration: channel XOR members based on type."""
    sensor_type = config[CONF_TYPE]
    has_channel = CONF_CHANNEL in config
    has_members = CONF_MEMBERS in config
    
    if sensor_type == "average_temperature":
        # average_temperature requires members, not channel
        if has_channel:
            raise cv.Invalid("average_temperature sensor cannot have 'channel', use 'members' instead")
        if not has_members:
            raise cv.Invalid("average_temperature sensor requires 'members' list")
    elif sensor_type == "cpu_temperature":
        # cpu_temperature is a global sensor, no channel or members needed
        if has_channel:
            raise cv.Invalid("cpu_temperature sensor cannot have 'channel'")
        if has_members:
            raise cv.Invalid("cpu_temperature sensor cannot have 'members'")
    else:
        # Other sensor types require channel, not members
        if has_members:
            raise cv.Invalid(f"{sensor_type} sensor cannot have 'members', use 'channel' instead")
        if not has_channel:
            raise cv.Invalid(f"{sensor_type} sensor requires 'channel'")
    
    return config


CONFIG_SCHEMA = cv.All(
    sensor.sensor_schema().extend(
        {
            cv.GenerateID(CONF_PARENT_ID): cv.use_id(WavinAHC9000),
            cv.Optional(CONF_CHANNEL): cv.int_range(min=1, max=16),
            cv.Optional(CONF_MEMBERS): cv.ensure_list(cv.int_range(min=1, max=16)),
            cv.Required(CONF_TYPE): cv.one_of(
                "battery",
                "temperature",
                "floor_temperature",
                "floor_min_temperature",
                "floor_max_temperature",
                "average_temperature",
                "cpu_temperature",
                lower=True,
            ),
        }
    ),
    validate_sensor_config,
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_PARENT_ID])
    sens = await sensor.new_sensor(config)
    # Apply defaults based on sensor type
    if config[CONF_TYPE] == "battery":
        cg.add(sens.set_device_class(DEVICE_CLASS_BATTERY))
        cg.add(sens.set_unit_of_measurement(UNIT_PERCENT))
        cg.add(sens.set_icon(ICON_BATTERY))
        cg.add(sens.set_accuracy_decimals(0))
        cg.add(hub.add_channel_battery_sensor(config[CONF_CHANNEL], sens))
        cg.add(hub.add_active_channel(config[CONF_CHANNEL]))
    elif config[CONF_TYPE] == "cpu_temperature":
        # CPU temperature sensor (controller internal diagnostic)
        cg.add(sens.set_device_class(DEVICE_CLASS_TEMPERATURE))
        cg.add(sens.set_unit_of_measurement(UNIT_CELSIUS))
        cg.add(sens.set_accuracy_decimals(1))
        cg.add(hub.add_cpu_temperature_sensor(sens))
    elif config[CONF_TYPE] == "average_temperature":
        # average_temperature sensor with members list
        cg.add(sens.set_device_class(DEVICE_CLASS_TEMPERATURE))
        cg.add(sens.set_unit_of_measurement(UNIT_CELSIUS))
        cg.add(sens.set_accuracy_decimals(1))
        # Pass the members list to the hub
        members = config[CONF_MEMBERS]
        cg.add(hub.add_average_temperature_sensor(sens, members))
        # Mark all member channels as active
        for ch in members:
            cg.add(hub.add_active_channel(ch))
    else:
        # temperature and floor sensors share temperature meta
        cg.add(sens.set_device_class(DEVICE_CLASS_TEMPERATURE))
        cg.add(sens.set_unit_of_measurement(UNIT_CELSIUS))
        cg.add(sens.set_accuracy_decimals(1))
        if config[CONF_TYPE] == "floor_temperature":
            cg.add(hub.add_channel_floor_temperature_sensor(config[CONF_CHANNEL], sens))
        elif config[CONF_TYPE] == "floor_min_temperature":
            cg.add(hub.add_channel_floor_min_temperature_sensor(config[CONF_CHANNEL], sens))
        elif config[CONF_TYPE] == "floor_max_temperature":
            cg.add(hub.add_channel_floor_max_temperature_sensor(config[CONF_CHANNEL], sens))
        else:
            cg.add(hub.add_channel_temperature_sensor(config[CONF_CHANNEL], sens))
        cg.add(hub.add_active_channel(config[CONF_CHANNEL]))