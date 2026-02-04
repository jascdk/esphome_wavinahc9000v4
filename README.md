# Wavin AHC-9000 ESPHome Component

This is an ESPHome custom component for the Wavin AHC-9000 underfloor heating controller (also known as Wavin Sentio). This component allows you to integrate your Wavin heating system with ESPHome and Home Assistant.

## Features

- **Climate Control**: Full climate entity support for up to 16 heating zones
- **Group Climate**: Create virtual thermostats that control multiple zones together
- **Temperature Sensors**: Room temperature, floor temperature sensors
- **Average Temperature Sensors**: Calculate average temperature across multiple zones
- **Floor Temperature Limits**: Read floor temperature min/max limits
- **Battery Monitoring**: Monitor battery levels for wireless thermostats
- **Child Lock**: Control and monitor child lock status per zone
- **Group Child Lock**: Create master switches that control child locks across multiple zones
- **Device Information**: Read controller hardware/software version, address, and device name

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

### Climate Entity (`climate` platform)

- **wavin_ahc9000_id** (*Required*): ID of the main wavin_ahc9000 component
- **channel** (*Optional*): Single channel number (1-16)
- **members** (*Optional*): List of channel numbers for group climate
- **strict_mode_writes** (*Optional*, default: `false`): Enable strict mode for writes
- **use_floor_temperature** (*Optional*, default: `false`): Use floor temperature for control

**Note**: Either `channel` (for single zone) or `members` (for group control) must be specified, but not both.

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
  - `average_temperature`: Average temperature across multiple channels (requires `members`)

**Note**: Use `channel` for single channel sensors, or `members` for average temperature sensors. They are mutually exclusive.

**Average Temperature Sensor**: The `average_temperature` sensor type calculates the average temperature across multiple channels. Specify the channels to include using the `members` parameter as a list of channel numbers. Only channels with valid temperature readings are included in the average calculation.

Example:
```yaml
sensor:
  - platform: wavin_ahc9000
    wavin_ahc9000_id: wavin_controller
    type: average_temperature
    members: [1, 2, 3, 4]  # Average of channels 1-4
    name: "House Average Temperature"
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
