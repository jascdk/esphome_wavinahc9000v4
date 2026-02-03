# Quick Start Guide

Get your Wavin AHC-9000 heating controller integrated with ESPHome in minutes!

## What You Need

1. **Hardware**:
   - ESP32 or ESP8266 board
   - RS485 to TTL converter (MAX485 or similar)
   - Wavin AHC-9000 controller

2. **Software**:
   - ESPHome installed ([esphome.io](https://esphome.io))
   - Home Assistant (optional, for full integration)

## Quick Setup (5 Minutes)

### Step 1: Wire Your Hardware

Connect the RS485 module:

```
Wavin AHC-9000  ⟷  RS485 Module  ⟷  ESP32
A (RS485+)      →   A+
B (RS485-)      →   B-
                    RO (RX)      →   GPIO16
                    DI (TX)      →   GPIO17
                    DE/RE        →   GPIO5
                    VCC          →   3.3V or 5V
                    GND          →   GND
```

### Step 2: Create Your Configuration

Create a file `wavin-heating.yaml`:

```yaml
esphome:
  name: wavin-heating
  platform: ESP32
  board: esp32dev

wifi:
  ssid: !secret wifi_ssid
  password: !secret wifi_password

api:
logger:
ota:
  platform: esphome

# Load the custom component
external_components:
  - source: github://jascdk/esphome_wavinahc9000v4
    components: [wavin_ahc9000]

# Configure UART
uart:
  id: uart_bus
  tx_pin: GPIO17
  rx_pin: GPIO16
  baud_rate: 38400
  parity: EVEN

# Configure Wavin controller
wavin_ahc9000:
  id: wavin_controller
  uart_id: uart_bus
  tx_enable_pin: GPIO5
  flow_control_pin: GPIO5

# Add your first zone
climate:
  - platform: wavin_ahc9000
    wavin_ahc9000_id: wavin_controller
    name: "Living Room"
    channel: 1

# Optional: Add temperature sensor
sensor:
  - platform: wavin_ahc9000
    wavin_ahc9000_id: wavin_controller
    channel: 1
    type: temperature
    name: "Living Room Temperature"
```

### Step 3: Install to Your ESP

```bash
esphome run wavin-heating.yaml
```

Choose your connection method (USB, WiFi, etc.)

### Step 4: Add to Home Assistant

If you have Home Assistant with ESPHome integration:

1. The device will be auto-discovered
2. Go to **Settings** → **Devices & Services**
3. Look for "Wavin Heating Controller"
4. Click **Configure**

That's it! 🎉

## Next Steps

### Add More Zones

Copy the climate block for each additional zone, changing the `channel` number:

```yaml
climate:
  - platform: wavin_ahc9000
    wavin_ahc9000_id: wavin_controller
    name: "Kitchen"
    channel: 2
```

### Use Auto-Discovery

Let the component detect your zones automatically:

```yaml
text_sensor:
  - platform: wavin_ahc9000
    wavin_ahc9000_id: wavin_controller
    name: "Generated Config"

binary_sensor:
  - platform: wavin_ahc9000
    wavin_ahc9000_id: wavin_controller
    type: yaml_ready
    name: "Config Ready"
```

After boot, check the "Generated Config" sensor for suggested YAML!

### Add More Features

See [README.md](README.md) for:
- Battery monitoring
- Floor temperature sensors
- Child lock switches
- Comfort/standby setpoint controls
- Group climate (control multiple zones together)
- Repair buttons

### Full Examples

Check the `examples/` directory:
- `basic-example.yaml` - Simple single zone setup
- `full-example.yaml` - All features demonstrated
- `external-components-test.yaml` - Component reference methods

## Troubleshooting

**No communication?**
- Try swapping A and B wires on the RS485 connection
- Verify baud rate is 38400 with EVEN parity
- Check GPIO pin numbers match your wiring

**ESP crashes or reboots?**
- Check power supply is adequate (ESP32 needs stable power)
- Verify RS485 module voltage matches ESP (3.3V or 5V)

**Zones not detected?**
- Wait a few update cycles (default 5 seconds each)
- Ensure thermostats have fresh batteries
- Check that zones are actually configured in the Wavin controller

## Support

For more information:
- 📖 Full documentation: [README.md](README.md)
- 🔍 Validation guide: [VALIDATION.md](VALIDATION.md)
- 💡 Examples: [examples/](examples/)
- 🐛 Issues: [GitHub Issues](https://github.com/jascdk/esphome_wavinahc9000v4/issues)

## Common Configuration Patterns

### ESP8266 Setup

```yaml
esphome:
  name: wavin-heating
  platform: ESP8266
  board: nodemcuv2

# Use GPIO pins available on NodeMCU
uart:
  tx_pin: GPIO1  # TX pin
  rx_pin: GPIO3  # RX pin
  baud_rate: 38400
  parity: EVEN

wavin_ahc9000:
  uart_id: uart_bus
  tx_enable_pin: D5  # GPIO14
  flow_control_pin: D5
```

### Minimal Setup (No RS485 Control Pins)

Some RS485 modules don't need DE/RE control:

```yaml
wavin_ahc9000:
  id: wavin_controller
  uart_id: uart_bus
  # No tx_enable_pin or flow_control_pin needed
```

### Multiple Zones with Groups

Control bedrooms together but also individually:

```yaml
climate:
  # Individual controls
  - platform: wavin_ahc9000
    wavin_ahc9000_id: wavin_controller
    name: "Bedroom 1"
    channel: 3
  
  - platform: wavin_ahc9000
    wavin_ahc9000_id: wavin_controller
    name: "Bedroom 2"
    channel: 4
  
  # Group control
  - platform: wavin_ahc9000
    wavin_ahc9000_id: wavin_controller
    name: "All Bedrooms"
    members: [3, 4]
```

### Floor Temperature Control

For zones with floor sensors:

```yaml
climate:
  - platform: wavin_ahc9000
    wavin_ahc9000_id: wavin_controller
    name: "Bathroom"
    channel: 5
    use_floor_temperature: true

sensor:
  - platform: wavin_ahc9000
    wavin_ahc9000_id: wavin_controller
    channel: 5
    type: floor_temperature
    name: "Bathroom Floor Temperature"
```

---

Ready to get started? Just follow the Quick Setup steps above! 🚀
