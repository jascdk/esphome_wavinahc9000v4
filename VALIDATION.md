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
            ├── binary_sensor.py        # Binary sensor platform
            ├── text_sensor.py          # Text sensor platform
            ├── switch.py               # Switch platform
            ├── button.py               # Button platform
            └── number.py               # Number platform
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
