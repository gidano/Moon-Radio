#pragma once

#include <FS.h>
#include <WiFi.h>
#include <array>
#include <vector>

class WifiManager {
 public:
  explicit WifiManager(fs::FS& filesystem);

  bool begin();
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

  fs::FS& filesystem_;
  std::vector<Credential> credentials_;
  bool accessPointMode_{false};
  uint32_t lastReconnectAttempt_{0};
  uint32_t disconnectedSince_{0};
  mutable bool hasSmoothedRssi_{false};
  mutable int smoothedRssi_{-70};
};
