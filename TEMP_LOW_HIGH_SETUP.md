# How to Add temp_low and temp_high Number Entities

The `temp_low` (eco) and `temp_high` (comfort) number entities are **optional** and must be explicitly configured in your YAML. They don't appear automatically.

## Quick Setup

Add these to your configuration under the `number:` section:

```yaml
number:
  # Temperature Low (Eco mode) - for channels 1-4
  - platform: wavin_ahc9000
    wavin_ahc9000_id: wavin_controller
    type: temp_low
    members: [1, 2, 3, 4]
    name: "Eco Temperature"
    # entity_category: config  # Optional: Uncomment to hide from main UI
  
  # Temperature High (Comfort mode) - for channels 1-4
  - platform: wavin_ahc9000
    wavin_ahc9000_id: wavin_controller
    type: temp_high
    members: [1, 2, 3, 4]
    name: "Comfort Temperature"
    # entity_category: config  # Optional: Uncomment to hide from main UI
```

## ⚠️ Entity Visibility in Home Assistant

**Important**: Where you can find these entities depends on the `entity_category` setting:

- **Without `entity_category: config`** (recommended for easy access):
  - Entities appear in your Home Assistant dashboards
  - Visible in the main entity list
  - Easy to add to lovelace cards

- **With `entity_category: config`**:
  - Entities are **hidden from the main UI**
  - Only accessible via: Settings → Devices & Services → ESPHome → [Your Device] → Configure
  - Or by searching for the entity name in entity settings

**If you can't see your entities**: Remove the `entity_category: config` line from your YAML configuration and re-upload your ESPHome firmware.

## What These Do

- **temp_low (Eco)**: Sets the target temperature when the thermostat is in ECO mode
- **temp_high (Comfort)**: Sets the target temperature when the thermostat is in COMFORT mode
- **members**: List of channel numbers (1-16) that this setting controls

## With Bi-Directional Sync

When `sync_group_settings: true` is enabled:
- Changes made on the physical thermostats will be detected
- The Home Assistant UI will update to show the new values
- Other thermostats in the same `members` list will be updated automatically

## Example with Multiple Groups

```yaml
number:
  # Living area (channels 1, 2)
  - platform: wavin_ahc9000
    wavin_ahc9000_id: wavin_controller
    type: temp_low
    members: [1, 2]
    name: "Living Area Eco Temperature"
  
  - platform: wavin_ahc9000
    wavin_ahc9000_id: wavin_controller
    type: temp_high
    members: [1, 2]
    name: "Living Area Comfort Temperature"
  
  # Bedrooms (channels 3, 4)
  - platform: wavin_ahc9000
    wavin_ahc9000_id: wavin_controller
    type: temp_low
    members: [3, 4]
    name: "Bedrooms Eco Temperature"
  
  - platform: wavin_ahc9000
    wavin_ahc9000_id: wavin_controller
    type: temp_high
    members: [3, 4]
    name: "Bedrooms Comfort Temperature"
```

## Important Notes

1. **These are optional** - You only need them if you want to control eco/comfort temperatures
2. **Requires `members` parameter** - You must specify which channels to control
3. **Range**: 6-40°C with 0.5°C steps
4. **Persistence**: Values are saved to flash and restored after restart
5. **With bi-directional sync**: The entities will also update when you change settings on physical thermostats

## See Also

- Full example: `examples/temp-low-high-example.yaml`
- Bi-directional sync example: `examples/bi-directional-sync-example.yaml`
