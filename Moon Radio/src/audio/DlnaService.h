#pragma once

#include <SoapESP32.h>
#include <WiFiUdp.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

// SoapESP32 is used only by this worker; UI readers see completed snapshots.
class DlnaService {
 public:
  static constexpr uint16_t pageSize = 32;
  bool scan();
  bool browse(unsigned server, const String& id, const String& title,
              uint32_t offset, uint32_t revision);
  void close();
  bool busy() const { return busy_.load(); }
  struct State {
    bool busy{false}, servers{true}, more{false};
    unsigned server{0};
    size_t count{0};
    uint32_t revision{0}, offset{0}, nextOffset{0};
    const char* error{""};
  };
  struct Location {
    unsigned server{0};
    uint32_t offset{0};
    SoapString id, title;
  };
  State state();
  String heading();
  String nameAt(size_t index, uint32_t revision);
  bool selection(size_t index, uint32_t revision, Location& out, bool& directory);
  bool location(Location& out);
  bool showServers();
  SoapString json();
  bool play(size_t index, uint32_t revision, String& url, String& title);
  bool step(int delta, String& url, String& title);
  bool resume(String& url, String& title);
  int playingIndex() const { return playingIndex_; }
  size_t playingCount() const { return playing_.size(); }

 private:
  enum class Operation { Scan, Browse, Clear };
  struct Command {
    Operation op{Operation::Clear};
    unsigned server{0};
    uint32_t offset{0}, generation{0};
    char id[513]{}, title[257]{};
  };
  bool start();
  bool submit(Command command);
  static void taskEntry(void* context);
  void task();
  bool track(const soapObject_t& entry, String& url, String& title);
  void clearPublished();

  WiFiClient client_;
  WiFiUDP udp_;
  SoapESP32 soap_{&client_, &udp_};
  SemaphoreHandle_t mutex_{nullptr};
  TaskHandle_t task_{nullptr};
  std::atomic<bool> busy_{false};
  std::atomic<uint32_t> generation_{0};
  Command command_;
  soapServerVect_t servers_;
  soapObjectVect_t entries_, playing_;
  SoapString objectId_, heading_;
  const char* error_{""};
  bool serverView_{true}, more_{false};
  unsigned server_{0};
  uint32_t offset_{0}, nextOffset_{0}, revision_{0};
  int playingIndex_{-1};
};
