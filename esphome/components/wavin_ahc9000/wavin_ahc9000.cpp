#include "wavin_ahc9000.h"
#include "esphome/core/log.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/time.h"
#include "esphome/core/application.h"
#include <vector>
#include <cmath>
#include <algorithm>

namespace esphome {
namespace wavin_ahc9000 {

static const char *const TAG = "wavin_ahc9000";


// Simple Modbus CRC16 (0xA001 poly)
static uint16_t crc16(const uint8_t *frame, size_t len) {
  uint16_t temp = 0xFFFF;
  for (size_t i = 0; i < len; i++) {
    temp ^= frame[i];
    for (uint8_t j = 0; j < 8; j++) {
      bool flag = temp & 0x0001;
      temp >>= 1;
      if (flag) temp ^= 0xA001;
    }
  }
  return temp;
}

void WavinAHC9000::setup() { 
  ESP_LOGCONFIG(TAG, "Wavin AHC9000 hub setup");
  // Read device info at startup
  this->read_device_info();
  
  // Sync clock on connect if configured
  if (this->sync_clock_on_connect_ && this->time_id_ != nullptr) {
    // Delay initial sync slightly to allow time component to initialize
    this->set_timeout(2000, [this]() {
      ESP_LOGI(TAG, "Initial clock sync on connect");
      this->sync_clock_now();
    });
  }
}
void WavinAHC9000::loop() {}

void WavinAHC9000::set_channel_friendly_name(uint8_t channel, const std::string &name) {
  if (channel < 1 || channel > 16) return;
  if (this->channel_friendly_names_.size() < 17) this->channel_friendly_names_.assign(17, std::string());
  this->channel_friendly_names_[channel] = name;
}

std::string WavinAHC9000::get_channel_friendly_name(uint8_t channel) const {
  if (channel < 1 || channel > 16) return std::string();
  if (this->channel_friendly_names_.size() < 17) return std::string();
  return this->channel_friendly_names_[channel];
}

std::set<uint8_t> WavinAHC9000::get_group_sibling_channels(uint8_t ch) const {
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

void WavinAHC9000::add_hysteresis_number(WavinHysteresisNumber *num) {
  if (num != nullptr) {
    this->hysteresis_numbers_.push_back(num);
  }
}

void WavinAHC9000::add_temp_low_number(WavinTempLowNumber *num) {
  if (num != nullptr) {
    this->temp_low_numbers_.push_back(num);
  }
}

void WavinAHC9000::add_temp_high_number(WavinTempHighNumber *num) {
  if (num != nullptr) {
    this->temp_high_numbers_.push_back(num);
  }
}

void WavinAHC9000::update() {
  // If polling is temporarily suspended (after a write), skip until window expires
  if (this->suspend_polling_until_ != 0 && millis() < this->suspend_polling_until_) {
    ESP_LOGV(TAG, "Polling suspended for %u ms more", (unsigned) (this->suspend_polling_until_ - millis()));
    return;
  }

  // Process any urgent channels first (scheduled due to a write)
  std::vector<uint16_t> regs;
  uint8_t urgent_processed = 0;
  while (!this->urgent_channels_.empty() && urgent_processed < this->poll_channels_per_cycle_) {
    uint8_t ch = this->urgent_channels_.front();
    this->urgent_channels_.erase(this->urgent_channels_.begin());
    uint8_t ch_page = (uint8_t) (ch - 1);
    auto &st = this->channels_[ch];
    // Perform a compact refresh sequence for the channel
    if (this->read_registers(CAT_PACKED, ch_page, PACKED_CONFIGURATION, 1, regs) && regs.size() >= 1) {
      uint16_t raw_cfg = regs[0];
      uint16_t mode_bits = raw_cfg & PACKED_CONFIGURATION_MODE_MASK;
      // Only MODE=001 is permanent standby per Wavin spec (treat as OFF in Home Assistant)
      bool is_off = (mode_bits == PACKED_CONFIGURATION_MODE_STANDBY);
      st.mode = is_off ? climate::CLIMATE_MODE_OFF : climate::CLIMATE_MODE_HEAT;
      st.child_lock = (raw_cfg & PACKED_CONFIGURATION_CHILD_LOCK_MASK) != 0;
      ESP_LOGD(TAG, "CH%u cfg=0x%04X mode=%s child_lock=%s", (unsigned) ch, (unsigned) raw_cfg, is_off ? "OFF" : "HEAT", st.child_lock?"Y":"N");
      // Reconcile desired mode if pending and mismatch
      auto it_des = this->desired_mode_.find(ch);
      if (it_des != this->desired_mode_.end()) {
        auto want = it_des->second;
        if (want != st.mode) {
          uint16_t current = raw_cfg;
          // Enforce standard OFF bits or MANUAL, and clear SCHED_ENA to ensure manual mode
          uint16_t new_bits = (want == climate::CLIMATE_MODE_OFF) ? PACKED_CONFIGURATION_MODE_STANDBY : PACKED_CONFIGURATION_MODE_MANUAL;
          uint16_t next = (uint16_t) ((current & ~(PACKED_CONFIGURATION_MODE_MASK | PACKED_CONFIGURATION_SCHED_ENA_BIT)) | (new_bits & PACKED_CONFIGURATION_MODE_MASK));
          ESP_LOGW(TAG, "Reconciling mode for ch=%u cur=0x%04X next=0x%04X (cleared SCHED_ENA)", (unsigned) ch, (unsigned) current, (unsigned) next);
          if (this->write_register(CAT_PACKED, ch_page, PACKED_CONFIGURATION, next)) {
            // Schedule another quick check
            this->urgent_channels_.push_back(ch);
            this->suspend_polling_until_ = millis() + 100;
          }
        } else {
          // Achieved desired mode; clear desire
          this->desired_mode_.erase(it_des);
        }
      }
    }
    if (this->read_registers(CAT_PACKED, ch_page, PACKED_MANUAL_TEMPERATURE, 1, regs) && regs.size() >= 1) {
      st.setpoint_c = this->raw_to_c(regs[0]);
    }
    // Read floor min/max (read-only) during urgent refresh in one combined request (reduces bus load)
    if (this->read_registers(CAT_PACKED, ch_page, PACKED_FLOOR_MIN_TEMPERATURE, 2, regs) && regs.size() >= 2) {
      st.floor_min_c = this->raw_to_c(regs[0]);
      st.floor_max_c = this->raw_to_c(regs[1]);
    }
    if (this->read_registers(CAT_CHANNELS, ch_page, CH_TIMER_EVENT, 1, regs) && regs.size() >= 1) {
      bool heating = (regs[0] & CH_TIMER_EVENT_OUTP_ON_MASK) != 0;
      st.action = heating ? climate::CLIMATE_ACTION_HEATING : climate::CLIMATE_ACTION_IDLE;
    }
    if (st.primary_index > 0) {
      uint8_t elem_page = (uint8_t) (st.primary_index - 1);
      if (this->read_registers(CAT_ELEMENTS, elem_page, 0x00, 12, regs) && regs.size() > ELEM_AIR_TEMPERATURE) {
        st.current_temp_c = this->raw_to_c(regs[ELEM_AIR_TEMPERATURE]);
        this->yaml_elem_read_mask_ |= (1u << (ch - 1));
        if (regs.size() > ELEM_FLOOR_TEMPERATURE) {
          float ft = this->raw_to_c(regs[ELEM_FLOOR_TEMPERATURE]);
          // Basic plausibility filter (>1..90C) to avoid default/zero noise
          if (ft > 1.0f && ft < 90.0f) {
            st.floor_temp_c = ft;
            st.has_floor_sensor = true;
          } else {
            st.floor_temp_c = NAN;
          }
        }
        if (regs.size() > ELEM_HUMIDITY) {
          float humidity = this->parse_humidity(regs[ELEM_HUMIDITY]);
          if (!std::isnan(humidity)) {
            st.humidity_pct = humidity;
            ESP_LOGD(TAG, "CH%u humidity=%.1f%%", (unsigned) ch, humidity);
          } else {
            st.humidity_pct = NAN;
          }
        }
      }
    }
    urgent_processed++;
  }

  // Round-robin staged reads across active channels; each advances one step per update
  if (this->active_channels_.empty()) {
    // Default to all 1..16 if none explicitly configured
    this->active_channels_.reserve(16);
    for (uint8_t ch = 1; ch <= 16; ch++) this->active_channels_.push_back(ch);
  }

  for (uint8_t i = urgent_processed; i < this->poll_channels_per_cycle_ && !this->active_channels_.empty(); i++) {
    // Wrap active index
    if (this->next_active_index_ >= this->active_channels_.size()) this->next_active_index_ = 0;
    uint8_t ch_num = this->active_channels_[this->next_active_index_]; // 1..16
    uint8_t ch_page = (uint8_t) (ch_num - 1);
    auto &st = this->channels_[ch_num];
    uint8_t &step = this->channel_step_[ch_page];

    // Two steps per update to surface values faster
    for (int s = 0; s < 2; s++) {
      switch (step) {
        case 0: {
          if (this->read_registers(CAT_CHANNELS, ch_page, CH_PRIMARY_ELEMENT, 1, regs) && regs.size() >= 1) {
            uint16_t v = regs[0];
            st.primary_index = v & CH_PRIMARY_ELEMENT_ELEMENT_MASK;
            st.all_tp_lost = (v & CH_PRIMARY_ELEMENT_ALL_TP_LOST_MASK) != 0;
            ESP_LOGD(TAG, "CH%u primary elem=%u lost=%s", ch_num, (unsigned) st.primary_index, st.all_tp_lost ? "Y" : "N");
            if (st.primary_index > 0 && !st.all_tp_lost) this->yaml_primary_present_mask_ |= (1u << (ch_num - 1));
          } else {
            ESP_LOGW(TAG, "CH%u: primary element read failed", ch_num);
          }
          step = 1;
          break;
        }
        case 1: {
          if (this->read_registers(CAT_PACKED, ch_page, PACKED_CONFIGURATION, 1, regs) && regs.size() >= 1) {
            uint16_t raw_cfg = regs[0];
            uint16_t mode_bits = raw_cfg & PACKED_CONFIGURATION_MODE_MASK;
            // Only MODE=001 is permanent standby per Wavin spec (treat as OFF in Home Assistant)
            bool is_off = (mode_bits == PACKED_CONFIGURATION_MODE_STANDBY);
            st.mode = is_off ? climate::CLIMATE_MODE_OFF : climate::CLIMATE_MODE_HEAT;
            st.child_lock = (raw_cfg & PACKED_CONFIGURATION_CHILD_LOCK_MASK) != 0;
            ESP_LOGD(TAG, "CH%u cfg=0x%04X mode=%s child_lock=%s", ch_num, (unsigned) raw_cfg, is_off ? "OFF" : "HEAT", st.child_lock?"Y":"N");
            // Reconcile desired mode if pending and mismatch (same logic as urgent refresh)
            auto it_des = this->desired_mode_.find(ch_num);
            if (it_des != this->desired_mode_.end()) {
              auto want = it_des->second;
              if (want != st.mode) {
                uint16_t current = raw_cfg;
                // Enforce standard OFF bits or MANUAL, and clear SCHED_ENA to ensure manual mode
                uint16_t new_bits = (want == climate::CLIMATE_MODE_OFF) ? PACKED_CONFIGURATION_MODE_STANDBY : PACKED_CONFIGURATION_MODE_MANUAL;
                uint16_t next = (uint16_t) ((current & ~(PACKED_CONFIGURATION_MODE_MASK | PACKED_CONFIGURATION_SCHED_ENA_BIT)) | (new_bits & PACKED_CONFIGURATION_MODE_MASK));
                ESP_LOGW(TAG, "Reconciling mode for ch=%u cur=0x%04X next=0x%04X (cleared SCHED_ENA)", (unsigned) ch_num, (unsigned) current, (unsigned) next);
                if (this->write_register(CAT_PACKED, ch_page, PACKED_CONFIGURATION, next)) {
                  // Schedule another quick check
                  this->urgent_channels_.push_back(ch_num);
                  this->suspend_polling_until_ = millis() + 100;
                }
              } else {
                // Achieved desired mode; clear desire
                this->desired_mode_.erase(it_des);
              }
            }
          } else {
            ESP_LOGW(TAG, "CH%u: mode read failed", ch_num);
          }
          step = 2;
          break;
        }
        case 2: {
          if (this->read_registers(CAT_PACKED, ch_page, PACKED_MANUAL_TEMPERATURE, 1, regs) && regs.size() >= 1) {
            st.setpoint_c = this->raw_to_c(regs[0]);
            ESP_LOGD(TAG, "CH%u setpoint=%.1fC", ch_num, st.setpoint_c);
          } else {
            ESP_LOGW(TAG, "CH%u: setpoint read failed", ch_num);
          }
          step = 3;
          break;
        }
        case 3: {
          // Read floor min+max together (contiguous) to reduce transactions
          if (this->read_registers(CAT_PACKED, ch_page, PACKED_FLOOR_MIN_TEMPERATURE, 2, regs) && regs.size() >= 2) {
            st.floor_min_c = this->raw_to_c(regs[0]);
            st.floor_max_c = this->raw_to_c(regs[1]);
          }
          if (this->read_registers(CAT_CHANNELS, ch_page, CH_TIMER_EVENT, 1, regs) && regs.size() >= 1) {
            bool heating = (regs[0] & CH_TIMER_EVENT_OUTP_ON_MASK) != 0;
            st.action = heating ? climate::CLIMATE_ACTION_HEATING : climate::CLIMATE_ACTION_IDLE;
            ESP_LOGD(TAG, "CH%u action=%s", ch_num, heating ? "HEATING" : "IDLE");
          } else {
            ESP_LOGW(TAG, "CH%u: action read failed", ch_num);
          }
          step = 4;
          break;
        }
        case 4: {
          if (st.primary_index > 0) {
            uint8_t elem_page = (uint8_t) (st.primary_index - 1);
            if (this->read_registers(CAT_ELEMENTS, elem_page, 0x00, 12, regs) && regs.size() > ELEM_AIR_TEMPERATURE) {
              st.current_temp_c = this->raw_to_c(regs[ELEM_AIR_TEMPERATURE]);
              this->yaml_elem_read_mask_ |= (1u << (ch_num - 1));
              if (regs.size() > ELEM_FLOOR_TEMPERATURE) {
                float ft = this->raw_to_c(regs[ELEM_FLOOR_TEMPERATURE]);
                if (ft > 1.0f && ft < 90.0f) {
                  st.floor_temp_c = ft;
                  st.has_floor_sensor = true;
                } else {
                  st.floor_temp_c = NAN;
                }
              }
              ESP_LOGD(TAG, "CH%u current=%.1fC", ch_num, st.current_temp_c);
              // Publish to per-channel temperature sensor if configured
              auto it_t = this->temperature_sensors_.find(ch_num);
              if (it_t != this->temperature_sensors_.end() && it_t->second != nullptr && !std::isnan(st.current_temp_c)) {
                it_t->second->publish_state(st.current_temp_c);
              }
              // Floor sensor publish (on staged read) if configured
              auto it_ft = this->floor_temperature_sensors_.find(ch_num);
              if (it_ft != this->floor_temperature_sensors_.end() && it_ft->second != nullptr && !std::isnan(st.floor_temp_c)) {
                it_ft->second->publish_state(st.floor_temp_c);
              }
              // Humidity sensor publish if configured
              if (regs.size() > ELEM_HUMIDITY) {
                float humidity = this->parse_humidity(regs[ELEM_HUMIDITY]);
                if (!std::isnan(humidity)) {
                  st.humidity_pct = humidity;
                  ESP_LOGD(TAG, "CH%u humidity=%.1f%%", (unsigned) ch_num, humidity);
                  auto it_h = this->humidity_sensors_.find(ch_num);
                  if (it_h != this->humidity_sensors_.end() && it_h->second != nullptr) {
                    it_h->second->publish_state(humidity);
                  }
                } else {
                  st.humidity_pct = NAN;
                }
              }
              // Battery status if available (0..9 scale, where 9=100%)
              if (regs.size() > ELEM_BATTERY_STATUS) {
                uint16_t raw = regs[ELEM_BATTERY_STATUS];
                uint8_t steps = (raw > 9) ? 9 : (uint8_t) raw;
                // Map 0-9 scale to 0-100%: pct = (steps * 100) / 9 with proper rounding
                uint8_t pct = (uint8_t) ((steps * 100 + 5) / 9);
                st.battery_pct = pct;
                auto it = this->battery_sensors_.find(ch_num);
                if (it != this->battery_sensors_.end() && it->second != nullptr) {
                  it->second->publish_state((float) pct);
                }
              }
            } else {
              ESP_LOGW(TAG, "CH%u: element temp read failed", ch_num);
            }
          } else {
            st.current_temp_c = NAN;
          }
          // If bi-directional sync is enabled, proceed to step 5 to read group settings
          if (this->sync_group_settings_) {
            step = 5;
          } else {
            step = 0;
          }
          break;
        }
        case 5: {
          // Read hysteresis, eco, and comfort temperatures for bi-directional sync
          // Read all three in one combined request (they're at indices 0x0E, 0x03, 0x02)
          // Actually read them separately since they're not contiguous in this order
          // Read comfort (0x02) and eco (0x03) together (contiguous)
          if (this->read_registers(CAT_PACKED, ch_page, PACKED_COMFORT_TEMPERATURE, 2, regs) && regs.size() >= 2) {
            float comfort_temp = this->raw_to_c(regs[0]);
            float eco_temp = this->raw_to_c(regs[1]);
            
            // Detect changes and sync to group members
            if (!std::isnan(st.prev_comfort_temp_c) && std::abs(comfort_temp - st.prev_comfort_temp_c) > 0.01f) {
              ESP_LOGI(TAG, "CH%u comfort temp changed: %.1f°C -> %.1f°C", ch_num, st.prev_comfort_temp_c, comfort_temp);
              this->sync_comfort_temp_to_group(ch_num, comfort_temp);
            }
            st.prev_comfort_temp_c = comfort_temp;
            
            if (!std::isnan(st.prev_eco_temp_c) && std::abs(eco_temp - st.prev_eco_temp_c) > 0.01f) {
              ESP_LOGI(TAG, "CH%u eco temp changed: %.1f°C -> %.1f°C", ch_num, st.prev_eco_temp_c, eco_temp);
              this->sync_eco_temp_to_group(ch_num, eco_temp);
            }
            st.prev_eco_temp_c = eco_temp;
          }
          
          // Read hysteresis separately
          if (this->read_registers(CAT_PACKED, ch_page, PACKED_HYSTERESIS, 1, regs) && regs.size() >= 1) {
            float hysteresis = this->raw_to_c(regs[0]);
            
            // Detect changes and sync to group members
            if (!std::isnan(st.prev_hysteresis_c) && std::abs(hysteresis - st.prev_hysteresis_c) > 0.01f) {
              ESP_LOGI(TAG, "CH%u hysteresis changed: %.1f°C -> %.1f°C", ch_num, st.prev_hysteresis_c, hysteresis);
              this->sync_hysteresis_to_group(ch_num, hysteresis);
            }
            st.prev_hysteresis_c = hysteresis;
          }
          
          step = 0;
          break;
        }
      }
    }

  // advance to next active channel
  this->next_active_index_ = (uint8_t) ((this->next_active_index_ + 1) % this->active_channels_.size());
  }

  // publish once per cycle
  this->publish_updates();

  // Check if we need to sync the clock periodically
  if (this->time_id_ != nullptr && this->clock_sync_interval_ > 0) {
    uint32_t now = millis();
    // Check if it's time for periodic sync (after initial sync)
    if (this->clock_synced_once_ && (now - this->last_clock_sync_) >= (this->clock_sync_interval_ * 1000)) {
      ESP_LOGD(TAG, "Periodic clock sync triggered");
      this->sync_clock_now();
    }
  }
}

bool WavinAHC9000::sync_clock_now() {
  if (this->time_id_ == nullptr) {
    ESP_LOGW(TAG, "Clock sync requested but no time source configured");
    return false;
  }

  // Get current time from ESPHome time component
  auto now = this->time_id_->now();
  if (!now.is_valid()) {
    ESP_LOGD(TAG, "Time not yet valid, skipping clock sync");
    return false;
  }

  // Prepare clock register values
  std::vector<uint16_t> clock_values;
  clock_values.reserve(CLOCK_REGISTER_COUNT);
  
  // Year (2001-2099)
  uint16_t year = now.year;
  if (year < 2001 || year > 2099) {
    ESP_LOGW(TAG, "Year %u out of range (2001-2099), cannot sync clock", (unsigned) year);
    return false;
  }
  clock_values.push_back(year);
  
  // Month (1-12)
  clock_values.push_back(now.month);
  
  // Day of month (1-31)
  clock_values.push_back(now.day_of_month);
  
  // Day of week (0-6, 0=Monday, 6=Sunday)
  // ESPHome: 1=Sunday, 2=Monday, ..., 7=Saturday
  // Wavin:   0=Monday, 1=Tuesday, ..., 6=Sunday
  uint8_t dow_wavin = (now.day_of_week == 1) ? 6 : (now.day_of_week - 2);
  clock_values.push_back(dow_wavin);
  
  // Hour (0-23)
  clock_values.push_back(now.hour);
  
  // Minute (0-59)
  clock_values.push_back(now.minute);
  
  // Second (0-59)
  clock_values.push_back(now.second);

  // Day of week string lookup (ESPHome: 1=Sunday, 2=Monday, ..., 7=Saturday)
  static const char* days[] = {"?", "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  const char* day_str = (now.day_of_week >= 1 && now.day_of_week <= 7) ? days[now.day_of_week] : "?";

  ESP_LOGI(TAG, "Syncing clock: %04u-%02u-%02u %s %02u:%02u:%02u", 
           (unsigned) year, (unsigned) now.month, (unsigned) now.day_of_month,
           day_str, (unsigned) now.hour, (unsigned) now.minute, (unsigned) now.second);

  bool success = this->write_clock_registers(clock_values);
  if (success) {
    this->last_clock_sync_ = millis();
    this->clock_synced_once_ = true;
    ESP_LOGI(TAG, "Clock synchronized successfully to %04u-%02u-%02u %02u:%02u:%02u",
             (unsigned) year, (unsigned) now.month, (unsigned) now.day_of_month,
             (unsigned) now.hour, (unsigned) now.minute, (unsigned) now.second);
  } else {
    ESP_LOGW(TAG, "Failed to synchronize clock");
  }
  return success;
}

void WavinAHC9000::dump_config() { ESP_LOGCONFIG(TAG, "Wavin AHC9000 (UART test read)"); }
void WavinAHC9000::dump_channel_floor_limits(uint8_t channel) {
  if (channel < 1 || channel > 16) return;
  uint8_t page = (uint8_t) (channel - 1);
  std::vector<uint16_t> regs;
  ESP_LOGI(TAG, "DUMP ch=%u PACKED indices 0x00..0x0F:", (unsigned) channel);
  for (uint8_t idx = 0; idx <= 0x0F; idx++) {
    if (this->read_registers(CAT_PACKED, page, idx, 1, regs) && regs.size() >= 1) {
      ESP_LOGI(TAG, "  PACKED[%u]=0x%04X (%u)", (unsigned) idx, (unsigned) regs[0], (unsigned) regs[0]);
    } else {
      ESP_LOGI(TAG, "  PACKED[%u]=<err>", (unsigned) idx);
    }
  }
  // Try elements if primary known
  auto it = this->channels_.find(channel);
  if (it != this->channels_.end() && it->second.primary_index > 0 && !it->second.all_tp_lost) {
    uint8_t elem_page = (uint8_t) (it->second.primary_index - 1);
    ESP_LOGI(TAG, "DUMP ch=%u ELEMENTS page=%u indices 0x00..0x10:", (unsigned) channel, (unsigned) elem_page);
    for (uint8_t idx = 0; idx <= 0x10; idx++) {
      if (this->read_registers(CAT_ELEMENTS, elem_page, idx, 1, regs) && regs.size() >= 1) {
        ESP_LOGI(TAG, "  ELEM[%u]=0x%04X (%u)", (unsigned) idx, (unsigned) regs[0], (unsigned) regs[0]);
      } else {
        ESP_LOGI(TAG, "  ELEM[%u]=<err>", (unsigned) idx);
      }
    }
  } else {
    ESP_LOGW(TAG, "DUMP ch=%u: primary element unknown or TP lost; elements not dumped", (unsigned) channel);
  }
}

void WavinAHC9000::add_channel_climate(WavinZoneClimate *c) { this->single_ch_climates_.push_back(c); }
void WavinAHC9000::add_group_climate(WavinZoneClimate *c) { 
  this->group_climates_.push_back(c); 
  // Build reverse mapping: channel -> groups containing it
  const auto &members = c->get_members();
  for (uint8_t ch : members) {
    this->channel_to_groups_[ch].push_back(c);
  }
}
void WavinAHC9000::add_active_channel(uint8_t ch) {
  if (ch < 1 || ch > 16) return;
  if (std::find(this->active_channels_.begin(), this->active_channels_.end(), ch) == this->active_channels_.end()) {
    this->active_channels_.push_back(ch);
  }
}

void WavinAHC9000::add_average_temperature_sensor(sensor::Sensor *s, const std::vector<int> &members) {
  AverageTempSensor avg_sensor;
  avg_sensor.sensor = s;
  // Convert int vector to uint8_t vector, warning about invalid channels
  for (int m : members) {
    if (m >= 1 && m <= 16) {
      avg_sensor.members.push_back(static_cast<uint8_t>(m));
    } else {
      ESP_LOGW(TAG, "Average temperature sensor: ignoring invalid channel %d (must be 1-16)", m);
    }
  }
  if (avg_sensor.members.empty()) {
    ESP_LOGW(TAG, "Average temperature sensor has no valid member channels");
  }
  this->average_temperature_sensors_.push_back(avg_sensor);
}

// Repair functions removed; use normalize_channel_config via API service

bool WavinAHC9000::read_registers(uint8_t category, uint8_t page, uint8_t index, uint8_t count, std::vector<uint16_t> &out) {
  // Retry logic: attempt up to IO_RETRY_ATTEMPTS. First attempt failures are logged at DEBUG; only the
  // final failed attempt escalates to WARN to reduce log noise from transient bus glitches.
  for (uint8_t attempt = 0; attempt < IO_RETRY_ATTEMPTS; attempt++) {
    uint8_t msg[8];
    msg[0] = DEVICE_ADDR;
    msg[1] = FC_READ;
    msg[2] = category;
    msg[3] = index;
    msg[4] = page;
    msg[5] = count;
    uint16_t crc = crc16(msg, 6);
    msg[6] = crc & 0xFF;
    msg[7] = crc >> 8;

  // Direction control: if a dedicated flow control pin (DE/RE) is provided, drive HIGH to enable TX.
  if (this->flow_control_pin_ != nullptr) this->flow_control_pin_->digital_write(true);
  if (this->tx_enable_pin_ != nullptr) this->tx_enable_pin_->digital_write(true);
    ESP_LOGD(TAG, "TX: addr=0x%02X fc=0x%02X cat=%u idx=%u page=%u cnt=%u attempt=%u", msg[0], msg[1], category, index, page, count, (unsigned) attempt + 1);
    this->write_array(msg, 8);
    this->flush();
  // Allow line to settle; at 9600 baud 250us is < one char time but sufficient for DE switching.
  delayMicroseconds(250);
  if (this->tx_enable_pin_ != nullptr) this->tx_enable_pin_->digital_write(false);
  if (this->flow_control_pin_ != nullptr) this->flow_control_pin_->digital_write(false); // back to RX ASAP

    std::vector<uint8_t> buf;
    uint32_t start = millis();
    while (millis() - start < this->receive_timeout_ms_) {
      while (this->available()) {
        int c = this->read();
        if (c < 0) break;
        buf.push_back((uint8_t) c);
        if (buf.size() >= 5) {
          uint8_t expected = (uint8_t) (buf[2] + 5);
          if (buf[0] == DEVICE_ADDR && buf[1] == FC_READ && buf.size() == expected) {
            uint16_t rcrc = crc16(buf.data(), buf.size());
            if (rcrc != 0) {
              // CRC mismatch: retry unless last attempt
              if (attempt + 1 == IO_RETRY_ATTEMPTS) {
                ESP_LOGW(TAG, "RX: CRC mismatch (len=%u) after %u attempts", (unsigned) buf.size(), (unsigned) IO_RETRY_ATTEMPTS);
              } else {
                ESP_LOGD(TAG, "RX: CRC mismatch attempt %u (len=%u) -> retry", (unsigned) attempt + 1, (unsigned) buf.size());
              }
              goto next_attempt; // break both loops, retry
            }
            uint8_t bytes = buf[2];
            out.clear();
            for (uint8_t i = 0; i + 1 < bytes; i += 2) {
              uint16_t w = (uint16_t) (buf[3 + i] << 8) | buf[3 + i + 1];
              out.push_back(w);
            }
            return true;
          }
        }
      }
      delay(1);
    }
    // Timeout
    if (attempt + 1 == IO_RETRY_ATTEMPTS) {
      ESP_LOGW(TAG, "RX: timeout waiting for response after %u attempts (cat=%u idx=%u page=%u cnt=%u)", (unsigned) IO_RETRY_ATTEMPTS, category, index, page, count);
    } else {
      ESP_LOGD(TAG, "RX: timeout attempt %u (cat=%u idx=%u page=%u) -> retry", (unsigned) attempt + 1, category, index, page);
    }
  next_attempt:;
  }
  return false;
}

bool WavinAHC9000::write_register(uint8_t category, uint8_t page, uint8_t index, uint16_t value) {
  // Similar retry strategy as read_registers() with severity gating.
  for (uint8_t attempt = 0; attempt < IO_RETRY_ATTEMPTS; attempt++) {
    uint8_t msg[10];
    msg[0] = DEVICE_ADDR;
    msg[1] = FC_WRITE;
    msg[2] = category;
    msg[3] = index;
    msg[4] = page;
    msg[5] = 1;  // count
    msg[6] = (uint8_t) (value >> 8);
    msg[7] = (uint8_t) (value & 0xFF);
    uint16_t crc = crc16(msg, 8);
    msg[8] = (uint8_t) (crc & 0xFF);
    msg[9] = (uint8_t) (crc >> 8);

  if (this->flow_control_pin_ != nullptr) this->flow_control_pin_->digital_write(true);
  if (this->tx_enable_pin_ != nullptr) this->tx_enable_pin_->digital_write(true);
    ESP_LOGD(TAG, "TX-WR: cat=%u idx=%u page=%u val=0x%04X attempt=%u", category, index, page, (unsigned) value, (unsigned) attempt + 1);
    this->write_array(msg, 10);
    this->flush();
  delayMicroseconds(250);
  if (this->tx_enable_pin_ != nullptr) this->tx_enable_pin_->digital_write(false);
  if (this->flow_control_pin_ != nullptr) this->flow_control_pin_->digital_write(false);

    std::vector<uint8_t> buf;
    uint32_t start = millis();
    while (millis() - start < this->receive_timeout_ms_) {
      while (this->available()) {
        int c = this->read();
        if (c < 0) break;
        buf.push_back((uint8_t) c);
        if (buf.size() >= 5) {
          uint8_t expected = (uint8_t) (buf[2] + 5);
          if (buf[0] == DEVICE_ADDR && buf[1] == FC_WRITE && buf.size() == expected) {
            uint16_t rcrc = crc16(buf.data(), buf.size());
            bool ok = (rcrc == 0);
            if (!ok) {
              if (attempt + 1 == IO_RETRY_ATTEMPTS) {
                ESP_LOGW(TAG, "ACK-WR: CRC mismatch after %u attempts (cat=%u idx=%u page=%u)", (unsigned) IO_RETRY_ATTEMPTS, category, index, page);
              } else {
                ESP_LOGD(TAG, "ACK-WR: CRC mismatch attempt %u -> retry", (unsigned) attempt + 1);
              }
              goto next_wr_attempt;
            }
            ESP_LOGD(TAG, "ACK-WR: OK");
            return true;
          }
        }
      }
      delay(1);
    }
    if (attempt + 1 == IO_RETRY_ATTEMPTS) {
      ESP_LOGW(TAG, "ACK-WR: timeout after %u attempts (cat=%u idx=%u page=%u)", (unsigned) IO_RETRY_ATTEMPTS, category, index, page);
    } else {
      ESP_LOGD(TAG, "ACK-WR: timeout attempt %u (cat=%u idx=%u page=%u) -> retry", (unsigned) attempt + 1, category, index, page);
    }
  next_wr_attempt:;
  }
  return false;
}

bool WavinAHC9000::write_masked_register(uint8_t category, uint8_t page, uint8_t index, uint16_t and_mask, uint16_t or_mask) {
  // Similar retry strategy as write_register(); reduces spurious WARN logs.
  for (uint8_t attempt = 0; attempt < IO_RETRY_ATTEMPTS; attempt++) {
    uint8_t msg[12];
    msg[0] = DEVICE_ADDR;
    msg[1] = FC_WRITE_MASKED;
    msg[2] = category;
    msg[3] = index;
    msg[4] = page;
    msg[5] = 1;  // count
    msg[6] = (uint8_t) (and_mask >> 8);
    msg[7] = (uint8_t) (and_mask & 0xFF);
    msg[8] = (uint8_t) (or_mask >> 8);
    msg[9] = (uint8_t) (or_mask & 0xFF);
    uint16_t crc = crc16(msg, 10);
    msg[10] = (uint8_t) (crc & 0xFF);
    msg[11] = (uint8_t) (crc >> 8);

  if (this->flow_control_pin_ != nullptr) this->flow_control_pin_->digital_write(true);
  if (this->tx_enable_pin_ != nullptr) this->tx_enable_pin_->digital_write(true);
    ESP_LOGD(TAG, "TX-WM: cat=%u idx=%u page=%u and=0x%04X or=0x%04X attempt=%u", category, index, page, (unsigned) and_mask, (unsigned) or_mask, (unsigned) attempt + 1);
    this->write_array(msg, 12);
    this->flush();
  delayMicroseconds(250);
  if (this->tx_enable_pin_ != nullptr) this->tx_enable_pin_->digital_write(false);
  if (this->flow_control_pin_ != nullptr) this->flow_control_pin_->digital_write(false);

    std::vector<uint8_t> buf;
    uint32_t start = millis();
    while (millis() - start < this->receive_timeout_ms_) {
      while (this->available()) {
        int c = this->read();
        if (c < 0) break;
        buf.push_back((uint8_t) c);
        if (buf.size() >= 5) {
          uint8_t expected = (uint8_t) (buf[2] + 5);
          if (buf[0] == DEVICE_ADDR && buf[1] == FC_WRITE_MASKED && buf.size() == expected) {
            uint16_t rcrc = crc16(buf.data(), buf.size());
            bool ok = (rcrc == 0);
            if (!ok) {
              if (attempt + 1 == IO_RETRY_ATTEMPTS) {
                ESP_LOGW(TAG, "ACK-WM: CRC mismatch after %u attempts (cat=%u idx=%u page=%u)", (unsigned) IO_RETRY_ATTEMPTS, category, index, page);
              } else {
                ESP_LOGD(TAG, "ACK-WM: CRC mismatch attempt %u -> retry", (unsigned) attempt + 1);
              }
              goto next_wm_attempt;
            }
            ESP_LOGD(TAG, "ACK-WM: OK");
            return true;
          }
        }
      }
      delay(1);
    }
    if (attempt + 1 == IO_RETRY_ATTEMPTS) {
      ESP_LOGW(TAG, "ACK-WM: timeout after %u attempts (cat=%u idx=%u page=%u)", (unsigned) IO_RETRY_ATTEMPTS, category, index, page);
    } else {
      ESP_LOGD(TAG, "ACK-WM: timeout attempt %u (cat=%u idx=%u page=%u) -> retry", (unsigned) attempt + 1, category, index, page);
    }
  next_wm_attempt:;
  }
  return false;
}

bool WavinAHC9000::write_clock_registers(const std::vector<uint16_t> &values) {
  // Write all 7 clock registers at once as per spec requirement
  if (values.size() != CLOCK_REGISTER_COUNT) {
    ESP_LOGW(TAG, "Clock write requires exactly %u values, got %u", CLOCK_REGISTER_COUNT, (unsigned) values.size());
    return false;
  }

  for (uint8_t attempt = 0; attempt < IO_RETRY_ATTEMPTS; attempt++) {
    // Message format: ADDR(1) FC(1) CAT(1) IDX(1) PAGE(1) COUNT(1) DATA(14) CRC(2) = 22 bytes
    uint8_t msg[22];
    msg[0] = DEVICE_ADDR;
    msg[1] = FC_WRITE;
    msg[2] = CAT_CLOCK;
    msg[3] = CLOCK_YEAR;  // start at first register
    msg[4] = 0x00;        // page 0
    msg[5] = CLOCK_REGISTER_COUNT;  // write all 7 registers
    
    // Pack the 7 register values (14 bytes)
    for (uint8_t i = 0; i < CLOCK_REGISTER_COUNT; i++) {
      msg[6 + i * 2] = (uint8_t) (values[i] >> 8);
      msg[7 + i * 2] = (uint8_t) (values[i] & 0xFF);
    }
    
    uint16_t crc = crc16(msg, 20);
    msg[20] = (uint8_t) (crc & 0xFF);
    msg[21] = (uint8_t) (crc >> 8);

    if (this->flow_control_pin_ != nullptr) this->flow_control_pin_->digital_write(true);
    if (this->tx_enable_pin_ != nullptr) this->tx_enable_pin_->digital_write(true);
    ESP_LOGD(TAG, "TX-CLOCK: Writing %u registers attempt=%u", CLOCK_REGISTER_COUNT, (unsigned) attempt + 1);
    this->write_array(msg, 22);
    this->flush();
    delayMicroseconds(250);
    if (this->tx_enable_pin_ != nullptr) this->tx_enable_pin_->digital_write(false);
    if (this->flow_control_pin_ != nullptr) this->flow_control_pin_->digital_write(false);

    std::vector<uint8_t> buf;
    uint32_t start = millis();
    while (millis() - start < this->receive_timeout_ms_) {
      while (this->available()) {
        int c = this->read();
        if (c < 0) break;
        buf.push_back((uint8_t) c);
        if (buf.size() >= 5) {
          uint8_t expected = (uint8_t) (buf[2] + 5);
          if (buf[0] == DEVICE_ADDR && buf[1] == FC_WRITE && buf.size() == expected) {
            uint16_t rcrc = crc16(buf.data(), buf.size());
            bool ok = (rcrc == 0);
            if (!ok) {
              if (attempt + 1 == IO_RETRY_ATTEMPTS) {
                ESP_LOGW(TAG, "ACK-CLOCK: CRC mismatch after %u attempts", (unsigned) IO_RETRY_ATTEMPTS);
              } else {
                ESP_LOGD(TAG, "ACK-CLOCK: CRC mismatch attempt %u -> retry", (unsigned) attempt + 1);
              }
              goto next_clock_attempt;
            }
            ESP_LOGI(TAG, "Clock registers written successfully");
            return true;
          }
        }
      }
      delay(1);
    }
    if (attempt + 1 == IO_RETRY_ATTEMPTS) {
      ESP_LOGW(TAG, "ACK-CLOCK: timeout after %u attempts", (unsigned) IO_RETRY_ATTEMPTS);
    } else {
      ESP_LOGD(TAG, "ACK-CLOCK: timeout attempt %u -> retry", (unsigned) attempt + 1);
    }
  next_clock_attempt:;
  }
  return false;
}

// High-level write helpers
void WavinAHC9000::write_channel_setpoint(uint8_t channel, float celsius) {
  if (channel < 1 || channel > 16) return;
  uint8_t page = (uint8_t) (channel - 1);
  uint16_t raw = this->c_to_raw(celsius);
  if (this->write_register(CAT_PACKED, page, PACKED_MANUAL_TEMPERATURE, raw)) {
    this->channels_[channel].setpoint_c = celsius;
  // Schedule a quick refresh on next cycle and briefly suspend normal polling to avoid collisions
  this->urgent_channels_.push_back(channel);
  this->suspend_polling_until_ = millis() + 100; // 100 ms guard
  }
}

void WavinAHC9000::write_group_setpoint(const std::vector<uint8_t> &members, float celsius) {
  for (auto ch : members) this->write_channel_setpoint(ch, celsius);
}

void WavinAHC9000::write_channel_mode(uint8_t channel, climate::ClimateMode mode) {
  if (channel < 1 || channel > 16) return;
  uint8_t page = (uint8_t) (channel - 1);
  this->desired_mode_[channel] = mode;
  // Strict write: Use known-good baseline 0x4000 with appropriate MODE bits
  // 0x4000 = bit 14 set (baseline), bit 4 (SCHED_ENA) = 0, other bits = 0
  // This provides reliable manual control by clearing all scheduling/program flags
  bool ok = false;
  {
    uint16_t strict_val = (uint16_t) (0x4000 | (mode == climate::CLIMATE_MODE_OFF ? PACKED_CONFIGURATION_MODE_STANDBY : PACKED_CONFIGURATION_MODE_MANUAL));
    ok = this->write_register(CAT_PACKED, page, PACKED_CONFIGURATION, strict_val);
  }
  if (!ok) {
    // Fallback: read-modify-write to preserve other bits (e.g., CHILD_LOCK) if strict write fails
    std::vector<uint16_t> regs;
    if (this->read_registers(CAT_PACKED, page, PACKED_CONFIGURATION, 1, regs) && regs.size() >= 1) {
      uint16_t current = regs[0];
      // Clear MODE bits (0x07) and SCHED_ENA bit (0x10), then set new MODE
      uint16_t new_bits = (mode == climate::CLIMATE_MODE_OFF) ? PACKED_CONFIGURATION_MODE_STANDBY : PACKED_CONFIGURATION_MODE_MANUAL;
      uint16_t next = (uint16_t) ((current & ~(PACKED_CONFIGURATION_MODE_MASK | PACKED_CONFIGURATION_SCHED_ENA_BIT)) | (new_bits & PACKED_CONFIGURATION_MODE_MASK));
      ESP_LOGW(TAG, "WM fallback: PACKED_CONFIGURATION ch=%u cur=0x%04X next=0x%04X (cleared SCHED_ENA)", (unsigned) channel, (unsigned) current, (unsigned) next);
      ok = this->write_register(CAT_PACKED, page, PACKED_CONFIGURATION, next);
  // No alternate OFF attempt to avoid special thermostat modes
    } else {
      ESP_LOGW(TAG, "WM fallback: read PACKED_CONFIGURATION failed for ch=%u", (unsigned) channel);
    }
  }
  if (ok) {
    this->channels_[channel].mode = (mode == climate::CLIMATE_MODE_OFF) ? climate::CLIMATE_MODE_OFF : climate::CLIMATE_MODE_HEAT;
    this->urgent_channels_.push_back(channel);
    this->suspend_polling_until_ = millis() + 100; // 100 ms guard
  } else {
    ESP_LOGW(TAG, "Mode write failed for ch=%u", (unsigned) channel);
  }
}

void WavinAHC9000::write_channel_child_lock(uint8_t channel, bool enable) {
  if (channel < 1 || channel > 16) return;
  uint8_t page = (uint8_t) (channel - 1);
  std::vector<uint16_t> regs;
  if (!this->read_registers(CAT_PACKED, page, PACKED_CONFIGURATION, 1, regs) || regs.size() < 1) {
    ESP_LOGW(TAG, "Child lock: read current config failed ch=%u", (unsigned) channel);
    return;
  }
  uint16_t current = regs[0];
  uint16_t next;
  if (enable)
    next = (uint16_t) (current | PACKED_CONFIGURATION_CHILD_LOCK_MASK);
  else
    next = (uint16_t) (current & ~PACKED_CONFIGURATION_CHILD_LOCK_MASK);
  if (next == current) {
    ESP_LOGD(TAG, "Child lock: no change ch=%u (enable=%s)", (unsigned) channel, enable?"true":"false");
    return;
  }
  if (this->write_register(CAT_PACKED, page, PACKED_CONFIGURATION, next)) {
    this->channels_[channel].child_lock = enable;
    this->urgent_channels_.push_back(channel);
    this->suspend_polling_until_ = millis() + 100;
    ESP_LOGI(TAG, "Child lock: set ch=%u -> %s (0x%04X)", (unsigned) channel, enable?"ENABLED":"DISABLED", (unsigned) next);
  } else {
    ESP_LOGW(TAG, "Child lock: write failed ch=%u", (unsigned) channel);
  }
}

void WavinAHC9000::write_group_child_lock(const std::vector<uint8_t> &members, bool enable) {
  for (auto ch : members) this->write_channel_child_lock(ch, enable);
}

void WavinAHC9000::write_channel_floor_min_temperature(uint8_t channel, float celsius) {
  if (channel < 1 || channel > 16) return;
  // Clamp to a sane range; controller likely enforces further constraints
  if (celsius < 5.0f) celsius = 5.0f;
  if (celsius > 35.0f) celsius = 35.0f;
  uint8_t page = (uint8_t) (channel - 1);
  uint16_t raw = this->c_to_raw(celsius);
  if (this->write_register(CAT_PACKED, page, PACKED_FLOOR_MIN_TEMPERATURE, raw)) {
    this->channels_[channel].floor_min_c = celsius;
    this->urgent_channels_.push_back(channel);
    this->suspend_polling_until_ = millis() + 100;
  }
}

void WavinAHC9000::write_channel_floor_max_temperature(uint8_t channel, float celsius) {
  if (channel < 1 || channel > 16) return;
  if (celsius < 5.0f) celsius = 5.0f;
  if (celsius > 35.0f) celsius = 35.0f;
  uint8_t page = (uint8_t) (channel - 1);
  uint16_t raw = this->c_to_raw(celsius);
  if (this->write_register(CAT_PACKED, page, PACKED_FLOOR_MAX_TEMPERATURE, raw)) {
    this->channels_[channel].floor_max_c = celsius;
    this->urgent_channels_.push_back(channel);
    this->suspend_polling_until_ = millis() + 100;
  }
}

void WavinAHC9000::write_channel_hysteresis(uint8_t channel, float celsius) {
  if (channel < 1 || channel > 16) return;
  // Clamp hysteresis to reasonable range using defined constants
  if (celsius < HYSTERESIS_MIN) celsius = HYSTERESIS_MIN;
  if (celsius > HYSTERESIS_MAX) celsius = HYSTERESIS_MAX;
  uint8_t page = (uint8_t) (channel - 1);
  uint16_t raw = this->c_to_raw(celsius);
  if (this->write_register(CAT_PACKED, page, PACKED_HYSTERESIS, raw)) {
    ESP_LOGI(TAG, "Hysteresis written to thermostat: ch=%u value=%.1f°C (raw=0x%04X)", 
             (unsigned) channel, celsius, (unsigned) raw);
    this->urgent_channels_.push_back(channel);
    this->suspend_polling_until_ = millis() + 100;
  } else {
    ESP_LOGW(TAG, "Failed to write hysteresis to thermostat: ch=%u", (unsigned) channel);
  }
}

void WavinAHC9000::write_channel_comfort_temperature(uint8_t channel, float celsius) {
  if (channel < 1 || channel > 16) return;
  // Clamp to range 6-40°C as specified
  if (celsius < 6.0f) celsius = 6.0f;
  if (celsius > 40.0f) celsius = 40.0f;
  uint8_t page = (uint8_t) (channel - 1);
  uint16_t raw = this->c_to_raw(celsius);
  if (this->write_register(CAT_PACKED, page, PACKED_COMFORT_TEMPERATURE, raw)) {
    ESP_LOGI(TAG, "Comfort temperature written to thermostat: ch=%u value=%.1f°C (raw=0x%04X)", 
             (unsigned) channel, celsius, (unsigned) raw);
    this->urgent_channels_.push_back(channel);
    this->suspend_polling_until_ = millis() + 100;
  } else {
    ESP_LOGW(TAG, "Failed to write comfort temperature to thermostat: ch=%u", (unsigned) channel);
  }
}

void WavinAHC9000::write_channel_eco_temperature(uint8_t channel, float celsius) {
  if (channel < 1 || channel > 16) return;
  // Clamp to range 6-40°C as specified
  if (celsius < 6.0f) celsius = 6.0f;
  if (celsius > 40.0f) celsius = 40.0f;
  uint8_t page = (uint8_t) (channel - 1);
  uint16_t raw = this->c_to_raw(celsius);
  if (this->write_register(CAT_PACKED, page, PACKED_ECO_TEMPERATURE, raw)) {
    ESP_LOGI(TAG, "Eco temperature written to thermostat: ch=%u value=%.1f°C (raw=0x%04X)", 
             (unsigned) channel, celsius, (unsigned) raw);
    this->urgent_channels_.push_back(channel);
    this->suspend_polling_until_ = millis() + 100;
  } else {
    ESP_LOGW(TAG, "Failed to write eco temperature to thermostat: ch=%u", (unsigned) channel);
  }
}

void WavinAHC9000::set_strict_mode_write(uint8_t channel, bool enable) {
  if (channel < 1 || channel > 16) return;
  if (enable) this->strict_mode_channels_.insert(channel);
  else this->strict_mode_channels_.erase(channel);
}
bool WavinAHC9000::is_strict_mode_write(uint8_t channel) const {
  return this->strict_mode_channels_.find(channel) != this->strict_mode_channels_.end();
}

void WavinAHC9000::refresh_channel_now(uint8_t channel) {
  if (channel < 1 || channel > 16) return;
  // Just schedule urgent refresh; actual reads happen in update()
  this->urgent_channels_.push_back(channel);
}

void WavinAHC9000::normalize_channel_config(uint8_t channel, bool off) {
  if (channel < 1 || channel > 16) return;
  uint8_t page = (uint8_t) (channel - 1);
  // Force PACKED_CONFIGURATION to exact baseline used by healthy channels
  // Clear SCHED_ENA to ensure manual control mode persists
  uint16_t value = (uint16_t) (0x4000 | (off ? PACKED_CONFIGURATION_MODE_STANDBY : PACKED_CONFIGURATION_MODE_MANUAL));
  if (this->write_register(CAT_PACKED, page, PACKED_CONFIGURATION, value)) {
    ESP_LOGW(TAG, "Normalize (strict) applied: ch=%u -> 0x%04X (cleared SCHED_ENA)", (unsigned) channel, (unsigned) value);
    this->urgent_channels_.push_back(channel);
    this->suspend_polling_until_ = millis() + 100;
  } else {
    ESP_LOGW(TAG, "Normalize (strict) failed: write not acknowledged for ch=%u", (unsigned) channel);
  }
}

void WavinAHC9000::generate_yaml_suggestion() {
  // One-shot discovery sweep: detect active channels immediately (independent of background polling)
  std::vector<uint8_t> active;
  active.reserve(16);
  // Map primary element index -> list of channels sharing it (for group climate suggestions)
  std::map<uint16_t, std::vector<uint8_t>> primary_groups;
  // Group detection rationale:
  // If multiple channels report the same primary element index they physically share the same thermostat.
  // We propose an optional aggregate climate entity using `members: [a, b, ...]` so users can control
  // all loops for that room with a single setpoint/mode. We keep single-channel suggestions as well
  // so they can choose either approach. Naming uses a compact pattern:
  //   - Exactly two channels:  Zone G <a>&<b>
  //   - More than two:        Zone G <first>-<last>
  // Users can rename afterwards; we avoid including 'Primary' or raw element index to keep it friendly.
  std::vector<uint16_t> regs;
  for (uint8_t ch = 1; ch <= 16; ch++) {
    uint8_t page = (uint8_t) (ch - 1);
    if (this->read_registers(CAT_CHANNELS, page, CH_PRIMARY_ELEMENT, 1, regs) && regs.size() >= 1) {
      uint16_t v = regs[0];
      uint16_t primary_index = v & CH_PRIMARY_ELEMENT_ELEMENT_MASK;
      bool all_tp_lost = (v & CH_PRIMARY_ELEMENT_ALL_TP_LOST_MASK) != 0;
      if (primary_index > 0 && !all_tp_lost) {
        active.push_back(ch);
        this->yaml_primary_present_mask_ |= (1u << (ch - 1));
        // Opportunistically fill cache (does not change behavior)
        auto &st = this->channels_[ch];
        st.primary_index = primary_index;
        st.all_tp_lost = all_tp_lost;
        if (primary_index > 0) {
          primary_groups[primary_index].push_back(ch);
        }
        // Read basic mode + setpoint so climates look sensible in cache
        if (this->read_registers(CAT_PACKED, page, PACKED_CONFIGURATION, 1, regs) && regs.size() >= 1) {
          uint16_t raw_cfg = regs[0];
          uint16_t mode_bits = raw_cfg & PACKED_CONFIGURATION_MODE_MASK;
          // Only MODE=001 is permanent standby per Wavin spec (treat as OFF in Home Assistant)
          bool is_off = (mode_bits == PACKED_CONFIGURATION_MODE_STANDBY);
          st.mode = is_off ? climate::CLIMATE_MODE_OFF : climate::CLIMATE_MODE_HEAT;
          st.child_lock = (raw_cfg & PACKED_CONFIGURATION_CHILD_LOCK_MASK) != 0;
        }
        if (this->read_registers(CAT_PACKED, page, PACKED_MANUAL_TEMPERATURE, 1, regs) && regs.size() >= 1) {
          st.setpoint_c = this->raw_to_c(regs[0]);
        }
        // Floor min/max (read-only) combined into one read (contiguous indices)
        if (this->read_registers(CAT_PACKED, page, PACKED_FLOOR_MIN_TEMPERATURE, 2, regs) && regs.size() >= 2) {
          st.floor_min_c = this->raw_to_c(regs[0]);
          st.floor_max_c = this->raw_to_c(regs[1]);
        }
        // Try to read elements block to surface air/floor temps and detect floor probe immediately
        uint8_t elem_page = (uint8_t) (primary_index - 1);
        if (this->read_registers(CAT_ELEMENTS, elem_page, 0x00, 12, regs) && regs.size() > ELEM_AIR_TEMPERATURE) {
          st.current_temp_c = this->raw_to_c(regs[ELEM_AIR_TEMPERATURE]);
          this->yaml_elem_read_mask_ |= (1u << (ch - 1));
          if (regs.size() > ELEM_FLOOR_TEMPERATURE) {
            float ft = this->raw_to_c(regs[ELEM_FLOOR_TEMPERATURE]);
            if (ft > 1.0f && ft < 90.0f) {
              st.floor_temp_c = ft;
              bool threshold_hit = (ft >= 15.0f);
              bool deviates = (!std::isnan(st.current_temp_c) && std::fabs(st.current_temp_c - ft) > 0.2f);
              if (threshold_hit || deviates) st.has_floor_sensor = true;
            } else {
              st.floor_temp_c = NAN;
            }
          }
          // Humidity (optional)
          if (regs.size() > ELEM_HUMIDITY) {
            st.humidity_pct = this->parse_humidity(regs[ELEM_HUMIDITY]);
          }
          // Battery (optional, 0-9 scale where 9=100%)
          if (regs.size() > ELEM_BATTERY_STATUS) {
            uint16_t raw = regs[ELEM_BATTERY_STATUS];
            uint8_t steps = (raw > 9) ? 9 : (uint8_t) raw;
            // Map 0-9 scale to 0-100%: pct = (steps * 100) / 9 with proper rounding
            st.battery_pct = (uint8_t) ((steps * 100 + 5) / 9);
          }
        }
      }
    }
  }

  // Persist active channels for chunk helpers
  this->yaml_active_channels_ = active;
  // For now, propose child lock switches for all active channels (user can trim later)
  this->yaml_child_lock_channels_ = active;

  // Build YAML sections; determine grouped channels first so we can comment out their single climates
  std::string yaml_climate;
  yaml_climate += "climate:\n";
  this->yaml_grouped_channels_.clear();

  // Group climates: for any primary element shared by >1 channel, propose a members-based climate.
  // Name strategy: "Zone G <first>-<last>" or if exactly 2 channels "Zone G <a>&<b>".
  std::string yaml_group_climate;
  bool any_group = false;
  this->yaml_group_climate_groups_.clear();
  for (auto &kv : primary_groups) {
    const auto &chs = kv.second;
    if (chs.size() <= 1) continue;
    std::vector<uint8_t> sorted = chs;
    std::sort(sorted.begin(), sorted.end());
    if (!any_group) {
      yaml_group_climate += "climate:\n";
      any_group = true;
    }
    // Save for chunk helper
    this->yaml_group_climate_groups_.push_back(sorted);
    std::string name;
    // If all members have friendly names, build a composite
    bool all_named = true;
    std::vector<std::string> member_names;
    for (auto ch : sorted) {
      auto fn = this->get_channel_friendly_name(ch);
      if (fn.empty()) { all_named = false; break; }
      member_names.push_back(fn);
    }
    if (all_named && !member_names.empty()) {
      if (member_names.size() == 2) {
        name = member_names[0] + " & " + member_names[1];
      } else if (member_names.size() <= 4) {
        // Join with commas and ' & ' before last for readability
        for (size_t i = 0; i < member_names.size(); ++i) {
          if (i > 0) name += (i + 1 == member_names.size() ? " & " : ", ");
          name += member_names[i];
        }
      } else {
        // Too many to list: First - Last pattern
        name = member_names.front() + " – " + member_names.back();
      }
    } else {
      if (sorted.size() == 2) name = "Zone G " + std::to_string((int) sorted[0]) + "&" + std::to_string((int) sorted[1]);
      else name = "Zone G " + std::to_string((int) sorted.front()) + "-" + std::to_string((int) sorted.back());
    }
    yaml_group_climate += "  - platform: wavin_ahc9000\n";
    yaml_group_climate += "    wavin_ahc9000_id: wavin\n";
    yaml_group_climate += "    name: \"" + name + "\"\n";
    yaml_group_climate += "    members: [";
    for (size_t i = 0; i < sorted.size(); i++) {
      yaml_group_climate += std::to_string((int) sorted[i]);
      if (i + 1 < sorted.size()) yaml_group_climate += ", ";
      // Mark channel as grouped
      this->yaml_grouped_channels_.insert(sorted[i]);
    }
    yaml_group_climate += "]\n";
  }

  // Now append single climates (comment out those that are grouped)
  for (auto ch : active) {
    std::string fname = this->get_channel_friendly_name(ch);
    if (fname.empty()) fname = "Zone " + std::to_string((int) ch);
    bool grouped = this->yaml_grouped_channels_.count(ch) != 0;
    std::string prefix = grouped ? "  #" : "  ";
    yaml_climate += prefix + " - platform: wavin_ahc9000\n";
    yaml_climate += prefix + "   wavin_ahc9000_id: wavin\n";
    yaml_climate += prefix + "   name: \"" + fname + "\"\n";
    yaml_climate += prefix + "   channel: " + std::to_string((int) ch) + "\n";
    if (grouped) yaml_climate += prefix + "   # Commented out because channel participates in a group climate above.\n";
  }

  // Comfort climates (floor-based current temp) for channels with detected floor sensor
  std::string yaml_comfort_climate;
  bool any_comfort = false;
  this->yaml_comfort_climate_channels_.clear();
  for (auto ch : active) {
    auto it = this->channels_.find(ch);
    if (it != this->channels_.end() && it->second.has_floor_sensor) {
      if (!any_comfort) {
        yaml_comfort_climate += "climate:\n";
        any_comfort = true;
      }
      std::string fname = this->get_channel_friendly_name(ch);
      if (fname.empty()) fname = "Zone " + std::to_string((int) ch);
      yaml_comfort_climate += "  - platform: wavin_ahc9000\n";
      yaml_comfort_climate += "    wavin_ahc9000_id: wavin\n";
      yaml_comfort_climate += "    name: \"" + fname + " Comfort\"\n";
      yaml_comfort_climate += "    channel: " + std::to_string((int) ch) + "\n";
      yaml_comfort_climate += "    use_floor_temperature: true\n";
      this->yaml_comfort_climate_channels_.push_back(ch);
    }
  }  // end for(active) comfort climates loop
  std::string yaml_batt;
  yaml_batt += "sensor:\n";
  for (auto ch : active) {
    std::string fname = this->get_channel_friendly_name(ch);
    if (fname.empty()) fname = "Zone " + std::to_string((int) ch);
    yaml_batt += "  - platform: wavin_ahc9000\n";
    yaml_batt += "    wavin_ahc9000_id: wavin\n";
    yaml_batt += "    name: \"" + fname + " Battery\"\n";
    yaml_batt += "    channel: " + std::to_string((int) ch) + "\n";
    yaml_batt += "    type: battery\n";
  }

  std::string yaml_temp;
  yaml_temp += "sensor:\n";
  for (auto ch : active) {
    std::string fname = this->get_channel_friendly_name(ch);
    if (fname.empty()) fname = "Zone " + std::to_string((int) ch);
    yaml_temp += "  - platform: wavin_ahc9000\n";
    yaml_temp += "    wavin_ahc9000_id: wavin\n";
    yaml_temp += "    name: \"" + fname + " Temperature\"\n";
    yaml_temp += "    channel: " + std::to_string((int) ch) + "\n";
    yaml_temp += "    type: temperature\n";
  }

  // Floor temperature / floor limit sensors omitted per new scope

  std::string out = yaml_climate;
  if (any_group) out += "\n" + yaml_group_climate;
  out += "\n" + yaml_batt + "\n" + yaml_temp;
  if (any_comfort) out += "\n" + yaml_comfort_climate;

  // Build cached floor channel list for comfort chunk helpers (just channels with floor sensors)
  this->yaml_floor_channels_.clear();
  for (auto ch : active) {
    auto it = this->channels_.find(ch);
    if (it != this->channels_.end() && it->second.has_floor_sensor) this->yaml_floor_channels_.push_back(ch);
  }

  // Save last YAML and publish to optional text sensor (HA may truncate state >255 chars)
  this->yaml_last_suggestion_ = out;
  this->yaml_last_climate_ = yaml_climate;
  this->yaml_last_battery_ = yaml_batt;
  this->yaml_last_temperature_ = yaml_temp;
  this->yaml_last_floor_temperature_.clear();
  this->yaml_last_group_climate_ = yaml_group_climate;


  // Also print with banners (and ANSI color if viewer supports it)
  const char *CYAN = "\x1b[36m";
  const char *GREEN = "\x1b[32m";
  const char *RESET = "\x1b[0m";
  ESP_LOGI(TAG, "%s==================== Wavin YAML SUGGESTION BEGIN ====================%s", CYAN, RESET);
  {
    // Print line by line to avoid single-message truncation in logger
    const char *p = out.c_str();
    const char *line_start = p;
    while (*p) {
      if (*p == '\n') {
        std::string line(line_start, p - line_start);
        ESP_LOGI(TAG, "%s%s%s", GREEN, line.c_str(), RESET);
        ++p;
        line_start = p;
      } else {
        ++p;
      }
    }
    // Last line if not newline-terminated
    if (line_start != p) {
      std::string line(line_start, p - line_start);
      ESP_LOGI(TAG, "%s%s%s", GREEN, line.c_str(), RESET);
    }
  }
  ESP_LOGI(TAG, "%s===================== Wavin YAML SUGGESTION END =====================%s", CYAN, RESET);
}

// --- YAML chunk helpers (whole-entity, not byte size) ---
static std::string build_climate_yaml_for(const WavinAHC9000 *parent, const std::vector<uint8_t> &chs) {
  // Return only entity blocks, no leading 'climate:' header
  std::string y;
  if (chs.empty()) return y;
  for (auto ch : chs) {
    std::string fname = parent->get_channel_friendly_name(ch);
    if (fname.empty()) fname = "Zone " + std::to_string((int) ch);
    y += "- platform: wavin_ahc9000\n";
    y += "  wavin_ahc9000_id: wavin\n";
    y += "  name: \"" + fname + "\"\n";
    y += "  channel: " + std::to_string((int) ch) + "\n";
  }
  return y;
}
static std::string build_battery_yaml_for(const WavinAHC9000 *parent, const std::vector<uint8_t> &chs) {
  // Return only entity blocks, no leading 'sensor:' header
  std::string y;
  if (chs.empty()) return y;
  for (auto ch : chs) {
    std::string fname = parent->get_channel_friendly_name(ch);
    if (fname.empty()) fname = "Zone " + std::to_string((int) ch);
    y += "- platform: wavin_ahc9000\n";
    y += "  wavin_ahc9000_id: wavin\n";
    y += "  name: \"" + fname + " Battery\"\n";
    y += "  channel: " + std::to_string((int) ch) + "\n";
    y += "  type: battery\n";
  }
  return y;
}
static std::string build_temperature_yaml_for(const WavinAHC9000 *parent, const std::vector<uint8_t> &chs) {
  // Return only entity blocks, no leading 'sensor:' header
  std::string y;
  if (chs.empty()) return y;
  for (auto ch : chs) {
    std::string fname = parent->get_channel_friendly_name(ch);
    if (fname.empty()) fname = "Zone " + std::to_string((int) ch);
    y += "- platform: wavin_ahc9000\n";
    y += "  wavin_ahc9000_id: wavin\n";
    y += "  name: \"" + fname + " Temperature\"\n";
    y += "  channel: " + std::to_string((int) ch) + "\n";
    y += "  type: temperature\n";
  }
  return y;
}
static std::string build_floor_temperature_yaml_for(const WavinAHC9000 *parent, const std::vector<uint8_t> &chs) {
  std::string y;
  if (chs.empty()) return y;
  for (auto ch : chs) {
    std::string fname = parent->get_channel_friendly_name(ch);
    if (fname.empty()) fname = "Zone " + std::to_string((int) ch);
    y += "- platform: wavin_ahc9000\n";
    y += "  wavin_ahc9000_id: wavin\n";
    y += "  name: \"" + fname + " Floor Temperature\"\n";
    y += "  channel: " + std::to_string((int) ch) + "\n";
    y += "  type: floor_temperature\n";
  }
  return y;
}
static std::string build_floor_min_temperature_yaml_for(const WavinAHC9000 *parent, const std::vector<uint8_t> &chs) {
  std::string y;
  if (chs.empty()) return y;
  for (auto ch : chs) {
    std::string fname = parent->get_channel_friendly_name(ch);
    if (fname.empty()) fname = "Zone " + std::to_string((int) ch);
    y += "- platform: wavin_ahc9000\n";
    y += "  wavin_ahc9000_id: wavin\n";
    y += "  name: \"" + fname + " Floor Min Temperature\"\n";
    y += "  channel: " + std::to_string((int) ch) + "\n";
    y += "  type: floor_min_temperature\n";
  }
  return y;
}
static std::string build_floor_max_temperature_yaml_for(const WavinAHC9000 *parent, const std::vector<uint8_t> &chs) {
  std::string y;
  if (chs.empty()) return y;
  for (auto ch : chs) {
    std::string fname = parent->get_channel_friendly_name(ch);
    if (fname.empty()) fname = "Zone " + std::to_string((int) ch);
    y += "- platform: wavin_ahc9000\n";
    y += "  wavin_ahc9000_id: wavin\n";
    y += "  name: \"" + fname + " Floor Max Temperature\"\n";
    y += "  channel: " + std::to_string((int) ch) + "\n";
    y += "  type: floor_max_temperature\n";
  }
  return y;
}

static std::string build_child_lock_yaml_for(const WavinAHC9000 *parent, const std::vector<uint8_t> &chs) {
  std::string y;
  if (chs.empty()) return y;
  for (auto ch : chs) {
    std::string fname = parent->get_channel_friendly_name(ch);
    if (fname.empty()) fname = "Zone " + std::to_string((int) ch);
    y += "- platform: wavin_ahc9000\n";
    y += "  wavin_ahc9000_id: wavin\n";
    y += "  name: \"" + fname + " Lock\"\n";
    y += "  channel: " + std::to_string((int) ch) + "\n";
    // type defaults to child_lock, so we omit for brevity
  }
  return y;
}

static std::string build_group_climate_yaml_for(const WavinAHC9000 *parent, const std::vector<std::vector<uint8_t>> &groups) {
  std::string y;
  for (auto &g : groups) {
    if (g.empty()) continue;
    std::string name;
    bool all_named = true;
    std::vector<std::string> member_names;
    for (auto ch : g) {
      auto fn = parent->get_channel_friendly_name(ch);
      if (fn.empty()) { all_named = false; break; }
      member_names.push_back(fn);
    }
    if (all_named && !member_names.empty()) {
      if (member_names.size() == 2) {
        name = member_names[0] + " & " + member_names[1];
      } else if (member_names.size() <= 4) {
        for (size_t i = 0; i < member_names.size(); ++i) {
          if (i > 0) name += (i + 1 == member_names.size() ? " & " : ", ");
          name += member_names[i];
        }
      } else {
        name = member_names.front() + " – " + member_names.back();
      }
    } else {
      if (g.size() == 2) name = "Zone G " + std::to_string((int) g[0]) + "&" + std::to_string((int) g[1]);
      else name = "Zone G " + std::to_string((int) g.front()) + "-" + std::to_string((int) g.back());
    }
    y += "- platform: wavin_ahc9000\n";
    y += "  wavin_ahc9000_id: wavin\n";
    y += "  name: \"" + name + "\"\n";
    y += "  members: [";
    for (size_t i = 0; i < g.size(); i++) {
      y += std::to_string((int) g[i]);
      if (i + 1 < g.size()) y += ", ";
    }
    y += "]\n";
  }
  return y;
}

std::string WavinAHC9000::get_yaml_climate_chunk(uint8_t start, uint8_t count) const {
  if (start >= this->yaml_active_channels_.size() || count == 0) return std::string("");
  uint8_t end = (uint8_t) std::min<size_t>(this->yaml_active_channels_.size(), (size_t) start + count);
  std::vector<uint8_t> chs(this->yaml_active_channels_.begin() + start, this->yaml_active_channels_.begin() + end);
  return build_climate_yaml_for(this, chs);
}
std::string WavinAHC9000::get_yaml_battery_chunk(uint8_t start, uint8_t count) const {
  if (start >= this->yaml_active_channels_.size() || count == 0) return std::string("");
  uint8_t end = (uint8_t) std::min<size_t>(this->yaml_active_channels_.size(), (size_t) start + count);
  std::vector<uint8_t> chs(this->yaml_active_channels_.begin() + start, this->yaml_active_channels_.begin() + end);
  return build_battery_yaml_for(this, chs);
}
std::string WavinAHC9000::get_yaml_temperature_chunk(uint8_t start, uint8_t count) const {
  if (start >= this->yaml_active_channels_.size() || count == 0) return std::string("");
  uint8_t end = (uint8_t) std::min<size_t>(this->yaml_active_channels_.size(), (size_t) start + count);
  std::vector<uint8_t> chs(this->yaml_active_channels_.begin() + start, this->yaml_active_channels_.begin() + end);
  return build_temperature_yaml_for(this, chs);
}
static std::string build_comfort_climate_yaml_for(const WavinAHC9000 *parent, const std::vector<uint8_t> &chs) {
  std::string y;
  if (chs.empty()) return y;
  for (auto ch : chs) {
    std::string fname = parent->get_channel_friendly_name(ch);
    if (fname.empty()) fname = "Zone " + std::to_string((int) ch);
    y += "- platform: wavin_ahc9000\n";
    y += "  wavin_ahc9000_id: wavin\n";
    y += "  name: \"" + fname + " Comfort\"\n";
    y += "  channel: " + std::to_string((int) ch) + "\n";
    y += "  use_floor_temperature: true\n";
  }
  return y;
}
std::string WavinAHC9000::get_yaml_comfort_climate_chunk(uint8_t start, uint8_t count) const {
  if (start >= this->yaml_comfort_climate_channels_.size() || count == 0) return std::string("");
  uint8_t end = (uint8_t) std::min<size_t>(this->yaml_comfort_climate_channels_.size(), (size_t) start + count);
  std::vector<uint8_t> chs(this->yaml_comfort_climate_channels_.begin() + start, this->yaml_comfort_climate_channels_.begin() + end);
  return build_comfort_climate_yaml_for(this, chs);
}
std::string WavinAHC9000::get_yaml_floor_temperature_chunk(uint8_t start, uint8_t count) const {
  if (start >= this->yaml_floor_channels_.size() || count == 0) return std::string("");
  uint8_t end = (uint8_t) std::min<size_t>(this->yaml_floor_channels_.size(), (size_t) start + count);
  std::vector<uint8_t> chs(this->yaml_floor_channels_.begin() + start, this->yaml_floor_channels_.begin() + end);
  return build_floor_temperature_yaml_for(this, chs);
}
std::string WavinAHC9000::get_yaml_floor_min_temperature_chunk(uint8_t start, uint8_t count) const {
  if (start >= this->yaml_floor_channels_.size() || count == 0) return std::string("");
  uint8_t end = (uint8_t) std::min<size_t>(this->yaml_floor_channels_.size(), (size_t) start + count);
  std::vector<uint8_t> chs(this->yaml_floor_channels_.begin() + start, this->yaml_floor_channels_.begin() + end);
  return build_floor_min_temperature_yaml_for(this, chs);
}
std::string WavinAHC9000::get_yaml_floor_max_temperature_chunk(uint8_t start, uint8_t count) const {
  if (start >= this->yaml_floor_channels_.size() || count == 0) return std::string("");
  uint8_t end = (uint8_t) std::min<size_t>(this->yaml_floor_channels_.size(), (size_t) start + count);
  std::vector<uint8_t> chs(this->yaml_floor_channels_.begin() + start, this->yaml_floor_channels_.begin() + end);
  return build_floor_max_temperature_yaml_for(this, chs);
}

std::string WavinAHC9000::get_yaml_group_climate_chunk(uint8_t start, uint8_t count) const {
  if (start >= this->yaml_group_climate_groups_.size() || count == 0) return std::string("");
  uint8_t end = (uint8_t) std::min<size_t>(this->yaml_group_climate_groups_.size(), (size_t) start + count);
  std::vector<std::vector<uint8_t>> slice(this->yaml_group_climate_groups_.begin() + start, this->yaml_group_climate_groups_.begin() + end);
  return build_group_climate_yaml_for(this, slice);
}
std::string WavinAHC9000::get_yaml_child_lock_chunk(uint8_t start, uint8_t count) const {
  if (start >= this->yaml_child_lock_channels_.size() || count == 0) return std::string("");
  uint8_t end = (uint8_t) std::min<size_t>(this->yaml_child_lock_channels_.size(), (size_t) start + count);
  std::vector<uint8_t> slice(this->yaml_child_lock_channels_.begin() + start, this->yaml_child_lock_channels_.begin() + end);
  return build_child_lock_yaml_for(this, slice);
}

void WavinAHC9000::sync_hysteresis_to_group(uint8_t changed_channel, float new_value) {
  // Get all sibling channels (other members of the same groups)
  std::set<uint8_t> siblings = this->get_group_sibling_channels(changed_channel);
  
  if (siblings.empty()) {
    return;  // No siblings to sync to
  }
  
  // Log the sync operation
  std::string sibling_list;
  for (uint8_t sibling : siblings) {
    if (!sibling_list.empty()) sibling_list += ", ";
    sibling_list += std::to_string(sibling);
  }
  ESP_LOGI(TAG, "Syncing hysteresis %.1f°C from CH%u to sibling channel(s): %s", 
           new_value, (unsigned) changed_channel, sibling_list.c_str());
  
  // Write to all sibling channels
  for (uint8_t sibling : siblings) {
    this->write_channel_hysteresis(sibling, new_value);
    // Update the tracking value to prevent re-triggering
    auto &st = this->channels_[sibling];
    st.prev_hysteresis_c = new_value;
  }
  
  // Update all hysteresis number entities that include any of the sibling channels
  for (auto *num : this->hysteresis_numbers_) {
    if (num == nullptr) continue;
    // Check if this number entity controls any of the affected channels
    const auto &members = num->get_members();
    for (uint8_t member : members) {
      if (siblings.count(member) > 0) {
        // This number entity controls at least one affected channel, update its UI
        num->publish_state(new_value);
        ESP_LOGD(TAG, "Updated hysteresis number entity '%s' to %.1f°C", num->get_name().c_str(), new_value);
        break;  // Only update each number entity once
      }
    }
  }
}

void WavinAHC9000::sync_eco_temp_to_group(uint8_t changed_channel, float new_value) {
  // Get all sibling channels (other members of the same groups)
  std::set<uint8_t> siblings = this->get_group_sibling_channels(changed_channel);
  
  if (siblings.empty()) {
    return;  // No siblings to sync to
  }
  
  // Log the sync operation
  std::string sibling_list;
  for (uint8_t sibling : siblings) {
    if (!sibling_list.empty()) sibling_list += ", ";
    sibling_list += std::to_string(sibling);
  }
  ESP_LOGI(TAG, "Syncing eco temp %.1f°C from CH%u to sibling channel(s): %s", 
           new_value, (unsigned) changed_channel, sibling_list.c_str());
  
  // Write to all sibling channels
  for (uint8_t sibling : siblings) {
    this->write_channel_eco_temperature(sibling, new_value);
    // Update the tracking value to prevent re-triggering
    auto &st = this->channels_[sibling];
    st.prev_eco_temp_c = new_value;
  }
  
  // Update all temp_low number entities that include any of the sibling channels
  for (auto *num : this->temp_low_numbers_) {
    if (num == nullptr) continue;
    // Check if this number entity controls any of the affected channels
    const auto &members = num->get_members();
    for (uint8_t member : members) {
      if (siblings.count(member) > 0) {
        // This number entity controls at least one affected channel, update its UI
        num->publish_state(new_value);
        ESP_LOGD(TAG, "Updated temp_low number entity '%s' to %.1f°C", num->get_name().c_str(), new_value);
        break;  // Only update each number entity once
      }
    }
  }
}

void WavinAHC9000::sync_comfort_temp_to_group(uint8_t changed_channel, float new_value) {
  // Get all sibling channels (other members of the same groups)
  std::set<uint8_t> siblings = this->get_group_sibling_channels(changed_channel);
  
  if (siblings.empty()) {
    return;  // No siblings to sync to
  }
  
  // Log the sync operation
  std::string sibling_list;
  for (uint8_t sibling : siblings) {
    if (!sibling_list.empty()) sibling_list += ", ";
    sibling_list += std::to_string(sibling);
  }
  ESP_LOGI(TAG, "Syncing comfort temp %.1f°C from CH%u to sibling channel(s): %s", 
           new_value, (unsigned) changed_channel, sibling_list.c_str());
  
  // Write to all sibling channels
  for (uint8_t sibling : siblings) {
    this->write_channel_comfort_temperature(sibling, new_value);
    // Update the tracking value to prevent re-triggering
    auto &st = this->channels_[sibling];
    st.prev_comfort_temp_c = new_value;
  }
  
  // Update all temp_high number entities that include any of the sibling channels
  for (auto *num : this->temp_high_numbers_) {
    if (num == nullptr) continue;
    // Check if this number entity controls any of the affected channels
    const auto &members = num->get_members();
    for (uint8_t member : members) {
      if (siblings.count(member) > 0) {
        // This number entity controls at least one affected channel, update its UI
        num->publish_state(new_value);
        ESP_LOGD(TAG, "Updated temp_high number entity '%s' to %.1f°C", num->get_name().c_str(), new_value);
        break;  // Only update each number entity once
      }
    }
  }
}

void WavinAHC9000::publish_updates() {
  ESP_LOGV(TAG, "Publishing updates: %u single climates, %u group climates",
           (unsigned) this->single_ch_climates_.size(), (unsigned) this->group_climates_.size());
  for (auto *c : this->single_ch_climates_) c->update_from_parent();
  for (auto *c : this->group_climates_) c->update_from_parent();
  // Channel sensors
  for (auto &kv : this->temperature_sensors_) {
    uint8_t ch = kv.first;
    auto *s = kv.second;
    if (!s) continue;
    float v = this->get_channel_current_temp(ch);
    if (!std::isnan(v)) s->publish_state(v);
  }
  for (auto &kv : this->battery_sensors_) {
    uint8_t ch = kv.first;
    auto *s = kv.second;
    if (!s) continue;
    auto it = this->channels_.find(ch);
    if (it != this->channels_.end() && it->second.battery_pct != 255) {
      s->publish_state((float) it->second.battery_pct);
    }
  }
  for (auto &kv : this->floor_temperature_sensors_) {
    uint8_t ch = kv.first;
    auto *s = kv.second;
    if (!s) continue;
    auto it = this->channels_.find(ch);
    if (it != this->channels_.end() && it->second.has_floor_sensor) {
      float v = it->second.floor_temp_c;
      if (!std::isnan(v)) s->publish_state(v);
    }
  }
  // Publish floor limit sensors (read-only)
  for (auto &kv : this->floor_min_temperature_sensors_) {
    uint8_t ch = kv.first;
    auto *s = kv.second;
    if (!s) continue;
    auto it = this->channels_.find(ch);
    if (it != this->channels_.end()) {
      float v = it->second.floor_min_c;
      if (!std::isnan(v)) s->publish_state(v);
    }
  }
  for (auto &kv : this->floor_max_temperature_sensors_) {
    uint8_t ch = kv.first;
    auto *s = kv.second;
    if (!s) continue;
    auto it = this->channels_.find(ch);
    if (it != this->channels_.end()) {
      float v = it->second.floor_max_c;
      if (!std::isnan(v)) s->publish_state(v);
    }
  }

  // Child lock switches
  for (auto &kv : this->child_lock_switches_) {
    uint8_t ch = kv.first;
    auto *sw = kv.second;
    if (!sw) continue;
    auto it = this->channels_.find(ch);
    if (it != this->channels_.end()) {
      sw->publish_state(it->second.child_lock);
    }
  }

  // Average temperature sensors
  for (auto &avg_sensor : this->average_temperature_sensors_) {
    if (!avg_sensor.sensor) continue;
    double sum = 0.0;  // Use double for better precision in intermediate calculations
    uint8_t valid_count = 0;
    // Calculate average of member channel temperatures
    for (uint8_t ch : avg_sensor.members) {
      auto it = this->channels_.find(ch);
      if (it != this->channels_.end()) {
        float temp = it->second.current_temp_c;
        if (!std::isnan(temp)) {
          sum += temp;
          valid_count++;
        }
      }
    }
    // Publish average if we have valid temperatures, otherwise publish NaN
    if (valid_count > 0) {
      float average = static_cast<float>(sum / valid_count);
      avg_sensor.sensor->publish_state(average);
    } else {
      // No valid temperatures available; publish NaN to indicate no data
      avg_sensor.sensor->publish_state(NAN);
    }
  }

  // YAML readiness: ready if we have discovered at least one active channel and have completed at least one element read for all of them.
  {
    uint16_t required = this->yaml_primary_present_mask_;
    bool ready = (required != 0) && ((this->yaml_elem_read_mask_ & required) == required);
  }
}

float WavinAHC9000::get_channel_current_temp(uint8_t channel) const {
  auto it = this->channels_.find(channel);
  return it == this->channels_.end() ? NAN : it->second.current_temp_c;
}
float WavinAHC9000::get_channel_setpoint(uint8_t channel) const {
  auto it = this->channels_.find(channel);
  return it == this->channels_.end() ? NAN : it->second.setpoint_c;
}
float WavinAHC9000::get_channel_floor_temp(uint8_t channel) const {
  auto it = this->channels_.find(channel);
  return it == this->channels_.end() ? NAN : it->second.floor_temp_c;
}
float WavinAHC9000::get_channel_floor_min_temp(uint8_t channel) const {
  auto it = this->channels_.find(channel);
  return it == this->channels_.end() ? NAN : it->second.floor_min_c;
}
float WavinAHC9000::get_channel_floor_max_temp(uint8_t channel) const {
  auto it = this->channels_.find(channel);
  return it == this->channels_.end() ? NAN : it->second.floor_max_c;
}
climate::ClimateMode WavinAHC9000::get_channel_mode(uint8_t channel) const {
  auto it = this->channels_.find(channel);
  return it == this->channels_.end() ? climate::CLIMATE_MODE_HEAT : it->second.mode;
}
climate::ClimateAction WavinAHC9000::get_channel_action(uint8_t channel) const {
  auto it = this->channels_.find(channel);
  return it == this->channels_.end() ? climate::CLIMATE_ACTION_OFF : it->second.action;
}

void WavinZoneClimate::dump_config() { LOG_CLIMATE("  ", "Wavin Zone Climate (minimal)", this); }
climate::ClimateTraits WavinZoneClimate::traits() {
  climate::ClimateTraits t;
  t.set_supported_modes({climate::CLIMATE_MODE_HEAT, climate::CLIMATE_MODE_OFF});
  t.add_feature_flags(climate::CLIMATE_SUPPORTS_CURRENT_TEMPERATURE | climate::CLIMATE_SUPPORTS_ACTION);
  // Default visual bounds
  float vmin = 5.0f;
  float vmax = 35.0f;
  // For comfort climates (using floor temperature), adopt current floor min/max when available
  if (this->single_channel_set_ && this->use_floor_temperature_) {
    t.add_feature_flags(climate::CLIMATE_REQUIRES_TWO_POINT_TARGET_TEMPERATURE);
    float fmin = this->parent_->get_channel_floor_min_temp(this->single_channel_);
    float fmax = this->parent_->get_channel_floor_max_temp(this->single_channel_);
    if (!std::isnan(fmin)) vmin = fmin;
    if (!std::isnan(fmax)) vmax = fmax;
  }
  t.set_visual_min_temperature(vmin);
  t.set_visual_max_temperature(vmax);
  t.set_visual_temperature_step(0.5f);
  return t;
}
void WavinZoneClimate::control(const climate::ClimateCall &call) {
  // Mode control
  if (call.get_mode().has_value()) {
    auto m = *call.get_mode();
  ESP_LOGD(TAG, "CTRL: mode=%s for %s", (m == climate::CLIMATE_MODE_OFF ? "OFF" : "HEAT"), this->get_name().c_str());
  if (this->parent_->get_allow_mode_writes()) {
      if (this->single_channel_set_) {
        this->parent_->write_channel_mode(this->single_channel_, m);
      } else if (!this->members_.empty()) {
        for (auto ch : this->members_) this->parent_->write_channel_mode(ch, m);
      }
    } else {
      ESP_LOGW(TAG, "Mode writes disabled by config; skipping write for %s", this->get_name().c_str());
    }
    this->mode = (m == climate::CLIMATE_MODE_OFF) ? climate::CLIMATE_MODE_OFF : climate::CLIMATE_MODE_HEAT;
  }

  // Target temperature
  if (call.get_target_temperature().has_value()) {
    float t = *call.get_target_temperature();
  ESP_LOGD(TAG, "CTRL: target=%.1fC for %s", t, this->get_name().c_str());
    // If we're turning OFF in the same call, skip setpoint write to avoid switching back to MANUAL
    bool turning_off = call.get_mode().has_value() && (*call.get_mode() == climate::CLIMATE_MODE_OFF);
    if (!turning_off) {
      if (this->single_channel_set_) {
        this->parent_->write_channel_setpoint(this->single_channel_, t);
      } else if (!this->members_.empty()) {
        this->parent_->write_group_setpoint(this->members_, t);
      }
    }
    this->target_temperature = t;
  }

  // If comfort climate is used, expose floor min/max through target_temperature_low/high
  if (this->use_floor_temperature_ && this->single_channel_set_) {
    bool has_lo = call.get_target_temperature_low().has_value();
    bool has_hi = call.get_target_temperature_high().has_value();
    float current_lo = this->parent_->get_channel_floor_min_temp(this->single_channel_);
    float current_hi = this->parent_->get_channel_floor_max_temp(this->single_channel_);
    float new_lo = current_lo;
    float new_hi = current_hi;
    if (has_lo) new_lo = *call.get_target_temperature_low();
    if (has_hi) new_hi = *call.get_target_temperature_high();
    auto round05 = [](float v) -> float { return std::round(v * 2.0f) / 2.0f; };
    // Clamp to global sane bounds first, then round to 0.5°C step
    if (!std::isnan(new_lo)) {
      if (new_lo < 5.0f) new_lo = 5.0f;
      if (new_lo > 35.0f) new_lo = 35.0f;
      new_lo = round05(new_lo);
    }
    if (!std::isnan(new_hi)) {
      if (new_hi < 5.0f) new_hi = 5.0f;
      if (new_hi > 35.0f) new_hi = 35.0f;
      new_hi = round05(new_hi);
    }
    // Enforce at least 1.0C separation if both present (or infer using the unchanged side)
    if (!std::isnan(new_lo) && !std::isnan(new_hi)) {
      if (new_hi < new_lo + 1.0f) {
        if (has_hi && !has_lo) {
          new_lo = round05(new_hi - 1.0f);
        } else {
          new_hi = round05(new_lo + 1.0f);
        }
      }
    } else if (!std::isnan(new_lo) && std::isnan(new_hi) && !std::isnan(current_hi)) {
      if (current_hi < new_lo + 1.0f) new_lo = round05(current_hi - 1.0f);
    } else if (!std::isnan(new_hi) && std::isnan(new_lo) && !std::isnan(current_lo)) {
      if (new_hi < current_lo + 1.0f) new_hi = round05(current_lo + 1.0f);
    }
    // Write only the values that actually changed after adjustment
    if (!std::isnan(new_lo) && (std::isnan(current_lo) || std::fabs(new_lo - current_lo) > 0.049f)) {
      ESP_LOGD(TAG, "CTRL: floor min(write)=%.1fC (req%s) for %s", new_lo, has_lo?" set":" implied", this->get_name().c_str());
      this->parent_->write_channel_floor_min_temperature(this->single_channel_, new_lo);
    }
    if (!std::isnan(new_hi) && (std::isnan(current_hi) || std::fabs(new_hi - current_hi) > 0.049f)) {
      ESP_LOGD(TAG, "CTRL: floor max(write)=%.1fC (req%s) for %s", new_hi, has_hi?" set":" implied", this->get_name().c_str());
      this->parent_->write_channel_floor_max_temperature(this->single_channel_, new_hi);
    }
  }

  this->publish_state();
}
void WavinZoneClimate::update_from_parent() {
  if (this->single_channel_set_) {
    uint8_t ch = this->single_channel_;
    if (this->use_floor_temperature_) {
      this->current_temperature = this->parent_->get_channel_floor_temp(ch);
    } else {
      this->current_temperature = this->parent_->get_channel_current_temp(ch);
    }
    this->target_temperature = this->parent_->get_channel_setpoint(ch);
    // For comfort climates, surface floor min/max as low/high targets
    if (this->use_floor_temperature_) {
      float fmin = this->parent_->get_channel_floor_min_temp(ch);
      float fmax = this->parent_->get_channel_floor_max_temp(ch);
      if (!std::isnan(fmin)) this->target_temperature_low = fmin;
      if (!std::isnan(fmax)) this->target_temperature_high = fmax;
    }
    this->mode = this->parent_->get_channel_mode(ch);
    // Action: derive from temperatures with a small deadband, fallback to controller bit
    const float db = this->hysteresis_;  // configurable hysteresis in °C (0.1-1.0)
    auto raw_action = this->parent_->get_channel_action(ch);
    if (!std::isnan(this->current_temperature) && !std::isnan(this->target_temperature)) {
      if (this->current_temperature > this->target_temperature + db) {
        this->action = climate::CLIMATE_ACTION_IDLE;
      } else if (this->current_temperature < this->target_temperature - db) {
        this->action = climate::CLIMATE_ACTION_HEATING;
      } else {
        this->action = raw_action;
      }
    } else {
      this->action = raw_action;
    }
  } else if (!this->members_.empty()) {
    float sum_curr = 0.0f, sum_set = 0.0f;
    int n_curr = 0;
    bool any_heat = false;
    bool all_off = true;
    for (auto ch : this->members_) {
      float c = this->parent_->get_channel_current_temp(ch);
      if (!std::isnan(c)) {
        sum_curr += c;
        n_curr++;
      }
      float s = this->parent_->get_channel_setpoint(ch);
      if (!std::isnan(s)) sum_set += s;
      if (this->parent_->get_channel_action(ch) == climate::CLIMATE_ACTION_HEATING) any_heat = true;
      if (this->parent_->get_channel_mode(ch) != climate::CLIMATE_MODE_OFF) all_off = false;
    }
    if (n_curr > 0) this->current_temperature = sum_curr / n_curr;
    if (!this->members_.empty()) this->target_temperature = sum_set / this->members_.size();
    this->mode = all_off ? climate::CLIMATE_MODE_OFF : climate::CLIMATE_MODE_HEAT;
    // Group action: prefer temperature comparison with deadband, fallback to any member heating
    const float db = this->hysteresis_;  // configurable hysteresis in °C (0.1-1.0)
    if (!std::isnan(this->current_temperature) && !std::isnan(this->target_temperature)) {
      if (this->current_temperature > this->target_temperature + db) {
        this->action = climate::CLIMATE_ACTION_IDLE;
      } else if (this->current_temperature < this->target_temperature - db) {
        this->action = climate::CLIMATE_ACTION_HEATING;
      } else {
        this->action = any_heat ? climate::CLIMATE_ACTION_HEATING : climate::CLIMATE_ACTION_IDLE;
      }
    } else {
      this->action = any_heat ? climate::CLIMATE_ACTION_HEATING : climate::CLIMATE_ACTION_IDLE;
    }
  }
  this->publish_state();
}

// Read device info from Info category registers (category 0x09)
void WavinAHC9000::read_device_info() {
  std::vector<uint16_t> regs;
  
  // Read all 5 info registers in one call (indices 0x00 to 0x04, page 0)
  // According to spec: Control Unit Address L, Control Unit Address H, HW Version, SW Version, Device Name
  if (this->read_registers(CAT_INFO, 0, INFO_CONTROL_UNIT_ADDRESS_L, 5, regs) && regs.size() >= 5) {
    // Control Unit Address is a 32-bit value: ADDRESS[31:0] = (ADDRESS_H << 16) | ADDRESS_L
    uint32_t control_unit_addr = ((uint32_t)regs[1] << 16) | regs[0];
    
    // HW Version: MC110xx where xx is HWVERS[6:0] in decimal
    // Bits 6:0 contain the version suffix
    uint16_t hw_vers = regs[2] & 0x007F;
    
    // SW Version: MC610xx where xx is SWVERS[7:0] in BCD
    // Register format: bits 15:4 = SWVERS[7:0], bits 3:0 = BETA[3:0]
    // We shift right 4 bits to extract SWVERS, then mask to 8 bits
    uint16_t sw_vers_bcd = (regs[3] >> 4) & 0x00FF;
    uint16_t beta = regs[3] & 0x000F;
    
    // Convert BCD to decimal: BCD has each nibble as a decimal digit (0-9)
    // High nibble = tens, low nibble = ones
    uint8_t sw_vers_tens = (sw_vers_bcd >> 4) & 0x0F;
    uint8_t sw_vers_ones = sw_vers_bcd & 0x0F;
    uint16_t sw_vers_decimal = sw_vers_tens * 10 + sw_vers_ones;
    
    // Device Name: AC-xxx where xxx is DEVNAME[15:0] in decimal
    uint16_t device_name = regs[4];
    
    // Format the strings according to spec
    char addr_str[32];
    snprintf(addr_str, sizeof(addr_str), "0x%08X", (unsigned)control_unit_addr);
    
    char hw_str[32];
    snprintf(hw_str, sizeof(hw_str), "MC110%02u", (unsigned)hw_vers);
    
    char sw_str[32];
    if (beta != 0) {
      snprintf(sw_str, sizeof(sw_str), "MC610%02ub%u", (unsigned)sw_vers_decimal, (unsigned)beta);
    } else {
      snprintf(sw_str, sizeof(sw_str), "MC610%02u", (unsigned)sw_vers_decimal);
    }
    
    char dev_str[32];
    snprintf(dev_str, sizeof(dev_str), "AC-%u", (unsigned)device_name);
    
    ESP_LOGI(TAG, "Device Info - Address: %s, HW: %s, SW: %s, Device: %s", 
             addr_str, hw_str, sw_str, dev_str);
    
    // Publish to text sensors if configured
    if (this->control_unit_address_text_sensor_ != nullptr) {
      this->control_unit_address_text_sensor_->publish_state(addr_str);
    }
    if (this->hw_version_text_sensor_ != nullptr) {
      this->hw_version_text_sensor_->publish_state(hw_str);
    }
    if (this->sw_version_text_sensor_ != nullptr) {
      this->sw_version_text_sensor_->publish_state(sw_str);
    }
    if (this->device_name_text_sensor_ != nullptr) {
      this->device_name_text_sensor_->publish_state(dev_str);
    }
  } else {
    ESP_LOGW(TAG, "Failed to read device info registers");
  }
}

}  // namespace wavin_ahc9000
}  // namespace esphome
