# PR Summary: Fix Hysteresis Group Propagation

## Problem Statement

The climate component had an issue where setting the hysteresis for a single thermostat channel that was a member of a group climate did not propagate the change to other members of the same group. This caused inconsistent behavior where thermostats in the same group could have different deadband settings, leading to unpredictable heating behavior.

**Original Issue Quote:**
> "The climate still goes back from off to heat when set :( It must could be fixed! The hysteresis is working - the only thing right now - when set in the UI in esphome (hysteresis) it sets all the members defined inside the yaml. But when set to another hysteresis from a given thermostat - which is set as memeber - it would be nice, if it sets all the other members then inside that yaml part it is a member of"

## Solution

Implemented automatic propagation of hysteresis changes to all sibling channels (other members of the same group).

### Technical Implementation

1. **Channel-to-Groups Reverse Mapping**
   - Added `std::map<uint8_t, std::vector<WavinZoneClimate *>> channel_to_groups_` to track which groups contain each channel
   - Populated in `add_group_climate()` when group climates are registered

2. **Sibling Detection Helper**
   - Added `get_group_sibling_channels(uint8_t ch)` method that returns all other channels in the same group(s)
   - Returns a `std::set<uint8_t>` of sibling channel numbers

3. **Hysteresis Propagation**
   - Updated `WavinHysteresisNumber::write_to_thermostat()` to write to sibling channels
   - Uses deduplication logic to prevent duplicate writes when a channel is in multiple groups
   - Comprehensive logging shows which sibling channels are being updated

## Changes Made

### Code Changes

**File: `esphome/components/wavin_ahc9000/wavin_ahc9000.h`**
- Added `channel_to_groups_` member variable (line 177)
- Added `get_group_sibling_channels()` method (lines 125-142)
- Updated `WavinHysteresisNumber::write_to_thermostat()` with propagation logic (lines 471-507)

**File: `esphome/components/wavin_ahc9000/wavin_ahc9000.cpp`**
- Updated `add_group_climate()` to populate channel-to-groups mapping (lines 415-422)

### Documentation

**File: `HYSTERESIS_GROUP_PROPAGATION.md`**
- Comprehensive 200+ line document explaining the feature
- Use cases, testing instructions, troubleshooting guide
- Technical details and performance notes

**File: `README.md`**
- Updated Number Entity section to document group propagation feature
- Added reference to detailed documentation

**File: `examples/hysteresis-group-propagation-test.yaml`**
- Complete test configuration demonstrating the fix
- Step-by-step testing instructions
- Expected log output examples

## Behavior

### Before the Fix
```
User sets "Bedroom 1 Hysteresis" to 0.5°C
→ Only channel 3 updated
→ Channel 4 keeps old value (e.g., 0.3°C)
→ Inconsistent group behavior
```

### After the Fix
```
User sets "Bedroom 1 Hysteresis" to 0.5°C
→ Channel 3 updated to 0.5°C
→ Channel 4 automatically updated to 0.5°C (sibling)
→ Consistent group behavior
→ Log shows: "Propagating hysteresis 0.5°C to sibling channel(s): 4"
```

## Testing

### Log Output Example
```
[I][wavin_ahc9000.number:xxx] Writing hysteresis 0.5°C to thermostat channel 3
[I][wavin_ahc9000.number:xxx] Propagating hysteresis 0.5°C to sibling channel(s): 4
[I][wavin_ahc9000:xxx] Hysteresis written to thermostat: ch=3 value=0.5°C (raw=0x0005)
[I][wavin_ahc9000:xxx] Hysteresis written to thermostat: ch=4 value=0.5°C (raw=0x0005)
```

### Test Configuration
See `examples/hysteresis-group-propagation-test.yaml` for a complete test setup with:
- Individual climates for channels 3 and 4
- Group climate for "All Bedrooms" (members 3, 4)
- Hysteresis number entities for each
- Step-by-step testing instructions

## Benefits

1. **Consistency**: All thermostats in a group maintain the same hysteresis
2. **User-Friendly**: Change once, automatically applied to group members
3. **Predictable**: Group behavior is uniform and expected
4. **Automatic**: No manual synchronization required
5. **Backward Compatible**: Works with existing configurations

## Edge Cases Handled

1. **Single Channel Not in Group**: No propagation, works as before
2. **Channel in Multiple Groups**: Propagates to all siblings across all groups
3. **Duplicate Prevention**: Tracks written channels to avoid duplicates
4. **Group Climate Hysteresis**: Already writes to all members (unchanged)

## Performance Impact

- **Memory**: ~16 bytes per group membership (typically <256 bytes total)
- **CPU**: Negligible (sibling lookup is O(G × M) with small G and M)
- **Modbus**: One command per channel (rate-limited by existing mechanism)

## Backward Compatibility

✅ **Fully backward compatible**
- Existing configurations work without changes
- Single-channel climates unaffected
- Group climates continue to work as before
- No breaking changes

## Security

- ✅ Code review completed and feedback addressed
- ✅ Security scan passed (CodeQL)
- No security concerns identified

## Files Changed

1. `esphome/components/wavin_ahc9000/wavin_ahc9000.h` - Core implementation
2. `esphome/components/wavin_ahc9000/wavin_ahc9000.cpp` - Mapping population
3. `README.md` - Feature documentation
4. `HYSTERESIS_GROUP_PROPAGATION.md` - Detailed guide (new)
5. `examples/hysteresis-group-propagation-test.yaml` - Test configuration (new)

## Commits

1. `5893b2f` - Add hysteresis propagation to group sibling channels
2. `771a0c7` - Improve logging for hysteresis propagation to list sibling channels
3. `fde99f6` - Add documentation and test example for hysteresis group propagation

## Next Steps

1. Deploy to test environment
2. Verify with actual hardware
3. Confirm log messages appear correctly
4. Test with various group configurations
5. Merge to main branch

## Related Issues

This PR addresses the second part of the original issue about hysteresis synchronization across group members.
