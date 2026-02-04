# Fix Summary: temp_low and temp_high Not Working When Set

## Problem Statement
The temp_low (eco) and temp_high (comfort) number entities were visible in Home Assistant but did not function when users tried to set them. The values would appear to change in the UI but wouldn't actually write to the physical thermostat hardware.

## Root Cause
The Modbus register addresses used for writing eco and comfort temperatures were incorrect:
- **Incorrect**: ECO at 0x03, COMFORT at 0x02
- **Correct**: ECO at 0x08, COMFORT at 0x09

## Solution
Updated the register address constants in `wavin_ahc9000.h`:
```cpp
// Before
static constexpr uint8_t PACKED_COMFORT_TEMPERATURE = 0x02;
static constexpr uint8_t PACKED_ECO_TEMPERATURE = 0x03;

// After  
static constexpr uint8_t PACKED_COMFORT_TEMPERATURE = 0x09;
static constexpr uint8_t PACKED_ECO_TEMPERATURE = 0x08;
```

Also updated the bi-directional sync code in `wavin_ahc9000.cpp` to read from the correct register addresses in the proper order.

## Files Changed
1. `esphome/components/wavin_ahc9000/wavin_ahc9000.h` - Updated register address constants
2. `esphome/components/wavin_ahc9000/wavin_ahc9000.cpp` - Updated bi-directional sync read operation

## Testing Recommendations
Since this changes hardware communication, testing on actual hardware is recommended:

1. **Write Test**: Set temp_low and temp_high values in Home Assistant and verify they're written to the thermostat
2. **Read Test**: Verify the values can be read back correctly
3. **Sync Test**: Change values on the physical thermostat and verify they sync to Home Assistant
4. **Functionality Test**: Switch thermostat to ECO mode and verify it uses the temp_low setpoint, switch to COMFORT mode and verify it uses temp_high

## Register Map Reference
The complete PACKED category register layout:
- 0x00: Manual temperature
- 0x04: Standby temperature
- 0x07: Configuration
- 0x08: Eco temperature (temp_low)
- 0x09: Comfort temperature (temp_high)
- 0x0A: Floor min temperature
- 0x0B: Floor max temperature
- 0x0E: Hysteresis

## Impact
This fix enables users to:
- Set eco temperature setpoints from Home Assistant
- Set comfort temperature setpoints from Home Assistant
- Have those values properly written to the physical thermostat
- Use ECO and COMFORT modes with the configured temperature setpoints
