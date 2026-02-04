#pragma once

#include "esphome/components/climate/climate.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/number/number.h"
#include "esphome/components/time/real_time_clock.h"
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"

#include <vector>
#include <map>
#include <set>
#include <cmath>
#include <string>

namespace esphome {
namespace sensor { class Sensor; }
namespace switch_ { class Switch; }
namespace text_sensor { class TextSensor; }
namespace number { class Number; }
namespace wavin_ahc9000 {

// Forward
class WavinZoneClimate;
class WavinChildLockSwitch;
class WavinHysteresisNumber;

class WavinAHC9000 : public PollingComponent, public uart::UARTDevice {
 public:
  void set_temp_divisor(float d) { this->temp_divisor_ = d; }
  void set_receive_timeout_ms(uint32_t t) { this->receive_timeout_ms_ = t; }
  void set_tx_enable_pin(GPIOPin *p) { this->tx_enable_pin_ = p; }
  // Optional half-duplex RS485 DE/RE (flow control) pin. If provided we drive HIGH to transmit and LOW to receive.
  void set_flow_control_pin(GPIOPin *p) { this->flow_control_pin_ = p; }
  void set_poll_channels_per_cycle(uint8_t n) { this->poll_channels_per_cycle_ = n == 0 ? 1 : (n > 16 ? 16 : n); }
  void set_allow_mode_writes(bool v) { this->allow_mode_writes_ = v; }
  bool get_allow_mode_writes() const { return this->allow_mode_writes_; }
  // Friendly name support (optional per-channel overrides for generated YAML)
  void set_channel_friendly_name(uint8_t channel, const std::string &name);
  std::string get_channel_friendly_name(uint8_t channel) const;

  void setup() override;
  void loop() override;
  void update() override;
  void dump_config() override;

  void add_channel_climate(WavinZoneClimate *c);
  void add_group_climate(WavinZoneClimate *c);
  void add_channel_battery_sensor(uint8_t ch, sensor::Sensor *s);
  void add_channel_temperature_sensor(uint8_t ch, sensor::Sensor *s);
  void add_channel_floor_temperature_sensor(uint8_t ch, sensor::Sensor *s);
  // New read-only floor limit sensors
  void add_channel_floor_min_temperature_sensor(uint8_t ch, sensor::Sensor *s);
  void add_channel_floor_max_temperature_sensor(uint8_t ch, sensor::Sensor *s);
  // Humidity sensor
  void add_channel_humidity_sensor(uint8_t ch, sensor::Sensor *s);
  // Average temperature sensor with member channels
  void add_average_temperature_sensor(sensor::Sensor *s, const std::vector<int> &members);
  void add_channel_child_lock_switch(uint8_t ch, switch_::Switch *s) { this->child_lock_switches_[ch] = s; }
  void add_active_channel(uint8_t ch);
  
  // Device info text sensors
  void add_control_unit_address_text_sensor(text_sensor::TextSensor *s) { this->control_unit_address_text_sensor_ = s; }
  void add_hw_version_text_sensor(text_sensor::TextSensor *s) { this->hw_version_text_sensor_ = s; }
  void add_sw_version_text_sensor(text_sensor::TextSensor *s) { this->sw_version_text_sensor_ = s; }
  void add_device_name_text_sensor(text_sensor::TextSensor *s) { this->device_name_text_sensor_ = s; }

  // Clock synchronization
  void set_time_id(time::RealTimeClock *time_id) { this->time_id_ = time_id; }
  void set_sync_clock_on_connect(bool v) { this->sync_clock_on_connect_ = v; }
  void set_clock_sync_interval(uint32_t seconds) { this->clock_sync_interval_ = seconds; }
  bool sync_clock_now();

  // Send commands
  void write_channel_setpoint(uint8_t channel, float celsius);
  void write_group_setpoint(const std::vector<uint8_t> &members, float celsius);
  void write_channel_mode(uint8_t channel, climate::ClimateMode mode);
  void write_channel_child_lock(uint8_t channel, bool enable);
  void write_group_child_lock(const std::vector<uint8_t> &members, bool enable);
  // Write floor temperature limits (Celsius), clamped to sane bounds
  void write_channel_floor_min_temperature(uint8_t channel, float celsius);
  void write_channel_floor_max_temperature(uint8_t channel, float celsius);
  // Write hysteresis (temperature control loop deadband) in Celsius
  void write_channel_hysteresis(uint8_t channel, float celsius);
  void refresh_channel_now(uint8_t channel);
  void set_strict_mode_write(uint8_t channel, bool enable);
  bool is_strict_mode_write(uint8_t channel) const;
  void request_status();
  void request_status_channel(uint8_t ch_index);
  void normalize_channel_config(uint8_t channel, bool off);
  void generate_yaml_suggestion();
  // Debug helper to dump registers for a channel (to identify floor min/max addresses)
  void dump_channel_floor_limits(uint8_t channel);
  // Accessor for last generated YAML (for HA notifications via lambda)
  std::string get_yaml_suggestion() const { return this->yaml_last_suggestion_; }
  std::string get_yaml_climate() const { return this->yaml_last_climate_; }
  std::string get_yaml_battery() const { return this->yaml_last_battery_; }
  std::string get_yaml_temperature() const { return this->yaml_last_temperature_; }
  std::string get_yaml_floor_temperature() const { return this->yaml_last_floor_temperature_; }
  std::string get_yaml_group_climate() const { return this->yaml_last_group_climate_; }
  // Group climate chunk helper (returns entity blocks without 'climate:' header)
  std::string get_yaml_group_climate_chunk(uint8_t start, uint8_t count) const;
  // Device info reading
  void read_device_info();
  // Chunk helpers: return YAML entity blocks (complete entities only, NO section header)
  // start is 0-based entity index among discovered active channels; count is number of entities to include
  std::string get_yaml_climate_chunk(uint8_t start, uint8_t count) const;
  std::string get_yaml_comfort_climate_chunk(uint8_t start, uint8_t count) const;
  std::string get_yaml_battery_chunk(uint8_t start, uint8_t count) const;
  std::string get_yaml_temperature_chunk(uint8_t start, uint8_t count) const;
  std::string get_yaml_floor_temperature_chunk(uint8_t start, uint8_t count) const;
  std::string get_yaml_floor_min_temperature_chunk(uint8_t start, uint8_t count) const;
  std::string get_yaml_floor_max_temperature_chunk(uint8_t start, uint8_t count) const;
  // New: child lock switch YAML chunk (returns switch entities)
  std::string get_yaml_child_lock_chunk(uint8_t start, uint8_t count) const;
  uint8_t get_yaml_active_count() const { return (uint8_t) this->yaml_active_channels_.size(); }
  bool is_channel_grouped(uint8_t ch) const { return this->yaml_grouped_channels_.count(ch) != 0; }
  bool is_channel_child_locked(uint8_t ch) const {
    auto it = this->channels_.find(ch);
    if (it == this->channels_.end()) return false;
    return it->second.child_lock;
  }
  
  // Get all sibling channels (other members of the same groups as the given channel)
  std::set<uint8_t> get_group_sibling_channels(uint8_t ch) const {
    std::set<uint8_t> siblings;
    auto it = this->channel_to_groups_.find(ch);
    if (it != this->channel_to_groups_.end()) {
      // Found groups containing this channel
      for (const auto *group : it->second) {
        // Add all members of this group to siblings set
        const auto &members = group->get_members();
        for (uint8_t member : members) {
          if (member != ch) {  // Don't include the channel itself
            siblings.insert(member);
          }
        }
      }
    }
    return siblings;
  }

  // Data access
  float get_channel_current_temp(uint8_t channel) const;
  float get_channel_setpoint(uint8_t channel) const;
  float get_channel_floor_temp(uint8_t channel) const;
  float get_channel_floor_min_temp(uint8_t channel) const;
  float get_channel_floor_max_temp(uint8_t channel) const;
  climate::ClimateMode get_channel_mode(uint8_t channel) const;
  climate::ClimateAction get_channel_action(uint8_t channel) const;

 protected:
  // Low-level protocol helpers (dkjonas framing)
  bool read_registers(uint8_t category, uint8_t page, uint8_t index, uint8_t count, std::vector<uint16_t> &out);
  bool write_register(uint8_t category, uint8_t page, uint8_t index, uint16_t value);
  // Masked write: apply (reg & and_mask) | or_mask semantics
  bool write_masked_register(uint8_t category, uint8_t page, uint8_t index, uint16_t and_mask, uint16_t or_mask);
  // Write multiple registers at once (for clock - all 7 registers must be written together)
  bool write_clock_registers(const std::vector<uint16_t> &values);

  void publish_updates();

  // Helpers
  float raw_to_c(float raw) const { return raw / this->temp_divisor_; }
  uint16_t c_to_raw(float c) const { return static_cast<uint16_t>(c * this->temp_divisor_ + 0.5f); }
  // Parse humidity from raw register value with plausibility check
  float parse_humidity(uint16_t raw_value) const {
    float humidity = this->raw_to_c(raw_value);
    // Basic plausibility filter (0..100%) to avoid invalid readings
    return (humidity >= 0.0f && humidity <= 100.0f) ? humidity : NAN;
  }

  // Simple cache per channel
  struct ChannelState {
    float current_temp_c{NAN};
    float floor_temp_c{NAN};
    // New read-only floor limits (Celsius)
    float floor_min_c{NAN};
    float floor_max_c{NAN};
    float setpoint_c{NAN};
    float standby_setpoint_c{NAN};
    float humidity_pct{NAN}; // humidity percentage (0-100)
    climate::ClimateMode mode{climate::CLIMATE_MODE_HEAT};
    climate::ClimateAction action{climate::CLIMATE_ACTION_OFF};
    uint8_t battery_pct{255}; // 0..100; 255=unknown
    uint16_t primary_index{0};
    bool all_tp_lost{false};
    bool has_floor_sensor{false};
    bool child_lock{false};
  };

  std::map<uint8_t, ChannelState> channels_;
  std::vector<WavinZoneClimate *> single_ch_climates_;
  std::vector<WavinZoneClimate *> group_climates_;
  // Mapping from channel number to list of group climates that contain it
  std::map<uint8_t, std::vector<WavinZoneClimate *>> channel_to_groups_;
  std::map<uint8_t, sensor::Sensor *> battery_sensors_;
  std::map<uint8_t, sensor::Sensor *> temperature_sensors_;
  std::map<uint8_t, sensor::Sensor *> floor_temperature_sensors_;
  // New read-only floor limit sensor maps
  std::map<uint8_t, sensor::Sensor *> floor_min_temperature_sensors_;
  std::map<uint8_t, sensor::Sensor *> floor_max_temperature_sensors_;
  std::map<uint8_t, sensor::Sensor *> humidity_sensors_;
  std::map<uint8_t, switch_::Switch *> child_lock_switches_;
  // Average temperature sensors: sensor pointer -> list of member channels
  struct AverageTempSensor {
    sensor::Sensor *sensor;
    std::vector<uint8_t> members;
  };
  std::vector<AverageTempSensor> average_temperature_sensors_;
  // Device info text sensors
  text_sensor::TextSensor *control_unit_address_text_sensor_{nullptr};
  text_sensor::TextSensor *hw_version_text_sensor_{nullptr};
  text_sensor::TextSensor *sw_version_text_sensor_{nullptr};
  text_sensor::TextSensor *device_name_text_sensor_{nullptr};
  std::string yaml_last_suggestion_{};
  std::string yaml_last_climate_{};
  std::string yaml_last_battery_{};
  std::string yaml_last_temperature_{};
  std::string yaml_last_floor_temperature_{};
  std::string yaml_last_group_climate_{}; // group climates section (optional)
  std::vector<std::vector<uint8_t>> yaml_group_climate_groups_; // channel groups used for chunking
  std::vector<uint8_t> yaml_active_channels_{}; // active channels discovered during last YAML generation
  std::vector<uint8_t> yaml_floor_channels_{}; // subset with detected floor sensors during last YAML generation
  std::vector<uint8_t> yaml_comfort_climate_channels_{}; // same as floor subset; for comfort climate generation
  std::vector<uint8_t> yaml_child_lock_channels_{}; // channels to suggest child lock switches for
  std::set<uint8_t> yaml_grouped_channels_; // channels that are members of any generated group
  std::vector<std::string> channel_friendly_names_; // 1-based index mapping (size >=17)
  std::vector<uint8_t> active_channels_;
  std::map<uint8_t, climate::ClimateMode> desired_mode_; // desired mode to reconcile after refresh
  std::set<uint8_t> strict_mode_channels_; // channels opting into strict baseline writes

  float temp_divisor_{10.0f};
  uint32_t last_poll_ms_{0};
  uint32_t receive_timeout_ms_{1000};
  uint32_t suspend_polling_until_{0};
  GPIOPin *tx_enable_pin_{nullptr};
  GPIOPin *flow_control_pin_{nullptr};
  uint8_t poll_channels_per_cycle_{2};
  uint8_t next_active_index_{0};
  uint8_t channel_step_[16] = {0};
  std::vector<uint8_t> urgent_channels_{}; // channels scheduled for immediate refresh on next update
  bool allow_mode_writes_{true};

  // YAML readiness tracking: which channels are present and which had an element block read at least once
  uint16_t yaml_primary_present_mask_{0};  // bit i set when channel (i+1) has a primary element and no tp lost
  uint16_t yaml_elem_read_mask_{0};        // bit i set when we've successfully read the element block for channel (i+1)

  // Protocol constants
  static constexpr uint8_t DEVICE_ADDR = 0x01;
  static constexpr uint8_t FC_READ = 0x43;
  static constexpr uint8_t FC_WRITE = 0x44;
  static constexpr uint8_t FC_WRITE_MASKED = 0x45;

  // Categories & indices (from dkjonas repo)
  static constexpr uint8_t CAT_ELEMENTS = 0x01;
  static constexpr uint8_t CAT_PACKED = 0x02;
  static constexpr uint8_t CAT_CHANNELS = 0x03;
  static constexpr uint8_t CAT_INFO = 0x07;

  static constexpr uint8_t CH_TIMER_EVENT = 0x00; // status incl. output bit
  static constexpr uint16_t CH_TIMER_EVENT_OUTP_ON_MASK = 0x0010;
  static constexpr uint8_t CH_PRIMARY_ELEMENT = 0x02;
  static constexpr uint16_t CH_PRIMARY_ELEMENT_ELEMENT_MASK = 0x003f;
  static constexpr uint16_t CH_PRIMARY_ELEMENT_ALL_TP_LOST_MASK = 0x0400;

  // ELEMENTS category register indices (per-element data)
  static constexpr uint8_t ELEM_STATUS = 0x00; // Status register with ALIVE, LOST, TP, etc.
  static constexpr uint8_t ELEM_AIR_TEMPERATURE = 0x04; // index within block
  static constexpr uint8_t ELEM_FLOOR_TEMPERATURE = 0x05; // index for floor probe
  static constexpr uint8_t ELEM_BATTERY_STATUS = 0x0A;  // not used yet
  static constexpr uint8_t ELEM_HUMIDITY = 0x0B;  // humidity sensor (if available)
  
  // ELEM_STATUS bit masks (from Modbus documentation page 11)
  static constexpr uint16_t ELEM_STATUS_ALIVE_MASK = 0x0001;  // Bit 0: ALIVE (1=online, 0=offline after ~25min)
  static constexpr uint16_t ELEM_STATUS_LOST_MASK = 0x0002;   // Bit 1: LOST (1=element not alive)
  static constexpr uint16_t ELEM_STATUS_TP_MASK = 0x0400;     // Bit 10: TP (1=element is thermostat)
  static constexpr uint16_t ELEM_STATUS_TP_ACT_MASK = 0x0800; // Bit 11: TP ACT (thermostat output active)

  static constexpr uint8_t PACKED_MANUAL_TEMPERATURE = 0x00;
  static constexpr uint8_t PACKED_STANDBY_TEMPERATURE = 0x04;
  static constexpr uint8_t PACKED_CONFIGURATION = 0x07;
  // Inferred from field dump: floor min/max setpoints exposed in PACKED page
  static constexpr uint8_t PACKED_FLOOR_MIN_TEMPERATURE = 0x0A; // 21.5C example
  static constexpr uint8_t PACKED_FLOOR_MAX_TEMPERATURE = 0x0B; // 25.5C example
  static constexpr uint8_t PACKED_HYSTERESIS = 0x0E; // Temperature control loop hysteresis (0.1°C per LSB)
  // Hysteresis value limits (Celsius)
  static constexpr float HYSTERESIS_MIN = 0.1f; // Minimum hysteresis (0.1°C)
  static constexpr float HYSTERESIS_MAX = 2.0f; // Maximum hysteresis (2.0°C)
  // Note: PACKED_FLOOR_MIN_TEMPERATURE and PACKED_FLOOR_MAX_TEMPERATURE are contiguous; reads
  // have been consolidated (count=2 starting at MIN) to reduce RS485 transactions.
  static constexpr uint16_t PACKED_CONFIGURATION_MODE_MASK = 0x07;
  static constexpr uint16_t PACKED_CONFIGURATION_MODE_MANUAL = 0x00;
  static constexpr uint16_t PACKED_CONFIGURATION_MODE_STANDBY = 0x01;
  static constexpr uint16_t PACKED_CONFIGURATION_MODE_STANDBY_ALT = 0x04; // fallback for variant firmwares
  static constexpr uint16_t PACKED_CONFIGURATION_PROGRAM_BIT = 0x0008; // suspected schedule/program flag
  static constexpr uint16_t PACKED_CONFIGURATION_PROGRAM_MASK = 0x0018; // extended clear: bits 3 and 4
  static constexpr uint16_t PACKED_CONFIGURATION_STRICT_UNLOCK_MASK = 0x0078; // bits 3..6 (avoid touching mode bits 0..2)
  static constexpr uint16_t PACKED_CONFIGURATION_CHILD_LOCK_MASK = 0x0800; // child lock bit (0x4000->0x4800)

  // Info category register indices
  static constexpr uint8_t INFO_CONTROL_UNIT_ADDRESS_L = 0x00;
  static constexpr uint8_t INFO_CONTROL_UNIT_ADDRESS_H = 0x01;
  static constexpr uint8_t INFO_HW_VERSION = 0x02;
  static constexpr uint8_t INFO_SW_VERSION = 0x03;
  static constexpr uint8_t INFO_DEVICE_NAME = 0x04;

  // Clock category (0x05) register indices
  static constexpr uint8_t CAT_CLOCK = 0x05;
  static constexpr uint8_t CLOCK_YEAR = 0x00;        // 2001-2099
  static constexpr uint8_t CLOCK_MONTH = 0x01;       // 1-12
  static constexpr uint8_t CLOCK_DAY = 0x02;         // 1-31
  static constexpr uint8_t CLOCK_DAY_OF_WEEK = 0x03; // 0-6 (0=Monday, 6=Sunday)
  static constexpr uint8_t CLOCK_HOUR = 0x04;        // 0-23
  static constexpr uint8_t CLOCK_MINUTE = 0x05;      // 0-59
  static constexpr uint8_t CLOCK_SECOND = 0x06;      // 0-59
  static constexpr uint8_t CLOCK_REGISTER_COUNT = 7;

  // I/O reliability: number of attempts for read/write before escalating to WARN
  static constexpr uint8_t IO_RETRY_ATTEMPTS = 2; // first failure logged at DEBUG, final at WARN

  // Clock sync state
  time::RealTimeClock *time_id_{nullptr};
  bool sync_clock_on_connect_{false};
  uint32_t clock_sync_interval_{0};  // seconds, 0 = no periodic sync
  uint32_t last_clock_sync_{0};      // millis of last sync
  bool clock_synced_once_{false};
};

// Simple dedicated switch subclass for child lock control. Avoids relying on codegen lambdas
// that reference a specific hub variable name. The state is optimistic; an urgent refresh
// scheduled by write_channel_child_lock() will reconcile if the write failed.
class WavinChildLockSwitch : public switch_::Switch {
 public:
  void set_parent(WavinAHC9000 *p) { this->parent_ = p; }
  void set_channel(uint8_t ch) { this->channel_ = ch; }
  void set_members(const std::vector<int> &members) {
    this->members_.clear();
    for (int m : members) this->members_.push_back(static_cast<uint8_t>(m));
  }
 protected:
  void write_state(bool state) override {
    if (this->parent_ != nullptr) {
      if (!this->members_.empty()) {
        // Group switch: write to all members
        this->parent_->write_group_child_lock(this->members_, state);
      } else if (this->channel_ != 0) {
        // Single channel switch
        this->parent_->write_channel_child_lock(this->channel_, state);
      }
    }
    // Optimistic publish; hub publish_updates() will correct after refresh.
    this->publish_state(state);
  }
  WavinAHC9000 *parent_{nullptr};
  uint8_t channel_{0};
  std::vector<uint8_t> members_{};
};

// Inline helpers for configuring sensors
inline void WavinAHC9000::add_channel_battery_sensor(uint8_t ch, sensor::Sensor *s) {
  this->battery_sensors_[ch] = s;
}

inline void WavinAHC9000::add_channel_temperature_sensor(uint8_t ch, sensor::Sensor *s) {
  this->temperature_sensors_[ch] = s;
}

inline void WavinAHC9000::add_channel_floor_temperature_sensor(uint8_t ch, sensor::Sensor *s) {
  this->floor_temperature_sensors_[ch] = s;
}

inline void WavinAHC9000::add_channel_floor_min_temperature_sensor(uint8_t ch, sensor::Sensor *s) {
  this->floor_min_temperature_sensors_[ch] = s;
}

inline void WavinAHC9000::add_channel_floor_max_temperature_sensor(uint8_t ch, sensor::Sensor *s) {
  this->floor_max_temperature_sensors_[ch] = s;
}

inline void WavinAHC9000::add_channel_humidity_sensor(uint8_t ch, sensor::Sensor *s) {
  this->humidity_sensors_[ch] = s;
}

class WavinZoneClimate : public climate::Climate, public Component {
 public:
  void set_parent(WavinAHC9000 *p) { this->parent_ = p; }
  void set_single_channel(uint8_t ch) {
    this->single_channel_ = ch;
    this->single_channel_set_ = true;
    this->members_.clear();
  }
  void set_use_floor_temperature(bool v) { this->use_floor_temperature_ = v; }
  void set_members(const std::vector<int> &members) {
    this->members_.clear();
    for (int m : members) this->members_.push_back(static_cast<uint8_t>(m));
    this->single_channel_set_ = false;
  }
  void set_hysteresis(float h) { this->hysteresis_ = h; }
  float get_hysteresis() const { return this->hysteresis_; }
  
  // Helper methods for hysteresis number entity to access channel info
  bool is_single_channel() const { return this->single_channel_set_; }
  uint8_t get_single_channel() const { return this->single_channel_; }
  const std::vector<uint8_t>& get_members() const { return this->members_; }

  void dump_config() override;

  void update_from_parent();

 protected:
  climate::ClimateTraits traits() override;
  void control(const climate::ClimateCall &call) override;

  WavinAHC9000 *parent_{nullptr};
  uint8_t single_channel_{0};
  bool single_channel_set_{false};
  std::vector<uint8_t> members_{};
  bool use_floor_temperature_{false};
  float hysteresis_{0.3f};  // default 0.3°C, configurable 0.1-1.0°C
};

// Hysteresis Number for controlling climate hysteresis value
class WavinHysteresisNumber : public number::Number, public Component {
 public:
  void set_parent(WavinAHC9000 *p) { this->parent_ = p; }
  void set_climate(WavinZoneClimate *c) { this->climate_ = c; }
  void set_members(const std::vector<int> &members) {
    this->members_.clear();
    for (int m : members) this->members_.push_back(static_cast<uint8_t>(m));
  }
  
  void setup() override {
    // Load persisted value from flash
    this->pref_ = global_preferences->make_preference<float>(this->get_object_id_hash());
    float value;
    if (this->pref_.load(&value)) {
      // Restore saved value
      if (this->climate_ != nullptr) {
        this->climate_->set_hysteresis(value);
      }
      // Write to thermostat on startup
      this->write_to_thermostat(value);
      this->publish_state(value);
      ESP_LOGD("wavin_ahc9000.number", "Restored hysteresis: %.1f°C", value);
    } else {
      // First boot or no saved value - use default or climate's value
      float current = 0.3f;  // default hysteresis
      if (this->climate_ != nullptr) {
        current = this->climate_->get_hysteresis();
      }
      this->publish_state(current);
      // Save the default value
      this->pref_.save(&current);
      // Write to thermostat
      this->write_to_thermostat(current);
      ESP_LOGD("wavin_ahc9000.number", "Initialized hysteresis: %.1f°C", current);
    }
  }

 protected:
  void control(float value) override {
    // Update the climate entity's hysteresis when value changes
    if (this->climate_ != nullptr) {
      this->climate_->set_hysteresis(value);
    }
    this->publish_state(value);
    // Save to flash for persistence across restarts
    this->pref_.save(&value);
    ESP_LOGD("wavin_ahc9000.number", "Saved hysteresis: %.1f°C", value);
    // Write to physical thermostat
    this->write_to_thermostat(value);
  }

  void write_to_thermostat(float value) {
    // Write hysteresis to channels - either from climate entity or direct members list
    if (this->parent_ == nullptr) return;
    
    // Track which channels we've written to (to avoid duplicates)
    std::set<uint8_t> written_channels;
    
    // If we have direct members (not tied to a climate), use those
    if (!this->members_.empty()) {
      ESP_LOGI("wavin_ahc9000.number", "Writing hysteresis %.1f°C to %zu channel(s)", value, this->members_.size());
      for (uint8_t ch : this->members_) {
        if (written_channels.find(ch) == written_channels.end()) {
          this->parent_->write_channel_hysteresis(ch, value);
          written_channels.insert(ch);
        }
      }
      return;
    }
    
    // Otherwise, use climate entity if available
    if (this->climate_ == nullptr) return;
    
    // Get the channel(s) from the climate entity
    if (this->climate_->is_single_channel()) {
      uint8_t ch = this->climate_->get_single_channel();
      ESP_LOGI("wavin_ahc9000.number", "Writing hysteresis %.1f°C to thermostat channel %u", value, (unsigned) ch);
      this->parent_->write_channel_hysteresis(ch, value);
      written_channels.insert(ch);
      
      // Also write to all sibling channels (other members of the same group(s))
      std::set<uint8_t> siblings = this->parent_->get_group_sibling_channels(ch);
      if (!siblings.empty()) {
        // Build comma-separated list of sibling channels for logging
        std::string sibling_list;
        for (uint8_t sibling : siblings) {
          if (!sibling_list.empty()) sibling_list += ", ";
          sibling_list += std::to_string(sibling);
        }
        ESP_LOGI("wavin_ahc9000.number", "Propagating hysteresis %.1f°C to sibling channel(s): %s", value, sibling_list.c_str());
        
        for (uint8_t sibling : siblings) {
          if (written_channels.find(sibling) == written_channels.end()) {
            this->parent_->write_channel_hysteresis(sibling, value);
            written_channels.insert(sibling);
          }
        }
      }
    } else {
      // For group climates, write to all member channels
      const auto &members = this->climate_->get_members();
      ESP_LOGI("wavin_ahc9000.number", "Writing hysteresis %.1f°C to thermostat for %zu channels", value, members.size());
      for (auto ch : members) {
        if (written_channels.find(ch) == written_channels.end()) {
          this->parent_->write_channel_hysteresis(ch, value);
          written_channels.insert(ch);
        }
      }
    }
  }

  WavinAHC9000 *parent_{nullptr};
  WavinZoneClimate *climate_{nullptr};
  std::vector<uint8_t> members_{};  // Direct channel list (alternative to climate_)
  ESPPreferenceObject pref_;
};

// Repair button removed; use API service to normalize

}  // namespace wavin_ahc9000
}  // namespace esphome

// --- Child lock extension placeholders (to integrate in subsequent patch) ---
// NOTE: Full integration attempted earlier but patching context mismatched. The following
// defines will be merged into the class on next edit cycle.
// Child lock bit observed: PACKED_CONFIGURATION (index 0x07) changes from 0x4000 to 0x4800 when enabled => bit 0x0800.
// Planned additions inside WavinAHC9000:
//   - bool is_channel_child_locked(uint8_t ch) const;
//   - void write_channel_child_lock(uint8_t ch, bool enable);
//   - ChannelState::bool child_lock; // per-channel cache
//   - std::map<uint8_t, switch_::Switch*> child_lock_switches_;
//   - static constexpr uint16_t PACKED_CONFIGURATION_CHILD_LOCK_MASK = 0x0800;
// Parsing: when reading PACKED_CONFIGURATION, set child_lock = (raw_cfg & mask) != 0.
// Writing: read-modify-write preserving mode bits and baseline 0x4000 prefix.
