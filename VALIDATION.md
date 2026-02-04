# Wavin AHC-9000 Component Validation

This document describes how to validate that the component works with ESPHome's external_components feature.

## Repository Structure Validation

The repository follows the required structure for ESPHome external_components:

```
esphome_wavinahc9000v4/
├── README.md                           # Main documentation
├── examples/                           # Example configurations
│   ├── basic-example.yaml
│   ├── full-example.yaml
│   └── external-components-test.yaml
└── esphome/
    └── components/
        └── wavin_ahc9000/              # Component implementation
            ├── __init__.py             # Main component integration
            ├── CMakeLists.txt          # Build configuration
            ├── wavin_ahc9000.h         # C++ header
            ├── wavin_ahc9000.cpp       # C++ implementation
            ├── climate.py              # Climate platform
            ├── sensor.py               # Sensor platform
            ├── text_sensor.py          # Text sensor platform
            └── switch.py               # Switch platform
```

## Component Structure Checklist

✅ **Component directory**: `esphome/components/wavin_ahc9000/` exists  
✅ **Main integration**: `__init__.py` with CONFIG_SCHEMA and to_code()  
✅ **Build file**: `CMakeLists.txt` with esphome_component_register()  
✅ **C++ implementation**: `.h` and `.cpp` files  
✅ **Platform integrations**: Individual `.py` files for each platform  
✅ **Documentation**: README.md with usage instructions  
✅ **Examples**: Sample YAML configurations in `examples/`  

## How Users Can Reference This Component

### Method 1: GitHub Reference (Recommended)

```yaml
external_components:
  - source: github://jascdk/esphome_wavinahc9000v4
    components: [wavin_ahc9000]
```

### Method 2: Specific Branch or Tag

```yaml
external_components:
  - source: github://jascdk/esphome_wavinahc9000v4@main
    components: [wavin_ahc9000]
```

### Method 3: Local Development

```yaml
external_components:
  - source:
      type: local
      path: /path/to/esphome_wavinahc9000v4
    components: [wavin_ahc9000]
```

## Testing the Component

### Option 1: ESPHome Validate Command

Create a test YAML file (see `examples/external-components-test.yaml`) and run:

```bash
esphome config test.yaml
```

This will:
1. Download the component from GitHub
2. Validate the configuration
3. Check for any syntax or schema errors

### Option 2: Full Compilation Test

```bash
esphome compile test.yaml
```

This will:
1. Download the component
2. Generate C++ code
3. Compile the firmware
4. Verify all dependencies are resolved

### Option 3: Quick Syntax Check

```bash
esphome config test.yaml --no-download
```

Note: This requires the component to be cached from a previous run.

## Expected Behavior

When users add this component to their ESPHome configuration:

1. **First run**: ESPHome will download the component from GitHub to `~/.esphome/external_components/`
2. **Validation**: ESPHome will validate the YAML schema defined in `__init__.py`
3. **Code generation**: ESPHome will generate C++ code using the component's configuration
4. **Compilation**: ESPHome will compile the C++ code including `wavin_ahc9000.cpp`
5. **Upload**: The firmware will be uploaded to the ESP device

## Troubleshooting

### Component Not Found

If ESPHome reports "Component wavin_ahc9000 not found":

1. Check the repository name in the `source:` line
2. Ensure the repository is public or you have access
3. Verify the `esphome/components/wavin_ahc9000/` directory exists in the repository
4. Clear ESPHome cache: `rm -rf ~/.esphome/external_components/`

### Validation Errors

If ESPHome reports validation errors:

1. Check that all required configuration keys are provided (e.g., `uart_id`)
2. Verify channel numbers are in range 1-16
3. Ensure baud rate and parity settings match your hardware
4. Review the examples in the `examples/` directory

### Compilation Errors

If compilation fails:

1. Ensure you're using a compatible ESPHome version
2. Check that the ESP platform (ESP32/ESP8266) is correctly specified
3. Review ESPHome logs for specific error messages
4. Verify all dependencies (uart, climate, sensor, etc.) are available

## Version Compatibility

This component is designed for ESPHome 2023.x and later versions. It uses:

- Modern ESPHome configuration validation (cv)
- Codegen API (cg)
- Component registration patterns
- Platform-specific integration files

## Success Indicators

A successful integration will show:

1. ✅ Configuration validation passes
2. ✅ Code generation completes without errors
3. ✅ Compilation succeeds
4. ✅ All platforms (climate, sensor, switch, etc.) are registered
5. ✅ Device appears in Home Assistant with all entities
6. ✅ Communication with Wavin controller works via RS485

## Next Steps

After validating the component structure:

1. Test with actual hardware if available
2. Verify RS485 communication with the Wavin controller
3. Test all entity types (climate, sensors, switches, etc.)
4. Validate that zone control works correctly
5. Test auto-discovery and YAML generation features

## Humidity Sensor Testing

### Background

According to the problem statement, room thermostats can read humidity (per documentation). This implementation adds humidity sensor support to test this capability.

### What Was Implemented

The following changes enable humidity sensor testing:

1. **Added ELEM_HUMIDITY constant** (index 0x0B) in the ELEMENTS category
2. **Extended register reads** from 11 to 12 registers to include potential humidity data
3. **Added humidity field** to ChannelState structure (humidity_pct)
4. **Added sensor configuration** for "humidity" type in sensor.py
5. **Implemented parsing logic** with plausibility checks (0-100% range)

### How to Test Humidity Sensors

1. **Use the test configuration**:
   ```yaml
   # See examples/humidity-test.yaml for full example
   sensor:
     - platform: wavin_ahc9000
       wavin_ahc9000_id: wavin_controller
       channel: 1
       type: humidity
       name: "Zone 1 Humidity"
   ```

2. **Enable DEBUG logging** to see if humidity values are being read:
   ```yaml
   logger:
     level: DEBUG
   ```

3. **Look for log messages** like:
   ```
   [D][wavin_ahc9000:xxx] CH1 humidity=45.2%
   ```

### Expected Behavior

**If thermostats support humidity:**
- Humidity sensor will publish valid values (0-100%)
- DEBUG logs will show "CH# humidity=XX.X%" messages
- Home Assistant will show humidity readings for configured channels

**If thermostats do NOT support humidity:**
- Humidity sensor will remain in "Unknown" state
- No humidity log messages will appear
- Register 0x0B may contain 0, invalid data, or unrelated values

### Validation Steps

1. **Deploy the test configuration** (examples/humidity-test.yaml) to an ESP32
2. **Monitor ESPHome logs** with DEBUG level enabled
3. **Check for humidity values** in the logs after the component starts polling
4. **Observe sensor states** in Home Assistant or ESPHome dashboard
5. **Document findings**:
   - Does register 0x0B contain valid humidity data?
   - What format is the data in (raw value 0-1000 representing 0-100.0%)?
   - Do all thermostats report humidity, or only certain models?

### Interpreting Results

The humidity value uses the same temperature divisor (10.0 by default), so:
- Raw value 450 → 45.0%
- Raw value 623 → 62.3%

If you see values outside 0-100% range:
- The data might be in a different format
- Adjust the parsing logic or divisor accordingly
- Register 0x0B might not contain humidity data

### Next Steps After Testing

1. If humidity works: Update documentation with confirmed support
2. If humidity doesn't work: Document that register 0x0B doesn't contain humidity
3. If different format needed: Adjust parsing logic based on actual data observed
4. Consider testing other register indices (0x0C, 0x0D, etc.) if 0x0B doesn't work
