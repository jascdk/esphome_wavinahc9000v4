# Wavin AHC-9000 ESPHome Component

This is an ESPHome custom component for the Wavin AHC-9000 underfloor heating controller (also known as Wavin Sentio). This component allows you to integrate your Wavin heating system with ESPHome and Home Assistant.

## Features

- **Climate Control**: Full climate entity support for up to 16 heating zones
- **Standby Mode**: Bi-directional OFF mode that activates the thermostat's standby feature
- **Group Climate**: Create virtual thermostats that control multiple zones together
- **Hysteresis Control**: Adjustable temperature deadband (0.1-1.0°C) for each climate entity with persistence across restarts
- **Temperature Sensors**: Room temperature, floor temperature sensors
- **Average Temperature Sensors**: Calculate average temperature across multiple zones
- **Floor Temperature Limits**: Read floor temperature min/max limits
- **Battery Monitoring**: Monitor battery levels for wireless thermostats
- **Child Lock**: Control and monitor child lock status per zone
- **Group Child Lock**: Create master switches that control child locks across multiple zones
- **Device Information**: Read controller hardware/software version, address, and device name
- **Clock Synchronization**: Sync the controller's internal clock with Home Assistant time

## Hardware Requirements

The Wavin AHC-9000 controller uses RS485 communication. You'll need:
- An ESP32 or ESP8266 board
- An RS485 to TTL converter (e.g., MAX485, MAX3485)
- Optional: GPIO pins for TX enable and flow control

### Typical Wiring

```
Wavin AHC-9000        RS485 Module        ESP32/ESP8266
--------------        ------------        -------------
A (RS485+)     <---->  A+
B (RS485-)     <---->  B-
                       RO (RX)     <---->  GPIO16 (RX pin)
                       DI (TX)     <---->  GPIO17 (TX pin)
                       DE          <---->  GPIO5 (optional TX enable)
                       RE          <---->  GPIO5 (optional flow control)
                       VCC         <---->  3.3V or 5V
                       GND         <---->  GND
```

**Note**: Some RS485 modules have DE and RE pins tied together. In that case, use one GPIO pin for both `tx_enable_pin` and `flow_control_pin`.

## Installation

Add this component to your ESPHome configuration using `external_components`:

```yaml
external_components:
  - source: github://jascdk/esphome_wavinahc9000v4
    components: [wavin_ahc9000]
```

## Basic Configuration

Here's a minimal configuration to get started:

```yaml
external_components:
  - source: github://jascdk/esphome_wavinahc9000v4
    components: [wavin_ahc9000]

uart:
  id: uart_bus
  tx_pin: GPIO17
  rx_pin: GPIO16
  baud_rate: 38400
  parity: EVEN

wavin_ahc9000:
  id: wavin_controller
  uart_id: uart_bus
  update_interval: 5s
  # Optional pins for RS485 control
  tx_enable_pin: GPIO5
  flow_control_pin: GPIO5

# Example climate entity for zone 1
climate:
  - platform: wavin_ahc9000
    wavin_ahc9000_id: wavin_controller
    name: "Living Room"
    channel: 1
```

## Configuration Variables

### Main Component (`wavin_ahc9000`)

- **uart_id** (*Required*): ID of the UART component
- **update_interval** (*Optional*, default: `5s`): How often to poll the controller
- **tx_enable_pin** (*Optional*): GPIO pin to enable RS485 transmission
- **flow_control_pin** (*Optional*): GPIO pin for RS485 flow control (DE/RE)
- **temp_divisor** (*Optional*, default: `10.0`): Temperature value divisor
- **receive_timeout_ms** (*Optional*, default: `1000`): Receive timeout in milliseconds
- **poll_channels_per_cycle** (*Optional*, default: `2`): Number of channels to poll per cycle
- **allow_mode_writes** (*Optional*, default: `true`): Allow climate mode changes
- **channel_XX_friendly_name** (*Optional*): Friendly name for channel XX (01-16)
- **time_id** (*Optional*): ID of the ESPHome time component to use for clock synchronization
- **sync_clock_on_connect** (*Optional*, default: `false`): Sync controller clock on connect
- **clock_sync_interval** (*Optional*): Periodic clock sync interval (e.g., `1h`, `24h`)

**Clock Synchronization**: The Wavin AHC-9000 controller has an internal clock that can be programmed via Modbus. You can synchronize this clock with Home Assistant's time by configuring a time component (e.g., `homeassistant` or `sntp`) and enabling clock sync. The clock is written atomically (all 7 registers at once) to prevent inconsistencies.

Example:
```yaml
time:
  - platform: homeassistant
    id: ha_time

wavin_ahc9000:
  id: wavin_controller
  uart_id: uart_bus
  time_id: ha_time
  sync_clock_on_connect: true
  clock_sync_interval: 24h
```

### Climate Entity (`climate` platform)

- **wavin_ahc9000_id** (*Required*): ID of the main wavin_ahc9000 component
- **channel** (*Optional*): Single channel number (1-16)
- **members** (*Optional*): List of channel numbers for group climate
- **strict_mode_writes** (*Optional*, default: `false`): Enable strict mode for writes
- **use_floor_temperature** (*Optional*, default: `false`): Use floor temperature for control

**Note**: Either `channel` (for single zone) or `members` (for group control) must be specified, but not both.

**Climate Modes - OFF/Standby Feature**: The climate entity supports two modes:
- **HEAT mode**: The thermostat operates in manual mode, maintaining the set target temperature
- **OFF mode**: The thermostat enters standby mode (energy-saving mode as shown on the physical thermostat)

This feature is **fully bi-directional**:
- Setting the climate to OFF in Home Assistant → Thermostat enters standby mode
- Setting standby on the physical thermostat → Climate shows as OFF in Home Assistant
- Setting the climate to HEAT in Home Assistant → Thermostat returns to manual mode with the previous target temperature
- Setting manual mode on the physical thermostat → Climate shows as HEAT in Home Assistant

The standby mode allows you to temporarily disable heating for a zone without losing the configured target temperature. When you switch back to HEAT mode, the thermostat automatically returns to the previously set temperature.

### Sensor Entity (`sensor` platform)

- **wavin_ahc9000_id** (*Required*): ID of the main wavin_ahc9000 component
- **channel** (*Optional*): Channel number (1-16) - required for single channel sensors
- **members** (*Optional*): List of channel numbers (1-16) - required for average temperature sensors
- **type** (*Required*): Sensor type, one of:
  - `battery`: Battery level percentage (requires `channel`)
  - `temperature`: Current room temperature (requires `channel`)
  - `floor_temperature`: Floor temperature (requires `channel`)
  - `floor_min_temperature`: Floor minimum temperature limit (read-only, requires `channel`)
  - `floor_max_temperature`: Floor maximum temperature limit (read-only, requires `channel`)
  - `humidity`: Room humidity percentage (requires `channel`)
  - `average_temperature`: Average temperature across multiple channels (requires `members`)

**Note**: Use `channel` for single channel sensors, or `members` for average temperature sensors. They are mutually exclusive.

**Average Temperature Sensor**: The `average_temperature` sensor type calculates the average temperature across multiple channels. Specify the channels to include using the `members` parameter as a list of channel numbers. Only channels with valid temperature readings are included in the average calculation.

Example:
```yaml
sensor:
  # Average temperature sensor
  - platform: wavin_ahc9000
    wavin_ahc9000_id: wavin_controller
    type: average_temperature
    members: [1, 2, 3, 4]  # Average of channels 1-4
    name: "House Average Temperature"
```

### Number Entity (`number` platform)

- **wavin_ahc9000_id** (*Required*): ID of the main wavin_ahc9000 component
- **climate_id** (*Optional*): ID of the climate entity to control
- **members** (*Optional*): List of channel numbers to control (alternative to climate_id)
- **type** (*Optional*, default: `hysteresis`): Number type (currently only `hysteresis`)

**Note**: Either `climate_id` OR `members` must be specified, but not both.

**Hysteresis Control**: The `hysteresis` number entity allows you to adjust the temperature deadband (hysteresis) for one or more channels. The hysteresis value determines how far the current temperature must deviate from the target temperature before the heating action changes between HEATING and IDLE states. This helps prevent rapid cycling of the heating system.

- **Range**: 0.1 to 2.0°C (clamped for safety)
- **Step**: 0.1°C
- **Default**: 0.3°C
- **Persistence**: Values are automatically saved to flash memory and restored after ESP32 restart
- **Scope**: Written to both ESPHome (for display) AND the physical thermostat (PACKED register 0x0E)
- **Group Propagation**: When changing hysteresis for a single channel that is a member of a group, the value automatically propagates to all sibling channels in the same group(s)

The hysteresis value works as follows:
- When `current_temperature > target_temperature + hysteresis`: Action is IDLE
- When `current_temperature < target_temperature - hysteresis`: Action is HEATING
- Within the deadband: Action remains unchanged (prevents oscillation)

**Important**: When you change the hysteresis value in Home Assistant, it is written to:
1. **ESPHome memory**: For calculating the displayed climate action (HEATING/IDLE)
2. **Physical thermostat**: Via Modbus to PACKED register 0x0E, affecting the actual relay control
3. **Sibling channels**: If the channel is a member of a group, the hysteresis is also written to all other members of that group

This ensures both the UI state and the physical heating behavior are synchronized, and all thermostats in a group maintain consistent behavior.

**Group Propagation Example**: If channels 3 and 4 are members of a "Bedrooms" group, changing the hysteresis for channel 3 will automatically update channel 4's hysteresis as well. This keeps all thermostats in the group synchronized. See [HYSTERESIS_GROUP_PROPAGATION.md](HYSTERESIS_GROUP_PROPAGATION.md) for detailed information.

**Configuration Options**:

You can configure hysteresis control in two ways:

1. **Via Climate Entity** (traditional approach):
```yaml
climate:
  - platform: wavin_ahc9000
    wavin_ahc9000_id: wavin_controller
    name: "Living Room"
    id: living_room_climate
    channel: 1

number:
  - platform: wavin_ahc9000
    wavin_ahc9000_id: wavin_controller
    climate_id: living_room_climate
    name: "Living Room Hysteresis"
    type: hysteresis
```

2. **Via Direct Members** (new approach):
```yaml
number:
  # Control hysteresis for multiple channels without a climate entity
  - platform: wavin_ahc9000
    wavin_ahc9000_id: wavin_controller
    members: [2, 3, 4, 5, 6, 7, 8, 9, 10, 11]
    name: "Master Hysteresis"
    type: hysteresis
  
  # Or for a smaller group
  - platform: wavin_ahc9000
    wavin_ahc9000_id: wavin_controller
    members: [3, 4]
    name: "Bedrooms Hysteresis"
    type: hysteresis
```

### Switch Entity (`switch` platform)

- **wavin_ahc9000_id** (*Required*): ID of the main wavin_ahc9000 component
- **channel** (*Optional*): Single channel number (1-16)
- **members** (*Optional*): List of channel numbers for group control
- **type** (*Optional*, default: `child_lock`): Switch type (currently only `child_lock`)

**Note**: Either `channel` (for single zone) or `members` (for group control) should be specified, but not both.

**Group Control**: The `members` parameter allows you to create a master switch that controls the child lock state for multiple channels simultaneously. When enabled, all member channels will have their child locks activated; when disabled, all will be deactivated.

Example:
```yaml
switch:
  # Single channel child lock
  - platform: wavin_ahc9000
    wavin_ahc9000_id: wavin_controller
    channel: 3
    type: child_lock
    name: "Zone 3 Lock"
  
  # Group child lock controlling multiple zones
  - platform: wavin_ahc9000
    wavin_ahc9000_id: wavin_controller
    type: child_lock
    members: [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11]
    name: "Master Lock"
```

### Text Sensor (`text_sensor` platform)

- **wavin_ahc9000_id** (*Required*): ID of the main wavin_ahc9000 component
- **type** (*Required*): Text sensor type, one of:
  - `control_unit_address`: Physical address of the control unit (read at startup)
  - `hw_version`: Hardware version (e.g., MC11012)
  - `sw_version`: Software version (e.g., MC61014 or MC61014b11 for beta)
  - `device_name`: Device name (e.g., AC-116)

**Device Info Sensors**: The control unit address, hardware version, software version, and device name are read once during startup from the Info category registers (0x09) as specified in the Modbus documentation. These provide identification information about the Wavin AHC-9000 controller.

## Clock Synchronization

The Wavin AHC-9000 controller has an internal real-time clock (RTC) that can be programmed via Modbus. This feature allows you to keep the controller's clock synchronized with Home Assistant's time, ensuring that any time-based schedules or logs on the controller are accurate.

### How It Works

The clock synchronization feature:
- Reads the current date and time from an ESPHome time component (e.g., `homeassistant` or `sntp`)
- Writes all 7 clock registers atomically (Year, Month, Day, Day of Week, Hour, Minute, Second)
- Supports automatic sync on startup and periodic sync at a configurable interval
- Validates the year range (2001-2099) as required by the controller specification

### Configuration

```yaml
time:
  - platform: homeassistant
    id: ha_time

wavin_ahc9000:
  id: wavin_controller
  uart_id: uart_bus
  time_id: ha_time                    # Reference to time component
  sync_clock_on_connect: true         # Sync when ESP connects (default: false)
  clock_sync_interval: 24h            # Periodic sync interval (optional)
```

### Clock Register Details

According to the Wavin Modbus specification, the clock is in category 0x05 (CLOCK) with 7 registers:

| Index | Register | Range | Description |
|-------|----------|-------|-------------|
| 0x00 | YEAR | 2001-2099 | Year value |
| 0x01 | MONTH | 1-12 | Month (1=Jan, 12=Dec) |
| 0x02 | DAY | 1-31 | Day of month |
| 0x03 | DAY_OF_WEEK | 0-6 | 0=Monday, 6=Sunday |
| 0x04 | HOUR | 0-23 | Hour (24-hour format) |
| 0x05 | MINUTE | 0-59 | Minute |
| 0x06 | SECOND | 0-59 | Second |

**Important**: All 7 registers must be written together in a single Modbus command to prevent inconsistencies between reads/writes, as specified in the controller documentation.

### Verification

After setting the clock, you can verify it was set correctly by checking the RTC VALID flag:
- Category: 0x00 (MAIN)
- Index: 0x08 (STATUS L)
- Bit 0: RTC VALID (should be 1 after successful clock set)

The component logs all clock sync operations at INFO level, showing the timestamp that was written to the controller.

## Full Example

See the [examples/full-example.yaml](examples/full-example.yaml) file for a comprehensive configuration with multiple zones, sensors, and controls.

## Troubleshooting

### No Communication

- Verify RS485 wiring (A and B may need to be swapped)
- Check baud rate (typically 38400 with EVEN parity)
- Ensure TX enable and flow control pins are correctly configured
- Check that the Wavin controller is powered and operational

### Missing Zones

- Increase `update_interval` to allow more time for zone detection
- Adjust `poll_channels_per_cycle` to scan more channels per update
- Verify that wireless thermostats have fresh batteries

### Incomplete Data

- Some zones may take time to report all values
- Floor temperature sensors may not be installed on all zones
- Battery levels are only available for wireless thermostats

## Credits

This component is based on the Wavin AHC-9000 Modbus protocol and community reverse engineering efforts.

## License

This project is provided as-is for personal and educational use.
