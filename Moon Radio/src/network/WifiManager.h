#pragma once

#include <FS.h>
#include <WiFi.h>
#include <array>
#include <vector>

class WifiManager {
 public:
  explicit WifiManager(fs::FS& filesystem);

  bool begin();
  // Indításkor a blokkoló Wi-Fi várakozás alatt hívódik (~20-100 ms-onként),
  // hogy a kijelző (bootlogó lejárta, LVGL) közben is kiszolgálható legyen.
  void setWaitHook(void (*hook)(void*), void* context) {
    waitHook_ = hook;
    waitContext_ = context;
  }
  void loop();
  bool saveCredentials(const String& ssid, const String& password);

  bool connected() const;
  bool accessPointMode() const;
  String address() const;
  String ssid() const;
  int rssi() const;

 private:
  struct Credential {
    String ssid;
    String password;
  };

  bool loadCredentials();
  bool findAccessPoint(const String& ssid, std::array<uint8_t, 6>& bssid,
                       int32_t& channel);
  bool tryConnect(const Credential& credential, uint32_t timeoutMs);
  void startAccessPoint();
  void waitStep(uint32_t ms);

  fs::FS& filesystem_;
  std::vector<Credential> credentials_;
  bool accessPointMode_{false};
  uint32_t lastReconnectAttempt_{0};
  uint32_t disconnectedSince_{0};
  mutable bool hasSmoothedRssi_{false};
  mutable int smoothedRssi_{-70};
  void (*waitHook_)(void*){nullptr};
  void* waitContext_{nullptr};
};
