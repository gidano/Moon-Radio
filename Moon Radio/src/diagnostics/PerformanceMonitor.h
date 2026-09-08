#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "driver/temperature_sensor.h"

struct DiagnosticsSnapshot {
  uint8_t cpu0Percent{0};
  uint8_t cpu1Percent{0};
  float temperatureC{NAN};
  uint32_t freeInternalHeap{0};
  uint32_t freePsram{0};
  uint16_t cpuFrequencyMhz{0};
  bool cpuValid{false};
  bool temperatureValid{false};
};

class PerformanceMonitor {
 public:
  bool begin();
  void update();
 DiagnosticsSnapshot snapshot() const;

 private:
  struct PreviousTaskRuntime {
    UBaseType_t taskNumber{0};
    configRUN_TIME_COUNTER_TYPE counter{0};
    bool valid{false};
  };

  static constexpr UBaseType_t kMaximumTasks = 48;

  bool sampleTaskRuntime(bool baselineOnly);
  configRUN_TIME_COUNTER_TYPE previousCounter(UBaseType_t taskNumber) const;
  void saveTaskRuntime(const TaskStatus_t* states, UBaseType_t count);

  temperature_sensor_handle_t temperatureSensor_{nullptr};
  DiagnosticsSnapshot snapshot_;
  uint32_t lastSystemSampleAt_{0};
  uint32_t lastCpuSampleAt_{0};
  configRUN_TIME_COUNTER_TYPE previousTotalRuntime_{0};
  TaskStatus_t taskStates_[kMaximumTasks]{};
  PreviousTaskRuntime previousTasks_[kMaximumTasks]{};
};
