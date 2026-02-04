# PR Summary: Add Members Parameter to Hysteresis Number Entity

## Problem Statement

The user had a hysteresis number entity tied to a master climate control with all channels as members:

```yaml
number:
  - platform: wavin_ahc9000
    climate_id: master_climate
    name: "Master Hysteresis"

climate:
  - platform: wavin_ahc9000
    id: master_climate
    name: "Master Hus"
    members: [2, 3, 4, 5, 6, 7, 8, 9, 10, 11]
```

The user suggested: *"Would it not be more clever to utilize a member function in the hysteresis feature?"*

This meant supporting a `members` parameter directly on the hysteresis number entity, similar to how climate and switch entities work.

## Solution

Added support for a `members` parameter to the hysteresis number entity, allowing direct channel specification without requiring a climate entity reference.

### New Configuration Option

```yaml
number:
  - platform: wavin_ahc9000
    wavin_ahc9000_id: wavin_controller
    members: [2, 3, 4, 5, 6, 7, 8, 9, 10, 11]
    name: "Master Hysteresis"
```

This eliminates the need for a group climate entity when you only want hysteresis control.

## Implementation

### Python Configuration (number.py)

1. **Added members parameter to schema**:
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

2. **Updated code generation**:
```python
if CONF_CLIMATE_ID in config:
    climate_var = await cg.get_variable(config[CONF_CLIMATE_ID])
    cg.add(var.set_climate(climate_var))

if CONF_MEMBERS in config:
    cg.add(var.set_members(config[CONF_MEMBERS]))
    for ch in config[CONF_MEMBERS]:
        cg.add(hub.add_active_channel(ch))
```

### C++ Implementation (wavin_ahc9000.h)

1. **Added members storage and setter**:
```cpp
void set_members(const std::vector<int> &members) {
  this->members_.clear();
  for (int m : members) this->members_.push_back(static_cast<uint8_t>(m));
}

std::vector<uint8_t> members_{};  // Direct channel list
```

2. **Updated setup() to work without climate entity**:
```cpp
void setup() override {
  // ... load from flash ...
  float current = 0.3f;  // default hysteresis
  if (this->climate_ != nullptr) {
    current = this->climate_->get_hysteresis();
  }
  // ... save and write ...
}
```

3. **Refactored write_to_thermostat() to prioritize members**:
```cpp
void write_to_thermostat(float value) {
  if (this->parent_ == nullptr) return;
  
  // Priority 1: Direct members list
  if (!this->members_.empty()) {
    ESP_LOGI("wavin_ahc9000.number", "Writing hysteresis %.1f°C to %zu channel(s)", 
             value, this->members_.size());
    for (uint8_t ch : this->members_) {
      this->parent_->write_channel_hysteresis(ch, value);
    }
    return;
  }
  
  // Priority 2: Climate entity (existing logic)
  if (this->climate_ != nullptr) {
    // ... climate-based logic ...
  }
}
```

## Benefits

1. **More Flexible**: Don't need a climate entity just for hysteresis control
2. **Cleaner Config**: Fewer entities in Home Assistant
3. **Independent**: Hysteresis decoupled from climate entities
4. **Consistent**: Matches pattern used by climate and switch entities
5. **Backward Compatible**: All existing `climate_id` configs still work

## Configuration Comparison

### Before (requires group climate)
```yaml
climate:
  - platform: wavin_ahc9000
    id: master_climate
    name: "Master Hus"
    members: [2, 3, 4, 5, 6, 7, 8, 9, 10, 11]

number:
  - platform: wavin_ahc9000
    climate_id: master_climate
    name: "Master Hysteresis"
```

**Result**: 2 entities (1 climate + 1 number)

### After (direct members)
```yaml
number:
  - platform: wavin_ahc9000
    members: [2, 3, 4, 5, 6, 7, 8, 9, 10, 11]
    name: "Master Hysteresis"
```

**Result**: 1 entity (1 number)

## Use Cases

### Use `members` when:
- You want hysteresis control without a climate entity
- You're controlling multiple zones but each has its own climate
- You want independent hysteresis control
- You want a cleaner configuration with fewer entities

### Use `climate_id` when:
- You already have a climate entity
- You want hysteresis logically coupled with a climate
- You're using single-channel climates
- You want sibling propagation for group members

### Mixed Approach
You can use both in the same configuration:

```yaml
climate:
  - platform: wavin_ahc9000
    name: "Living Room"
    id: living_room_climate
    channel: 1

number:
  # Climate-based for Living Room
  - platform: wavin_ahc9000
    climate_id: living_room_climate
    name: "Living Room Hysteresis"
  
  # Members-based for other zones
  - platform: wavin_ahc9000
    members: [2, 3, 4, 5]
    name: "Other Zones Hysteresis"
```

## Testing

### Expected Log Output

When changing hysteresis with members parameter:

```
[I][wavin_ahc9000.number:xxx] Writing hysteresis 0.5°C to 10 channel(s)
[I][wavin_ahc9000:xxx] Hysteresis written to thermostat: ch=2 value=0.5°C (raw=0x0005)
[I][wavin_ahc9000:xxx] Hysteresis written to thermostat: ch=3 value=0.5°C (raw=0x0005)
... (continues for all channels)
```

### Test Configuration

See `examples/hysteresis-members-example.yaml` for a complete test setup.

## Files Changed

**Code (2 files):**
1. `esphome/components/wavin_ahc9000/number.py` - Schema and code generation
2. `esphome/components/wavin_ahc9000/wavin_ahc9000.h` - C++ implementation

**Documentation (3 new files):**
1. `README.md` - Updated with new configuration option
2. `HYSTERESIS_MEMBERS_PARAMETER.md` - Comprehensive guide (287 lines)
3. `examples/hysteresis-members-example.yaml` - Test configuration (181 lines)

## Validation

### Schema Validation
- ✅ Ensures exactly one of `climate_id` or `members` is specified
- ✅ Validates channel numbers are in range (1-16)
- ✅ Ensures members list is properly formatted

### Code Review
- ✅ Passed - No issues found

### Security Check
- ✅ Passed - No alerts (CodeQL Python analysis)

## Backward Compatibility

✅ **Fully backward compatible**

- All existing configurations using `climate_id` continue to work
- No breaking changes
- New `members` parameter is optional

## Key Design Decisions

1. **Either/Or Validation**: Used `cv.has_exactly_one_key()` to ensure users specify either `climate_id` OR `members`, not both or neither
2. **Priority Logic**: Members list takes priority in `write_to_thermostat()`, then falls back to climate entity
3. **Default Value**: When using members without climate, defaults to 0.3°C
4. **Consistent Pattern**: Follows the same pattern used by climate and switch entities

## Migration Path

Users can gradually migrate from climate-based to members-based:

**Step 1**: Add members-based hysteresis
```yaml
number:
  - platform: wavin_ahc9000
    members: [2, 3, 4, 5]
    name: "New Hysteresis"
```

**Step 2**: Test and verify

**Step 3**: Remove old climate-based hysteresis (if desired)
```yaml
# Can remove this if not needed
# number:
#   - platform: wavin_ahc9000
#     climate_id: old_climate
#     name: "Old Hysteresis"
```

**Step 4**: Optionally remove group climate (if not needed for other purposes)

## Commits

1. `879898c` - Add members parameter support to hysteresis number entity
2. `a66a082` - Add documentation and examples for members parameter in hysteresis

## Status

**✅ COMPLETE AND READY FOR MERGE**

- Implementation complete
- Documentation complete
- Code review passed
- Security check passed
- Backward compatible
- Test configuration provided

## Conclusion

This enhancement addresses the user's request for a more flexible hysteresis configuration. By supporting direct channel specification via the `members` parameter, users can now create cleaner configurations without unnecessary climate entities, while still maintaining full backward compatibility with existing setups.
