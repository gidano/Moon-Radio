#include "PerformanceMonitor.h"

#include <algorithm>
#include <cstring>

#include <esp_heap_caps.h>

namespace {

constexpr uint32_t kSampleIntervalMs = 5000;
constexpr uint32_t kSystemSampleIntervalMs = 1000;
constexpr size_t kTopTasks = 6;

}  // namespace

bool PerformanceMonitor::begin() {
  snapshot_.cpuFrequencyMhz = getCpuFrequencyMhz();

  temperature_sensor_config_t temperatureConfig =
      TEMPERATURE_SENSOR_CONFIG_DEFAULT(20, 100);
  const esp_err_t installed =
      temperature_sensor_install(&temperatureConfig, &temperatureSensor_);
  const esp_err_t enabled =
      installed == ESP_OK ? temperature_sensor_enable(temperatureSensor_)
                          : installed;
  if (enabled != ESP_OK) temperatureSensor_ = nullptr;

  lastCpuSampleAt_ = millis();
  const bool runtimeStats = sampleTaskRuntime(true);
  update();
  Serial.printf(
      "[diag] Perfmon: FreeRTOS=%s, %u MHz, homero=%s\n",
      runtimeStats ? "OK" : "HIBA", snapshot_.cpuFrequencyMhz,
      temperatureSensor_ ? "OK" : "HIBA");
  return runtimeStats;
}

void PerformanceMonitor::update() {
  const uint32_t now = millis();
  if (now - lastSystemSampleAt_ >= kSystemSampleIntervalMs) {
    lastSystemSampleAt_ = now;
    snapshot_.freeInternalHeap =
        heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    snapshot_.freePsram =
        heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    if (temperatureSensor_) {
      float value = NAN;
      snapshot_.temperatureValid =
          temperature_sensor_get_celsius(temperatureSensor_, &value) == ESP_OK;
      snapshot_.temperatureC =
          snapshot_.temperatureValid ? value : static_cast<float>(NAN);
    }
  }

  if (now - lastCpuSampleAt_ < kSampleIntervalMs) return;
  lastCpuSampleAt_ = now;
  sampleTaskRuntime(false);
}

DiagnosticsSnapshot PerformanceMonitor::snapshot() const { return snapshot_; }

bool PerformanceMonitor::sampleTaskRuntime(bool baselineOnly) {
  configRUN_TIME_COUNTER_TYPE totalRuntime = 0;
  const UBaseType_t count =
      uxTaskGetSystemState(taskStates_, kMaximumTasks, &totalRuntime);
  if (!count || count > kMaximumTasks) {
    snapshot_.cpuValid = false;
    Serial.println("[diag] FreeRTOS task lista tul nagy vagy nem elerheto");
    return false;
  }

  if (baselineOnly || !previousTotalRuntime_) {
    previousTotalRuntime_ = totalRuntime;
    saveTaskRuntime(taskStates_, count);
    return true;
  }

  const configRUN_TIME_COUNTER_TYPE totalDelta =
      totalRuntime - previousTotalRuntime_;
  if (!totalDelta) return false;

  configRUN_TIME_COUNTER_TYPE idleDelta[2] = {0, 0};
  struct TopTask {
    UBaseType_t index{0};
    configRUN_TIME_COUNTER_TYPE delta{0};
  };
  TopTask top[kTopTasks]{};

  for (UBaseType_t index = 0; index < count; ++index) {
    const TaskStatus_t& task = taskStates_[index];
    const configRUN_TIME_COUNTER_TYPE before =
        previousCounter(task.xTaskNumber);
    if (!before) continue;
    const configRUN_TIME_COUNTER_TYPE delta =
        task.ulRunTimeCounter - before;

    if (task.xCoreID >= 0 && task.xCoreID < 2 &&
        strncmp(task.pcTaskName, "IDLE", 4) == 0) {
      idleDelta[task.xCoreID] += delta;
      continue;
    }

    for (size_t position = 0; position < kTopTasks; ++position) {
      if (delta <= top[position].delta) continue;
      for (size_t move = kTopTasks - 1; move > position; --move)
        top[move] = top[move - 1];
      top[position] = {.index = index, .delta = delta};
      break;
    }
  }

  auto usageFromIdle = [totalDelta](configRUN_TIME_COUNTER_TYPE idle) {
    const uint32_t idlePercent = static_cast<uint32_t>(
        std::min<uint64_t>(100U,
                           static_cast<uint64_t>(idle) * 100U / totalDelta));
    return static_cast<uint8_t>(100U - idlePercent);
  };
  snapshot_.cpu0Percent = usageFromIdle(idleDelta[0]);
  snapshot_.cpu1Percent = usageFromIdle(idleDelta[1]);
  snapshot_.cpuValid = true;

  previousTotalRuntime_ = totalRuntime;
  saveTaskRuntime(taskStates_, count);
  return true;
}

configRUN_TIME_COUNTER_TYPE PerformanceMonitor::previousCounter(
    UBaseType_t taskNumber) const {
  for (const PreviousTaskRuntime& task : previousTasks_) {
    if (task.valid && task.taskNumber == taskNumber) return task.counter;
  }
  return 0;
}

void PerformanceMonitor::saveTaskRuntime(const TaskStatus_t* states,
                                         UBaseType_t count) {
  for (PreviousTaskRuntime& task : previousTasks_) task = {};
  for (UBaseType_t index = 0; index < count && index < kMaximumTasks;
       ++index) {
    previousTasks_[index].taskNumber = states[index].xTaskNumber;
    previousTasks_[index].counter = states[index].ulRunTimeCounter;
    previousTasks_[index].valid = true;
  }
}
