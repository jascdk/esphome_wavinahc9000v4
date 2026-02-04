# Testing Guide for Standby Mode and Hysteresis Fixes

This guide helps you test the two critical fixes implemented in this PR.

## What Was Fixed

### 1. OFF Mode (Standby) Persistence
**Problem**: Setting climate to OFF would automatically switch back to HEAT shortly after.
**Fix**: Clear program/schedule bits (0x0018) when writing mode to prevent thermostat schedule from overriding manual control.

### 2. Hysteresis Writing to Thermostat
**Problem**: Changing hysteresis value only affected ESPHome display, not the physical thermostat.
**Fix**: Write hysteresis values to PACKED register 0x0E when changed in Home Assistant.

---

## Testing: OFF Mode (Standby) Persistence

### Prerequisites
- ESPHome device with Wavin AHC-9000 component installed
- Home Assistant with climate entity configured
- DEBUG logging enabled (see example below)

### Test Steps

1. **Enable DEBUG Logging** in your ESPHome configuration:
   ```yaml
   logger:
     level: DEBUG
   ```

2. **Turn OFF the Climate Entity** in Home Assistant:
   - Open the climate entity card
   - Set mode to "OFF"
   - The thermostat should enter standby mode

3. **Monitor ESPHome Logs** for these messages:
   ```
   [D][wavin_ahc9000:xxx] CH1 cfg=0x4001 mode=OFF child_lock=N
   [I][wavin_ahc9000:xxx] Writing mode: ch=1 OFF (clearing program bits)
   ```

4. **Wait 5-10 Minutes** and check:
   - Climate entity should remain in OFF mode
   - Thermostat display should show standby/off icon
   - No automatic switching back to HEAT

5. **Check Physical Thermostat**:
   - Press buttons on the physical thermostat
   - Verify it shows standby/off mode
   - It should NOT show scheduled/program mode active

6. **Test Bi-Directional Sync**:
   - On the physical thermostat, switch to manual/heat mode
   - Within 5-10 seconds, Home Assistant should show HEAT mode
   - On the physical thermostat, switch to standby/off
   - Within 5-10 seconds, Home Assistant should show OFF mode

### Expected Results
✅ Climate stays in OFF mode indefinitely
✅ No log messages about reconciling mode back to HEAT
✅ Physical thermostat shows standby/off
✅ Bi-directional sync works correctly

### Troubleshooting

**If climate switches back to HEAT**:
- Check logs for "Reconciling mode" messages
- Look for configuration value in logs (should be 0x4001, not 0x4009 or 0x4019)
- The fix might not be applied - verify you're running the updated code

**If physical thermostat doesn't reflect changes**:
- Check RS485 wiring
- Verify baud rate (38400) and parity (EVEN)
- Look for communication errors in logs

---

## Testing: Hysteresis Writing to Thermostat

### Prerequisites
- ESPHome device with hysteresis number entity configured
- Climate entity configured for at least one channel
- DEBUG logging enabled

### Test Configuration

Add a hysteresis number entity if you don't have one:

```yaml
number:
  - platform: wavin_ahc9000
    wavin_ahc9000_id: wavin_controller
    climate_id: living_room_climate  # Your climate entity ID
    name: "Living Room Hysteresis"
    type: hysteresis
```

### Test Steps

1. **Change Hysteresis Value** in Home Assistant:
   - Open the number entity card
   - Change value from default 0.3°C to 0.5°C
   - Observe ESPHome logs

2. **Monitor ESPHome Logs** for these messages:
   ```
   [D][wavin_ahc9000.number:xxx] Saved hysteresis: 0.5°C
   [I][wavin_ahc9000.number:xxx] Writing hysteresis 0.5°C to thermostat channel 1
   [I][wavin_ahc9000:xxx] Hysteresis written to thermostat: ch=1 value=0.5°C (raw=0x0005)
   ```

3. **Verify Register Write**:
   - Look for "Hysteresis written to thermostat" message
   - Check that raw value matches (0.5°C = 0x0005)
   - No error messages about failed writes

4. **Test Relay Behavior** (requires temperature monitoring):
   - Set target temperature to 21.0°C
   - Monitor when heating turns ON/OFF:
     - With 0.3°C hysteresis: ON at <20.7°C, OFF at >21.3°C
     - With 0.5°C hysteresis: ON at <20.5°C, OFF at >21.5°C
   - The actual relay behavior should match the hysteresis setting

5. **Test Range Limits**:
   - Try setting hysteresis to 0.05°C (should clamp to 0.1°C)
   - Try setting hysteresis to 3.0°C (should clamp to 2.0°C)
   - Check logs confirm clamping

6. **Test Persistence**:
   - Set hysteresis to a non-default value (e.g., 0.7°C)
   - Restart ESPHome device
   - Check logs show "Restored hysteresis: 0.7°C"
   - Verify write to thermostat happens on startup

### Expected Results
✅ Log shows "Hysteresis written to thermostat"
✅ Raw value in log matches expected (value * 10 in hex)
✅ Physical relay behavior changes to match new hysteresis
✅ Value persists across ESPHome restarts
✅ Value is written to thermostat on startup

### Advanced Testing: Verify Modbus Register

If you want to verify the register was actually written, you can use the dump function:

Add this button to your ESPHome configuration:

```yaml
button:
  - platform: template
    name: "Dump Channel 1 Registers"
    on_press:
      - lambda: |-
          id(wavin_controller)->dump_channel_floor_limits(1);
```

Press the button and look for this in logs:
```
[I][wavin_ahc9000:xxx] DUMP ch=1 PACKED indices 0x00..0x0F:
[I][wavin_ahc9000:xxx]   PACKED[0E]=0x0005 (5)  <- This should match your hysteresis * 10
```

### Troubleshooting

**If no "Hysteresis written" message appears**:
- Check that number entity is correctly linked to climate entity
- Verify climate entity has a channel (single_channel_set_ = true)
- Check for error messages about write failures

**If relay behavior doesn't change**:
- Verify the register write succeeded (check logs)
- Wait a few minutes for thermostat to apply new setting
- Try a larger change (e.g., 0.3 → 1.0°C) to see more obvious effect
- Check that thermostat doesn't have its own hysteresis override

**If value doesn't persist after restart**:
- Check flash memory is working (other settings persist?)
- Verify no errors about preference save failures
- Check ESPHome logs during startup for "Restored hysteresis" message

---

## Common Issues

### Program Bits Not Cleared
**Symptom**: Mode still switches automatically
**Check**: Look for configuration register value in logs
**Solution**: Verify code update is deployed

### Communication Errors
**Symptom**: Write operations fail
**Check**: RS485 wiring, baud rate, parity
**Solution**: Fix hardware/configuration issues first

### Hysteresis Not Applied
**Symptom**: Register writes succeed but relay behavior unchanged
**Check**: Verify register value with dump function
**Solution**: May need longer observation time or larger hysteresis change

---

## Reporting Issues

If you encounter problems, please provide:

1. **Full ESPHome logs** with DEBUG level enabled
2. **Configuration YAML** (sanitize sensitive info)
3. **Specific test scenario** that failed
4. **Expected vs actual behavior**
5. **Thermostat model and firmware version** (from text sensors)

Look for these key log lines:
- Mode writes: `cfg=0x____` values
- Hysteresis writes: `raw=0x____` values
- Error messages with "Failed to write"
- "Reconciling mode" messages (indicates problem)

---

## Success Criteria

Both fixes are working correctly if:

1. ✅ Climate OFF mode persists indefinitely
2. ✅ No automatic switching back to HEAT
3. ✅ Bi-directional sync works (thermostat ↔ Home Assistant)
4. ✅ Hysteresis changes are logged as written to thermostat
5. ✅ Physical relay behavior matches hysteresis setting
6. ✅ Hysteresis persists across restarts
7. ✅ No communication errors in logs

---

## Next Steps After Testing

Please report your test results so we can:
- Confirm the fixes work on actual hardware
- Identify any edge cases or issues
- Update documentation based on real-world usage
- Close the issue if everything works as expected

Thank you for testing! 🎉
