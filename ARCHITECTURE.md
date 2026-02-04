# Standby Switch Architecture

## How It Works

```
┌─────────────────────────────────────────────────────────────────┐
│                     Home Assistant UI                            │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  ┌────────────────────┐        ┌─────────────────────┐        │
│  │  Climate Entity    │        │  Standby Switch     │        │
│  │  "Living Room"     │◄──────►│  "Living Room       │        │
│  │                    │  Sync  │   Standby"          │        │
│  │  Mode: HEAT / OFF  │        │  State: ON / OFF    │        │
│  └────────────────────┘        └─────────────────────┘        │
│           │                              │                      │
│           │                              │                      │
└───────────┼──────────────────────────────┼──────────────────────┘
            │                              │
            │   ESPHome Wavin Component    │
            ▼                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                                                                  │
│  ┌─────────────────────────────────────────────────────────┐  │
│  │            WavinAHC9000 Hub Component                    │  │
│  │                                                           │  │
│  │  ┌─────────────────┐       ┌────────────────────────┐  │  │
│  │  │ WavinZoneClimate│       │ WavinStandbySwitch     │  │  │
│  │  │                 │       │                        │  │  │
│  │  │ mode: HEAT/OFF  │◄─────►│ write_channel_mode()   │  │  │
│  │  │                 │ Sync  │                        │  │  │
│  │  └─────────────────┘       └────────────────────────┘  │  │
│  │                                                           │  │
│  │         channels_[ch].mode = HEAT or OFF                 │  │
│  │                      ▼                                    │  │
│  │         publish_updates() syncs both entities            │  │
│  └─────────────────────────────────────────────────────────┘  │
│                              │                                 │
└──────────────────────────────┼─────────────────────────────────┘
                               │ Modbus/RS485
                               ▼
         ┌─────────────────────────────────────┐
         │    Wavin AHC-9000 Controller        │
         │                                     │
         │  PACKED_CONFIGURATION register      │
         │  MODE bits [2:0]:                   │
         │    000 = Manual (HEAT)              │
         │    001 = Standby (OFF)              │
         └─────────────────────────────────────┘
```

## State Synchronization Flow

### User turns standby switch ON:

```
1. User clicks switch in UI
   ↓
2. WavinStandbySwitch::write_state(true)
   ↓
3. parent_->write_channel_mode(ch, CLIMATE_MODE_OFF)
   ↓
4. Write MODE=001 (standby) to thermostat via Modbus
   ↓
5. publish_updates() reads current mode from channels_[ch]
   ↓
6. Updates both:
   - Climate entity: mode = OFF
   - Standby switch: state = ON (is_standby = true)
```

### User sets climate to OFF mode:

```
1. User sets climate HVAC mode to OFF
   ↓
2. WavinZoneClimate::control() called
   ↓
3. parent_->write_channel_mode(ch, CLIMATE_MODE_OFF)
   ↓
4. Write MODE=001 (standby) to thermostat via Modbus
   ↓
5. publish_updates() reads current mode from channels_[ch]
   ↓
6. Updates both:
   - Climate entity: mode = OFF
   - Standby switch: state = ON (is_standby = true)
```

## Key Components

### WavinStandbySwitch Class
```cpp
class WavinStandbySwitch : public switch_::Switch {
  void write_state(bool state) override {
    // state = true means standby (OFF mode)
    // state = false means heat (HEAT mode)
    climate::ClimateMode mode = state ? CLIMATE_MODE_OFF : CLIMATE_MODE_HEAT;
    parent_->write_channel_mode(channel_, mode);
    publish_state(state);  // Optimistic
  }
};
```

### Publishing Logic
```cpp
// In WavinAHC9000::publish_updates()
for (auto &kv : this->standby_switches_) {
  uint8_t ch = kv.first;
  auto *sw = kv.second;
  auto it = this->channels_.find(ch);
  if (it != this->channels_.end()) {
    bool is_standby = (it->second.mode == climate::CLIMATE_MODE_OFF);
    sw->publish_state(is_standby);  // Sync with actual mode
  }
}
```

## Benefits

1. **Unified State**: Single source of truth in `channels_[ch].mode`
2. **Bi-Directional**: Both controls update the same underlying state
3. **Reliable Sync**: publish_updates() ensures consistency
4. **Optimistic Updates**: Immediate UI feedback, corrected on next poll
5. **Group Support**: Master switches write to multiple channels

## Configuration Mapping

```yaml
switch:
  - platform: wavin_ahc9000
    channel: 1              # Single channel control
    type: standby          # Switch type
    ↓
WavinStandbySwitch instance
    ↓
Registered in hub.standby_switches_[1]
    ↓
Publishes based on channels_[1].mode
```

```yaml
switch:
  - platform: wavin_ahc9000
    members: [1, 2, 3]     # Group control
    type: standby
    ↓
WavinStandbySwitch instance with members_=[1,2,3]
    ↓
write_state() writes to all member channels
    ↓
Switch state = ON if all members are OFF, else OFF
```
