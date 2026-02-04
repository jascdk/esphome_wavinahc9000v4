# Using Standby Mode in the UI

This guide explains how to control standby mode in the Home Assistant UI using the Wavin AHC-9000 ESPHome component.

## Overview

The Wavin AHC-9000 thermostat has a standby mode (energy-saving mode) that can be controlled in two ways:

1. **Climate Entity HVAC Mode** - Set the climate to OFF mode
2. **Standby Switch** (NEW) - Use a dedicated switch for easier control

Both methods are fully synchronized and work together.

## Method 1: Using Climate Entity HVAC Mode

The climate entity for each zone supports two HVAC modes:
- **HEAT mode**: Normal operation, maintains the target temperature
- **OFF mode**: Standby mode, heating disabled

### In Home Assistant UI:

1. Open your climate entity card (e.g., "Living Room")
2. Click on the climate card to open the more-info dialog
3. Click the **"OFF"** button in the HVAC mode selector
4. The thermostat enters standby mode

To resume heating:
1. Click the **"HEAT"** button in the HVAC mode selector
2. The thermostat returns to heating mode with the previous target temperature

## Method 2: Using Standby Switch (Recommended)

The standby switch provides a simpler, more intuitive control:
- **Switch ON**: Zone enters standby mode (climate OFF)
- **Switch OFF**: Zone returns to heating mode (climate HEAT)

### Configuration

Add standby switches to your ESPHome YAML:

```yaml
switch:
  # Single zone standby control
  - platform: wavin_ahc9000
    wavin_ahc9000_id: wavin_controller
    channel: 1
    type: standby
    name: "Living Room Standby"
    icon: "mdi:power"
  
  # Master standby for multiple zones
  - platform: wavin_ahc9000
    wavin_ahc9000_id: wavin_controller
    type: standby
    members: [1, 2, 3, 4]
    name: "Master Standby"
    icon: "mdi:power-standby"
```

### In Home Assistant UI:

1. Find the standby switch entity (e.g., "Living Room Standby")
2. Toggle the switch **ON** to enter standby mode
3. Toggle the switch **OFF** to resume heating

The switch state automatically reflects the current mode:
- Switch is ON when zone is in standby (climate OFF)
- Switch is OFF when zone is heating (climate HEAT)

## Bi-Directional Synchronization

Both methods are fully synchronized:

| Action | Result |
|--------|--------|
| Turn standby switch ON | Climate mode becomes OFF |
| Set climate to OFF mode | Standby switch becomes ON |
| Turn standby switch OFF | Climate mode becomes HEAT |
| Set climate to HEAT mode | Standby switch becomes OFF |

You can use either method, and they will always stay in sync!

## Automation Examples

### Away Mode - Turn on standby when leaving home

```yaml
automation:
  - alias: "Away mode - enable standby"
    trigger:
      - platform: state
        entity_id: binary_sensor.home_occupied
        to: "off"
        for: "00:30:00"
    action:
      - service: switch.turn_on
        target:
          entity_id: switch.master_standby
```

### Home Mode - Turn off standby when arriving home

```yaml
automation:
  - alias: "Home mode - disable standby"
    trigger:
      - platform: state
        entity_id: binary_sensor.home_occupied
        to: "on"
    action:
      - service: switch.turn_off
        target:
          entity_id: switch.master_standby
```

### Night Mode - Standby in living areas, keep bedrooms heating

```yaml
automation:
  - alias: "Night mode - living areas standby"
    trigger:
      - platform: time
        at: "23:00:00"
    action:
      - service: switch.turn_on
        target:
          entity_id:
            - switch.living_room_standby
            - switch.kitchen_standby
```

### Morning Mode - Resume heating in living areas

```yaml
automation:
  - alias: "Morning mode - living areas active"
    trigger:
      - platform: time
        at: "06:00:00"
    action:
      - service: switch.turn_off
        target:
          entity_id:
            - switch.living_room_standby
            - switch.kitchen_standby
```

### Scene Example - "Away" scene with standby enabled

```yaml
scene:
  - name: Away
    entities:
      switch.master_standby: on
      light.all_lights: off
  
  - name: Home
    entities:
      switch.master_standby: off
```

## Dashboard Cards

### Simple Toggle Card

```yaml
type: entities
entities:
  - entity: switch.living_room_standby
  - entity: switch.bedroom_standby
  - entity: switch.kitchen_standby
  - entity: switch.master_standby
```

### Button Card with Custom Styling

```yaml
type: button
entity: switch.master_standby
name: Master Standby
icon: mdi:power-standby
tap_action:
  action: toggle
show_state: true
```

### Combined Climate + Standby Card

```yaml
type: vertical-stack
cards:
  - type: thermostat
    entity: climate.living_room
  - type: entities
    entities:
      - entity: switch.living_room_standby
        name: Standby Mode
        icon: mdi:power
```

## Advantages of Standby Switch over Climate OFF Mode

1. **Clearer Intent**: A switch labeled "Standby" is more intuitive than "OFF" mode
2. **Easier Automation**: Switch services (`switch.turn_on`/`switch.turn_off`) are simpler than climate mode changes
3. **Better Dashboard Integration**: Switches integrate well with scenes, scripts, and button cards
4. **Group Control**: Master standby switches can control multiple zones with a single toggle
5. **Status Visibility**: Switch state clearly shows if standby is active across the dashboard

## FAQ

**Q: Which method should I use?**
A: The standby switch is recommended for most users as it's more intuitive. Power users familiar with HVAC modes can use either method.

**Q: Can I use both methods at the same time?**
A: Yes! They are fully synchronized, so you can mix and match based on your needs.

**Q: What happens to the target temperature when I enable standby?**
A: The target temperature is preserved. When you disable standby (turn switch OFF or set climate to HEAT), the previous temperature is restored.

**Q: Does standby mode save energy?**
A: Yes, standby mode disables heating for the zone, similar to turning off the thermostat, which saves energy.

**Q: Can I control multiple zones with one switch?**
A: Yes! Use the `members` parameter to create a master standby switch that controls multiple zones simultaneously.

## Complete Example

See [examples/standby-switch-example.yaml](../examples/standby-switch-example.yaml) for a complete working configuration with:
- Individual zone standby switches
- Master standby switch for all zones
- Automation examples
- Sensor configuration
- Full documentation

## Troubleshooting

**Switch doesn't respond:**
- Check that `allow_mode_writes: true` is set in the wavin_ahc9000 configuration (this is the default)
- Verify the channel number is correct (1-16)
- Check ESPHome logs for any errors

**Switch state doesn't update:**
- The switch state is automatically synchronized with the climate mode
- State updates happen during the component's update interval (default 5s)
- Check that the channel is active and communicating properly

**Group switch doesn't control all zones:**
- Verify all member channel numbers are correct
- Ensure all channels are active and discovered by the component
- Check ESPHome logs for individual zone write failures
