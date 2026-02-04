# Hysteresis Group Propagation Feature

## Overview

This document describes the hysteresis group propagation feature that ensures all thermostats in a group maintain consistent hysteresis values.

## Problem Statement

When configuring multiple channels in a group climate, users expect that changing the hysteresis for one member should update all members of the group. This is especially important for groups that control multiple zones together (e.g., "All Bedrooms", "Downstairs Zones").

**Before the fix:**
- Changing hysteresis for an individual channel only affected that channel
- Other members of the same group kept their old hysteresis values
- Groups had inconsistent behavior with different deadbands per thermostat
- Users had to manually update each channel's hysteresis to keep them in sync

**After the fix:**
- Changing hysteresis for a single channel automatically propagates to all sibling channels (other members of the same group)
- All thermostats in a group maintain consistent hysteresis values
- Group behavior is predictable and uniform

## How It Works

### Implementation Details

1. **Channel-to-Groups Mapping**: When group climates are registered, the system builds a reverse mapping from each channel to all groups that contain it.

2. **Sibling Detection**: When a hysteresis value is changed for a single channel, the system finds all sibling channels (other members of the same groups).

3. **Propagation**: The hysteresis value is written to:
   - The original channel
   - All sibling channels in the same group(s)

4. **Deduplication**: A set tracks which channels have been written to, preventing duplicate writes when a channel is a member of multiple groups.

### Code Changes

**In `wavin_ahc9000.h`:**
- Added `channel_to_groups_` mapping
- Added `get_group_sibling_channels()` helper method
- Updated `WavinHysteresisNumber::write_to_thermostat()` to propagate to siblings

**In `wavin_ahc9000.cpp`:**
- Updated `add_group_climate()` to populate the reverse mapping

## Configuration Example

```yaml
climate:
  # Individual climates
  - platform: wavin_ahc9000
    name: "Bedroom 1"
    id: bedroom1_climate
    channel: 3
  
  - platform: wavin_ahc9000
    name: "Bedroom 2"
    id: bedroom2_climate
    channel: 4
  
  # Group climate
  - platform: wavin_ahc9000
    name: "All Bedrooms"
    id: bedrooms_climate
    members: [3, 4]

number:
  # Individual hysteresis controls
  - platform: wavin_ahc9000
    climate_id: bedroom1_climate
    name: "Bedroom 1 Hysteresis"
  
  - platform: wavin_ahc9000
    climate_id: bedroom2_climate
    name: "Bedroom 2 Hysteresis"
  
  # Group hysteresis control
  - platform: wavin_ahc9000
    climate_id: bedrooms_climate
    name: "Bedrooms Hysteresis"
```

## Testing

See `examples/hysteresis-group-propagation-test.yaml` for a complete test configuration.

### Test Steps

1. Deploy the test configuration to your ESP32
2. Enable INFO logging to see propagation messages
3. Change "Bedroom 1 Hysteresis" from 0.3 to 0.5 in Home Assistant
4. Observe the logs

### Expected Log Output

```
[I][wavin_ahc9000.number:xxx] Writing hysteresis 0.5°C to thermostat channel 3
[I][wavin_ahc9000.number:xxx] Propagating hysteresis 0.5°C to sibling channel(s): 4
[I][wavin_ahc9000:xxx] Hysteresis written to thermostat: ch=3 value=0.5°C (raw=0x0005)
[I][wavin_ahc9000:xxx] Hysteresis written to thermostat: ch=4 value=0.5°C (raw=0x0005)
```

### Verification

After changing the hysteresis:
1. Both channels 3 and 4 should have the same hysteresis value
2. The physical thermostats should exhibit consistent behavior
3. When temperature crosses the target ± hysteresis threshold, both should switch at the same offset

## Use Cases

### Single Group
```yaml
# Channels 1, 2, 3 in one group
climate:
  - platform: wavin_ahc9000
    name: "Downstairs"
    members: [1, 2, 3]
```
Changing hysteresis for channel 1 updates channels 2 and 3.

### Multiple Groups
```yaml
# Channel 3 is in both "All Bedrooms" and "Upstairs Zones"
climate:
  - platform: wavin_ahc9000
    name: "All Bedrooms"
    members: [3, 4]
  
  - platform: wavin_ahc9000
    name: "Upstairs Zones"
    members: [3, 5, 6]
```
Changing hysteresis for channel 3 updates channels 4, 5, and 6 (all siblings across both groups).

### No Groups
```yaml
# Channel 7 is not in any group
climate:
  - platform: wavin_ahc9000
    name: "Bathroom"
    channel: 7
```
Changing hysteresis for channel 7 only affects channel 7 (no siblings to propagate to).

## Benefits

1. **Consistency**: All thermostats in a group behave uniformly
2. **User-Friendly**: Change hysteresis once, automatically applied to all group members
3. **Prevents Confusion**: No more "why does this thermostat cycle differently?"
4. **Reduces Manual Work**: No need to update each channel individually
5. **Group Coherence**: Groups truly act as unified entities

## Technical Notes

### Performance
- Sibling lookup is O(G × M) where G is the number of groups containing a channel and M is the average number of members per group
- Typically G = 1-2 and M = 2-8, so performance impact is negligible
- Duplicate writes are prevented using a set

### Memory
- The reverse mapping adds approximately 16 bytes per group membership (pointer + overhead)
- For a typical configuration with 16 channels and 2-3 groups, this is ~256 bytes

### Modbus Traffic
- Each hysteresis write sends one Modbus command per channel
- For a group of 4 channels, this is 4 commands total
- Commands are rate-limited by the existing polling suspension mechanism

## Backward Compatibility

This feature is **fully backward compatible**:
- Existing configurations without groups work exactly as before
- Single-channel climates are unaffected
- Group climates that already work continue to work the same way
- No configuration changes are required

## Limitations

1. **One-Way Propagation**: Changes propagate from a single channel to its siblings, but not between different group hysteresis entities
2. **No Cross-Group Sync**: If channel 3 is in groups A and B, changing group A's hysteresis doesn't automatically update group B's hysteresis entity (though it does update all channels)
3. **Physical Thermostat Changes**: If you manually change hysteresis on the physical thermostat, it only affects that one channel

## Future Enhancements

Possible future improvements:
- Bidirectional sync between all hysteresis entities for the same channel
- Option to disable propagation for specific channels
- Auto-detect when all group members have the same hysteresis and sync the group entity

## Troubleshooting

### Propagation Not Working

**Symptom**: Changing one channel's hysteresis doesn't update siblings

**Check**:
1. Verify the channels are in the same group (check `members:` in group climate config)
2. Enable INFO logging and look for "Propagating hysteresis" messages
3. Check that there are no communication errors in the logs

**Solution**: Ensure your configuration has a group climate with the correct members.

### Duplicate Writes

**Symptom**: Logs show hysteresis being written to the same channel multiple times

**Expected**: This should NOT happen due to deduplication logic

**Report**: If you see this, please file a bug report with your configuration and logs.

### Performance Issues

**Symptom**: Slow response when changing hysteresis

**Expected**: Should complete within 1-2 seconds for typical group sizes (4-8 channels)

**Check**: Look for RS485 communication errors or timeouts in logs

## Conclusion

The hysteresis group propagation feature ensures that all thermostats in a group maintain consistent behavior, improving user experience and reducing configuration complexity. It works transparently with existing configurations and requires no changes to use.
