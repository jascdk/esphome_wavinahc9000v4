# Hysteresis Number Entity with Members Parameter

## Overview

The hysteresis number entity now supports two configuration approaches:
1. **Climate-based** (`climate_id`): Ties hysteresis to a specific climate entity
2. **Members-based** (`members`): Direct channel specification without requiring a climate entity

This enhancement provides more flexibility in how you configure hysteresis control for your Wavin AHC-9000 system.

## Why This Feature?

### Problem
Previously, if you wanted hysteresis control for multiple channels, you had two options:
1. Create individual hysteresis entities for each channel (tedious for many channels)
2. Create a group climate entity just to have a hysteresis control (unnecessary entity)

### Solution
The new `members` parameter allows you to specify channels directly in the hysteresis number entity, without needing a climate entity. This is cleaner and more flexible.

## Configuration Options

### Option 1: Climate-based (Traditional)

**Use when:**
- You already have a climate entity
- You want hysteresis logically coupled with a specific climate
- You're using single-channel climate entities

```yaml
climate:
  - platform: wavin_ahc9000
    wavin_ahc9000_id: wavin_controller
    name: "Living Room"
    id: living_room_climate
    channel: 1

number:
  - platform: wavin_ahc9000
    wavin_ahc9000_id: wavin_controller
    climate_id: living_room_climate
    name: "Living Room Hysteresis"
```

### Option 2: Members-based (New)

**Use when:**
- You want hysteresis control without creating a climate entity
- You're controlling multiple zones independently
- You want cleaner configuration with fewer entities

```yaml
number:
  # Control hysteresis for multiple channels directly
  - platform: wavin_ahc9000
    wavin_ahc9000_id: wavin_controller
    members: [2, 3, 4, 5, 6, 7, 8, 9, 10, 11]
    name: "Master Hysteresis"
```

## Examples

### Example 1: Master Control for All Zones

**Before (required group climate):**
```yaml
climate:
  - platform: wavin_ahc9000
    id: master_climate
    name: "Master Control"
    members: [2, 3, 4, 5, 6, 7, 8, 9, 10, 11]

number:
  - platform: wavin_ahc9000
    climate_id: master_climate
    name: "Master Hysteresis"
```

**After (direct members):**
```yaml
number:
  - platform: wavin_ahc9000
    members: [2, 3, 4, 5, 6, 7, 8, 9, 10, 11]
    name: "Master Hysteresis"
```

**Benefit**: One fewer entity in Home Assistant, cleaner configuration.

### Example 2: Mixed Approach

You can mix both approaches in the same configuration:

```yaml
climate:
  - platform: wavin_ahc9000
    name: "Living Room"
    id: living_room_climate
    channel: 1
  
  - platform: wavin_ahc9000
    name: "Kitchen"
    channel: 2
  
  - platform: wavin_ahc9000
    name: "Bedroom"
    channel: 3

number:
  # Individual hysteresis for Living Room (climate-based)
  - platform: wavin_ahc9000
    climate_id: living_room_climate
    name: "Living Room Hysteresis"
  
  # Group hysteresis for Kitchen and Bedroom (members-based)
  - platform: wavin_ahc9000
    members: [2, 3]
    name: "Upstairs Hysteresis"
```

### Example 3: Multiple Groups

```yaml
number:
  # All main zones
  - platform: wavin_ahc9000
    members: [2, 3, 4, 5, 6]
    name: "Main House Hysteresis"
  
  # Just bedrooms
  - platform: wavin_ahc9000
    members: [7, 8, 9]
    name: "Bedrooms Hysteresis"
  
  # Basement zones
  - platform: wavin_ahc9000
    members: [10, 11]
    name: "Basement Hysteresis"
```

## Behavior

### Writing Hysteresis

When you change a hysteresis value using the `members` parameter:

1. The value is written to all specified channels
2. No sibling propagation occurs (since you're explicitly listing all channels)
3. The value persists across restarts
4. All channels receive the same hysteresis setting

**Example Log Output:**
```
[I][wavin_ahc9000.number:xxx] Writing hysteresis 0.5°C to 10 channel(s)
[I][wavin_ahc9000:xxx] Hysteresis written to thermostat: ch=2 value=0.5°C (raw=0x0005)
[I][wavin_ahc9000:xxx] Hysteresis written to thermostat: ch=3 value=0.5°C (raw=0x0005)
[I][wavin_ahc9000:xxx] Hysteresis written to thermostat: ch=4 value=0.5°C (raw=0x0005)
... (continues for all channels)
```

### Persistence

The hysteresis value is saved to flash memory and restored on ESP32 restart, regardless of whether you use `climate_id` or `members`.

### Default Value

- With `climate_id`: Uses the climate entity's default hysteresis (0.3°C)
- With `members`: Uses a default of 0.3°C

## Comparison: Climate-based vs Members-based

| Aspect | Climate-based | Members-based |
|--------|---------------|---------------|
| **Entities Required** | Climate + Number | Number only |
| **Configuration** | 2 blocks | 1 block |
| **Flexibility** | Tied to climate | Independent |
| **Sibling Propagation** | Yes (for single-channel climates) | No (explicit channels) |
| **Use Case** | Logical coupling | Direct control |
| **Home Assistant UI** | 2 entities | 1 entity |

## Advanced Use Cases

### Case 1: Different Hysteresis for Different Areas

```yaml
number:
  # Tight control for bedrooms (0.2°C default)
  - platform: wavin_ahc9000
    members: [3, 4, 5]
    name: "Bedrooms Hysteresis"
  
  # Looser control for common areas (0.5°C default)
  - platform: wavin_ahc9000
    members: [1, 2, 6, 7]
    name: "Common Areas Hysteresis"
```

You can then set different hysteresis values for each group based on your needs.

### Case 2: Climate Without Hysteresis Control

If you want climate entities but don't need hysteresis control for them:

```yaml
climate:
  - platform: wavin_ahc9000
    name: "Living Room"
    channel: 1
  
  - platform: wavin_ahc9000
    name: "Kitchen"
    channel: 2

# No hysteresis entities needed - use default 0.3°C
```

### Case 3: Hysteresis Without Climate

If you only want hysteresis control and temperature monitoring, without climate entities:

```yaml
number:
  - platform: wavin_ahc9000
    members: [1, 2, 3, 4]
    name: "All Zones Hysteresis"

sensor:
  - platform: wavin_ahc9000
    channel: 1
    type: temperature
    name: "Zone 1 Temperature"
  
  - platform: wavin_ahc9000
    channel: 2
    type: temperature
    name: "Zone 2 Temperature"
```

## Implementation Details

### Python Configuration (number.py)

The schema now accepts either `climate_id` or `members`:

```python
CONFIG_SCHEMA = cv.All(
    number.number_schema(WavinHysteresisNumber).extend({
        cv.GenerateID(CONF_PARENT_ID): cv.use_id(WavinAHC9000),
        cv.Optional(CONF_CLIMATE_ID): cv.use_id(WavinZoneClimate),
        cv.Optional(CONF_MEMBERS): cv.ensure_list(cv.int_range(min=1, max=16)),
    }),
    cv.has_exactly_one_key(CONF_CLIMATE_ID, CONF_MEMBERS),
)
```

The `cv.has_exactly_one_key()` validator ensures you specify either `climate_id` OR `members`, but not both or neither.

### C++ Implementation (wavin_ahc9000.h)

The `WavinHysteresisNumber` class now has:

1. **Members storage**: `std::vector<uint8_t> members_`
2. **Setter method**: `void set_members(const std::vector<int> &members)`
3. **Updated write logic**: Checks `members_` first, then falls back to `climate_`

```cpp
void write_to_thermostat(float value) {
  if (this->parent_ == nullptr) return;
  
  // Priority 1: Direct members
  if (!this->members_.empty()) {
    for (uint8_t ch : this->members_) {
      this->parent_->write_channel_hysteresis(ch, value);
    }
    return;
  }
  
  // Priority 2: Climate entity
  if (this->climate_ != nullptr) {
    // ... write via climate ...
  }
}
```

## Testing

See `examples/hysteresis-members-example.yaml` for a complete test configuration.

### Test Steps

1. Deploy the example configuration
2. Change the "Master Hysteresis" value in Home Assistant
3. Check ESPHome logs to see hysteresis written to all channels
4. Verify all channels have the same hysteresis behavior

### Expected Log Output

```
[I][wavin_ahc9000.number:xxx] Writing hysteresis 0.5°C to 10 channel(s)
[I][wavin_ahc9000:xxx] Hysteresis written to thermostat: ch=2 value=0.5°C (raw=0x0005)
[I][wavin_ahc9000:xxx] Hysteresis written to thermostat: ch=3 value=0.5°C (raw=0x0005)
... (continues for all specified channels)
```

## Validation

The schema validation ensures:
- ✅ Either `climate_id` OR `members` is specified (not both, not neither)
- ✅ Channel numbers are in valid range (1-16)
- ✅ Members list is properly formatted
- ✅ Referenced climate_id exists (when used)

## Benefits

1. **Flexibility**: Choose the approach that fits your needs
2. **Cleaner Config**: Fewer entities when you don't need them
3. **Independent Control**: Hysteresis decoupled from climate entities
4. **Backward Compatible**: Existing `climate_id` configs still work
5. **Consistent Pattern**: Matches how climate and switch entities work

## Backward Compatibility

✅ **Fully backward compatible**

All existing configurations using `climate_id` continue to work exactly as before. The `members` parameter is a new optional feature.

## Migration Guide

If you want to switch from climate-based to members-based:

**Before:**
```yaml
climate:
  - platform: wavin_ahc9000
    id: master_climate
    name: "Master"
    members: [2, 3, 4, 5]

number:
  - platform: wavin_ahc9000
    climate_id: master_climate
    name: "Master Hysteresis"
```

**After:**
```yaml
# Remove the climate entity if you don't need it
# Keep it if you want climate control

number:
  - platform: wavin_ahc9000
    members: [2, 3, 4, 5]
    name: "Master Hysteresis"
```

**Note**: Your saved hysteresis value will be preserved (stored by entity object_id hash).

## Common Questions

### Q: Should I use climate_id or members?

**Use `climate_id` if:**
- You have a climate entity and want logical coupling
- You're using single-channel climates
- You want sibling propagation for group members

**Use `members` if:**
- You don't need a climate entity
- You want independent hysteresis control
- You're controlling many channels at once

### Q: Can I use both in the same configuration?

**Yes!** You can have some hysteresis entities using `climate_id` and others using `members` in the same configuration.

### Q: What about sibling propagation with members?

When using `members`, there's no sibling propagation because you're explicitly listing all channels you want to control. The value is written to all specified channels.

### Q: Can I change from climate_id to members later?

Yes, but you'll need to update your configuration and re-deploy. Your saved hysteresis value should be preserved if you keep the same entity name.

## Troubleshooting

### Configuration Error: "Must specify exactly one"

**Error:**
```
Must specify exactly one key out of climate_id, members
```

**Solution:** Specify either `climate_id` OR `members`, not both or neither.

### Channels Not Responding

**Check:**
1. Channel numbers are correct (1-16)
2. Channels are active and have thermostats
3. RS485 communication is working
4. Check ESPHome logs for write errors

### Hysteresis Not Persisting

**Check:**
1. Flash memory is working
2. No errors in logs about preference save
3. Entity object_id hasn't changed

## Conclusion

The new `members` parameter provides a more flexible way to configure hysteresis control for the Wavin AHC-9000 system. It's especially useful when you want to control multiple channels without creating extra climate entities, resulting in cleaner configurations and fewer entities in Home Assistant.

Both approaches (climate-based and members-based) have their uses, and you can choose the one that best fits your needs - or use both in the same configuration!
