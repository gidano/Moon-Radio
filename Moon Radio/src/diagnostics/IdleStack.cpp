// The prebuilt Arduino/IDF kernel uses a small IDLE stack.  Reserve extra
// space for interrupt-exit/context-switch bursts on both ESP32-S3 cores.
// Keep the IDF overhead separate, just as configMINIMAL_STACK_SIZE does.
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#if CONFIG_IDF_TARGET_ESP32S3
namespace {
constexpr uint32_t kIdleStackBytes = 3072 + configSTACK_OVERHEAD_TOTAL;
}

extern "C" void __wrap_vApplicationGetIdleTaskMemory(
    StaticTask_t** taskBuffer, StackType_t** stackBuffer, uint32_t* stackSize) {
  *taskBuffer = static_cast<StaticTask_t*>(pvPortMalloc(sizeof(StaticTask_t)));
  *stackBuffer = static_cast<StackType_t*>(pvPortMalloc(kIdleStackBytes));
  configASSERT(*taskBuffer != nullptr);
  configASSERT(*stackBuffer != nullptr);
  // ESP-IDF's task stack size is in bytes, not StackType_t elements.
  *stackSize = kIdleStackBytes;
}
#endif
