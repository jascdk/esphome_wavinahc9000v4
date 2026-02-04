# Implementation Summary: Standby Mode UI Control

## Problem Statement
User asked: "how do i set it to standby mode in the ui?"

The Wavin AHC-9000 component already supported standby mode through the climate entity's OFF/HEAT HVAC modes, but this wasn't intuitive or well-documented for users looking for a simple way to control standby mode from the Home Assistant UI.

## Solution Implemented

Added a new **standby switch** type that provides an intuitive ON/OFF control for standby mode, alongside the existing climate entity HVAC mode control.

### Key Features

1. **Simple ON/OFF Control**
   - Switch ON = Zone enters standby mode (climate OFF)
   - Switch OFF = Zone returns to heating mode (climate HEAT)

2. **Full Bi-Directional Synchronization**
   - Turn standby switch ON → Climate mode becomes OFF
   - Set climate to OFF → Standby switch becomes ON
   - Turn standby switch OFF → Climate mode becomes HEAT
   - Set climate to HEAT → Standby switch becomes OFF

3. **Single and Group Control**
   - Individual switches for each zone
   - Master switches that control multiple zones simultaneously

4. **Easy Automation**
   - Simple `switch.turn_on` and `switch.turn_off` services
   - Perfect for "away mode", "night mode", etc.

## Technical Implementation

### 1. C++ Header (wavin_ahc9000.h)
- Added `WavinStandbySwitch` class following the same pattern as `WavinChildLockSwitch`
- Added `standby_switches_` map to track switch entities
- Added `add_channel_standby_switch()` registration method

### 2. C++ Implementation (wavin_ahc9000.cpp)
- Added publishing logic in `publish_updates()` to synchronize switch state with climate mode
- Switch state reflects current climate mode (OFF = standby ON, HEAT = standby OFF)

### 3. Python Configuration (switch.py)
- Added support for "standby" switch type alongside existing "child_lock" type
- Proper switch registration and hub integration
- Support for both single channel and group control via `members` parameter

### 4. Documentation
- **README.md**: Added comprehensive switch documentation with examples
- **STANDBY_UI_GUIDE.md**: Complete guide with automation examples, dashboard cards, FAQ
- **standby-switch-example.yaml**: Full working configuration example

## Usage Examples

### Basic Configuration
```yaml
switch:
  - platform: wavin_ahc9000
    wavin_ahc9000_id: wavin_controller
    channel: 1
    type: standby
    name: "Living Room Standby"
    icon: "mdi:power"
```

### Master Standby Switch
```yaml
switch:
  - platform: wavin_ahc9000
    wavin_ahc9000_id: wavin_controller
    type: standby
    members: [1, 2, 3, 4]
    name: "Master Standby"
    icon: "mdi:power-standby"
```

### Automation Example
```yaml
automation:
  - alias: "Away mode"
    trigger:
      - platform: state
        entity_id: binary_sensor.home_occupied
        to: "off"
    action:
      - service: switch.turn_on
        target:
          entity_id: switch.master_standby
```

## Benefits Over Climate HVAC Mode Control

1. **More Intuitive**: "Standby" switch is clearer than "OFF" HVAC mode
2. **Simpler Automation**: Standard switch services vs. climate.set_hvac_mode
3. **Better UI Integration**: Works well with scenes, scripts, button cards
4. **Group Control**: Easy master switches for multiple zones
5. **Clear Status**: Switch state clearly shows standby status

## Validation

- ✅ Python syntax validation passed
- ✅ C++ structure validation passed
- ✅ Code review completed and feedback addressed
- ✅ Security scan (CodeQL): No vulnerabilities found
- ✅ Comprehensive documentation created
- ✅ Example configurations provided

## Files Modified

1. `esphome/components/wavin_ahc9000/wavin_ahc9000.h`
2. `esphome/components/wavin_ahc9000/wavin_ahc9000.cpp`
3. `esphome/components/wavin_ahc9000/switch.py`
4. `README.md`
5. `examples/standby-switch-example.yaml` (created)
6. `STANDBY_UI_GUIDE.md` (created)

## Backward Compatibility

✅ Fully backward compatible
- Existing configurations continue to work unchanged
- The "child_lock" switch type remains the default
- No breaking changes to existing functionality
- Users can adopt the new standby switch at their own pace

## Conclusion

This implementation provides users with a simple, intuitive answer to "how do i set it to standby mode in the ui?" by adding a dedicated standby switch that works alongside the existing climate entity control. Both methods are fully synchronized and users can choose the approach that best fits their needs.
