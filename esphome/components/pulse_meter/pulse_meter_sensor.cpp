#include "pulse_meter_sensor.h"
#include <utility>
#include "esphome/core/log.h"

namespace esphome {
namespace pulse_meter {

static const char *const TAG = "pulse_meter";

void PulseMeterSensor::set_total_pulses(uint32_t pulses) {
  this->total_pulses_ = pulses;
  if (this->total_sensor_ != nullptr) {
    this->total_sensor_->publish_state(this->total_pulses_);
  }
}

void PulseMeterSensor::setup() {
  this->pin_->setup();
  this->pin2_->setup();
  this->isr_pin2_ = pin2_->to_isr();

  // Set the last processed edge to now for the first timeout
  this->last_processed_edge_us_ = micros();

  this->pin_->attach_interrupt(PulseMeterSensor::edge_intr, this, gpio::INTERRUPT_RISING_EDGE);
}

void PulseMeterSensor::loop() {
  // Reset the count in get before we pass it back to the ISR as set
  this->get_->count_up_ = 0;
  this->get_->count_down_ = 0;

  // Swap out set and get to get the latest state from the ISR
  // The ISR could interrupt on any of these lines and the results would be consistent
  auto *temp = this->set_;
  this->set_ = this->get_;
  this->get_ = temp;
  int32_t count = this->get_->count_up_ - this->get_->count_down_;

  // Check if we detected a pulse this loop
  if (this->get_->count_up_ > 0 || this->get_->count_down_ > 0) {
    // Keep a running total of pulses if a total sensor is configured
      if (this->forward_sensor_ != nullptr) {
        if (this->get_->count_up_ > 0) {
          this->total_pulses_up_ += this->get_->count_up_;
          this->forward_sensor_->publish_state(this->total_pulses_up_);
        }
      }
      if (this->reverse_sensor_ != nullptr) {
        if (this->get_->count_down_ > 0) {
          this->total_pulses_down_ += this->get_->count_down_;
          this->reverse_sensor_->publish_statue(this->total_pulses_down_);
        }
      }
      if (this->total_sensor_ != nullptr) {
        if (count != 0) {
          this->total_pulses_ += count;
          this->total_sensor_->publish_state(this->total_pulses_);
        }
      }
    }

    // We need to detect at least two edges to have a valid pulse width
    switch (this->meter_state_) {
      case MeterState::INITIAL:
      case MeterState::TIMED_OUT: {
        this->meter_state_ = MeterState::RUNNING;
      } break;
      case MeterState::RUNNING: {
        uint32_t delta_us = this->get_->last_detected_edge_us_ - this->last_processed_edge_us_;
        if (count == 0) {
          this->publish_state(0.0f);
        } else {
          float pulse_width_us = delta_us / float(count);
          this->publish_state((60.0f * 1000000.0f) / pulse_width_us);
        }
      } break;
    }
    this->last_processed_edge_us_ = this->get_->last_detected_edge_us_;
  }
  // No detected edges this loop
  else {
    const uint32_t now = micros();
    const uint32_t time_since_valid_edge_us = now - this->last_processed_edge_us_;

    switch (this->meter_state_) {
      // Running and initial states can timeout
      case MeterState::INITIAL:
      case MeterState::RUNNING: {
        if (time_since_valid_edge_us > this->timeout_us_) {
          this->meter_state_ = MeterState::TIMED_OUT;
          ESP_LOGD(TAG, "No pulse detected for %" PRIu32 "s, assuming 0 pulses/min",
                   time_since_valid_edge_us / 1000000);
          this->publish_state(0.0f);
        }
      } break;
      default:
        break;
    }
  }
}

float PulseMeterSensor::get_setup_priority() const { return setup_priority::DATA; }

void PulseMeterSensor::dump_config() {
  LOG_SENSOR("", "Pulse Meter", this);
  LOG_PIN("  Pin: ", this->pin_);
  LOG_PIN("  Pin2: ", this->pin2_);
  ESP_LOGCONFIG(TAG, "  Assuming 0 pulses/min after not receiving a pulse for %" PRIu32 "s",
                this->timeout_us_ / 1000000);  
}

void IRAM_ATTR PulseMeterSensor::edge_intr(PulseMeterSensor *sensor) {
  // This is an interrupt handler - we can't call any virtual method from this method
  // Get the current time before we do anything else so the measurements are consistent
  const uint32_t now = micros();
  const bool pin2_val = sensor->isr_pin2_.digital_read();

  if (pin2_val == sensor->forward_) {
    if (pin2_val) {
      sensor->set_->count_up_++;
    } else {
      sensor->set_->count_down_++;
    }
  } else {
    sensor->forward_ = pin2_val;
  }
  sensor->set_->last_detected_edge_us_ = now;
}

}  // namespace pulse_meter
}  // namespace esphome
