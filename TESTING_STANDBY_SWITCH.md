# Testing Guide: Standby Switch Functionality

This guide describes how to validate the new `standby` switch type using the `examples/standby-switch-test.yaml` configuration.

## Prerequisites
1. Flash an ESP32 with `examples/standby-switch-test.yaml`.
2. Ensure the ESP32 is connected to the Wavin AHC-9000 controller via RS485.
3. Open Home Assistant or the ESPHome Web UI to interact with entities.

## Test Scenarios

### Scenario 1: Switch -> Climate Sync (Single Channel)
**Goal:** Verify that turning the switch ON puts the climate entity into OFF mode.

1. Ensure **Zone 1 Climate** is in `HEAT` mode.
2. Turn **ON** the `Zone 1 Standby` switch.
3. **Expectation:**
   - `Zone 1 Climate` changes to `OFF` mode immediately (or within next poll).
   - Physical thermostat display shows the Standby icon (power button).
   - Logs show: `[I][wavin_ahc9000.switch:xxx] 'Zone 1 Standby': Turning ON`

### Scenario 2: Climate -> Switch Sync (Single Channel)
**Goal:** Verify that changing the climate mode updates the switch state.

1. Set **Zone 1 Climate** to `HEAT` mode via the climate card.
2. **Expectation:**
   - `Zone 1 Standby` switch automatically turns **OFF**.
3. Set **Zone 1 Climate** to `OFF` mode via the climate card.
4. **Expectation:**
   - `Zone 1 Standby` switch automatically turns **ON**.

### Scenario 3: Master Switch Control (Group)
**Goal:** Verify that the master switch controls multiple zones simultaneously.

1. Ensure both Zone 1 and Zone 2 are in `HEAT` mode.
2. Turn **ON** the `Master Standby` switch.
3. **Expectation:**
   - `Zone 1 Climate` changes to `OFF`.
   - `Zone 2 Climate` changes to `OFF`.
   - `Zone 1 Standby` switch turns **ON**.
   - `Zone 2 Standby` switch turns **ON**.

4. Turn **OFF** the `Master Standby` switch.
5. **Expectation:**
   - Both climate entities return to `HEAT` mode.
   - Both individual standby switches turn **OFF**.

### Scenario 4: Physical Thermostat Interaction
**Goal:** Verify bi-directional sync with the physical hardware.

1. Walk to the physical thermostat for Zone 1.
2. Manually activate Standby mode (usually by holding the power/mode button).
3. Check Home Assistant.
4. **Expectation:**
   - `Zone 1 Climate` updates to `OFF`.
   - `Zone 1 Standby` switch updates to **ON**.

## Troubleshooting

**Switch flips back immediately:**
- If you turn the switch ON and it flips back OFF after a few seconds, check the logs.
- Ensure the `climate` entity isn't fighting the change (e.g., an automation setting it back to Heat).
- Verify RS485 communication is healthy (no timeout errors).

**Master switch state is inconsistent:**
- The Master Switch state is a "logical OR" of its members in the UI usually, but for control, it sends commands to all.
- If Zone 1 is ON and Zone 2 is OFF, the Master Switch might show as ON (depending on UI implementation) or OFF.
- **Test:** Toggle the Master Switch to ensure it forces *all* members to the desired state regardless of their previous state.

## Log Verification
Look for these lines in the ESPHome logs (Level: DEBUG):

```text
// When turning switch ON
[D][wavin_ahc9000.switch:045] 'Zone 1 Standby': Turning ON
[D][wavin_ahc9000:123] Writing mode: ch=1 OFF (clearing program bits)

// When sync happens from Climate/Physical change
[D][wavin_ahc9000:456] CH1 cfg=0x4001 mode=OFF ...
[D][switch:056] 'Zone 1 Standby': Turning ON
```