#include "WifiManager.h"

#include <Arduino.h>

namespace {

constexpr const char* kCredentialsPath = "/wifi.txt";
constexpr const char* kHostname = "lvgl-radio";
constexpr const char* kAccessPointName = "Moon-Radio-Setup";
constexpr int32_t kRssiDisconnected = -100;
constexpr uint32_t kInitialConnectTimeoutMs = 15000;
constexpr uint32_t kReconnectTimeoutMs = 8000;
constexpr uint32_t kReconnectIntervalMs = 15000;
constexpr uint32_t kRuntimeApFallbackMs = 5UL * 60UL * 1000UL;

bool readNonEmptyLine(File& file, String& result) {
  while (file.available()) {
    result = file.readStringUntil('\n');
    result.trim();
    if (!result.isEmpty()) return true;
  }
  result = "";
  return false;
}

}  // namespace

WifiManager::WifiManager(fs::FS& filesystem) : filesystem_(filesystem) {
  credentials_.reserve(4);
}

bool WifiManager::findAccessPoint(const String& ssid,
                                  std::array<uint8_t, 6>& bssid,
                                  int32_t& channel) {
  channel = 0;
  const int networkCount = WiFi.scanNetworks(false, true, false, 200, 0U);
  if (networkCount <= 0) {
    WiFi.scanDelete();
    return false;
  }

  int strongestIndex = -1;
  int strongestRssi = -1000;
  for (int i = 0; i < networkCount; ++i) {
    if (WiFi.SSID(i) != ssid) continue;
    const int candidateRssi = WiFi.RSSI(i);
    if (strongestIndex < 0 || candidateRssi > strongestRssi) {
      strongestIndex = i;
      strongestRssi = candidateRssi;
    }
  }

  if (strongestIndex < 0) {
    WiFi.scanDelete();
    return false;
  }

  bssid.fill(0);
  const uint8_t* sourceBssid = WiFi.BSSID(strongestIndex);
  if (sourceBssid != nullptr) {
    memcpy(bssid.data(), sourceBssid, bssid.size());
  }
  channel = WiFi.channel(strongestIndex);
  WiFi.scanDelete();
  return channel > 0;
}

bool WifiManager::loadCredentials() {
  credentials_.clear();
  File file = filesystem_.open(kCredentialsPath, FILE_READ);
  if (!file) return false;

  while (file.available()) {
    String ssid;
    if (!readNonEmptyLine(file, ssid)) break;
    String password = file.readStringUntil('\n');
    password.trim();
    credentials_.push_back({ssid, password});
  }
  file.close();
  return !credentials_.empty();
}

bool WifiManager::tryConnect(const Credential& credential,
                             uint32_t timeoutMs) {
  const bool keepAccessPoint = accessPointMode_;
  if (!keepAccessPoint) {
    WiFi.softAPdisconnect(true);
  }
  WiFi.disconnect(false, false);
  WiFi.mode(keepAccessPoint ? WIFI_AP_STA : WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  WiFi.setHostname(kHostname);

  std::array<uint8_t, 6> bssid{};
  int32_t channel = 0;
  if (findAccessPoint(credential.ssid, bssid, channel)) {
    WiFi.begin(credential.ssid.c_str(), credential.password.c_str(), channel,
               bssid.data(), true);
    Serial.printf("[wifi] Kapcsolodas celzott AP-hoz: %s, ch=%ld\n",
                  credential.ssid.c_str(), static_cast<long>(channel));
  } else {
    WiFi.begin(credential.ssid.c_str(), credential.password.c_str());
    Serial.printf("[wifi] Kapcsolodas AP-szken nelkul: %s\n",
                  credential.ssid.c_str());
  }
  Serial.printf("[wifi] Kapcsolodas: %s\n", credential.ssid.c_str());

  const uint32_t started = millis();
  while (millis() - started < timeoutMs) {
    if (WiFi.status() == WL_CONNECTED &&
        WiFi.localIP() != IPAddress(static_cast<uint32_t>(0))) {
      WiFi.setSleep(false);
      WiFi.setAutoReconnect(true);
      if (keepAccessPoint) {
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_STA);
      }
      accessPointMode_ = false;
      disconnectedSince_ = 0;
      hasSmoothedRssi_ = false;
      Serial.printf("[wifi] IP: %s\n", WiFi.localIP().toString().c_str());
      return true;
    }
    delay(100);
  }
  WiFi.disconnect();
  if (keepAccessPoint) {
    WiFi.mode(WIFI_AP_STA);
    if (WiFi.softAPIP() == IPAddress(static_cast<uint32_t>(0))) {
      WiFi.softAP(kAccessPointName);
    }
    accessPointMode_ = true;
  }
  return false;
}

void WifiManager::startAccessPoint() {
  WiFi.disconnect(true, false);
  WiFi.mode(credentials_.empty() ? WIFI_AP : WIFI_AP_STA);
  WiFi.softAP(kAccessPointName);
  accessPointMode_ = true;
  Serial.printf("[wifi] Beallito AP: %s, IP: %s\n", kAccessPointName,
                WiFi.softAPIP().toString().c_str());
}

bool WifiManager::begin() {
  WiFi.persistent(false);
  WiFi.useStaticBuffers(true);
  WiFi.mode(WIFI_MODE_NULL);
  WiFi.setHostname(kHostname);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  loadCredentials();
  if (credentials_.empty()) {
    startAccessPoint();
    return false;
  }
  for (const Credential& credential : credentials_) {
    if (tryConnect(credential, kInitialConnectTimeoutMs)) return true;
  }
  startAccessPoint();
  return false;
}

void WifiManager::loop() {
  if (credentials_.empty()) {
    return;
  }

  const uint32_t now = millis();
  if (WiFi.status() == WL_CONNECTED) {
    disconnectedSince_ = 0;
    if (accessPointMode_) {
      WiFi.softAPdisconnect(true);
      WiFi.mode(WIFI_STA);
      accessPointMode_ = false;
    }
    return;
  }

  if (disconnectedSince_ == 0) {
    disconnectedSince_ = now;
    Serial.println("[wifi] Kapcsolat megszakadt, ujracsatlakozas varakozassal");
  }

  if (now - lastReconnectAttempt_ < kReconnectIntervalMs) return;
  lastReconnectAttempt_ = now;
  bool connectedNow = false;
  for (const Credential& credential : credentials_) {
    if (tryConnect(credential, kReconnectTimeoutMs)) {
      connectedNow = true;
      break;
    }
  }

  if (!connectedNow && !accessPointMode_ &&
      now - disconnectedSince_ >= kRuntimeApFallbackMs) {
    Serial.println("[wifi] Tartosan nincs kapcsolat, beallito AP inditasa");
    startAccessPoint();
  }
}

bool WifiManager::saveCredentials(const String& inputSsid,
                                  const String& inputPassword) {
  String ssid = inputSsid;
  String password = inputPassword;
  ssid.trim();
  password.trim();
  ssid.replace("\r", "");
  ssid.replace("\n", "");
  password.replace("\r", "");
  password.replace("\n", "");
  if (ssid.isEmpty()) return false;

  File file = filesystem_.open(kCredentialsPath, FILE_WRITE);
  if (!file) return false;
  const bool ok = file.println(ssid) && file.println(password);
  file.close();
  return ok;
}

bool WifiManager::connected() const {
  return WiFi.status() == WL_CONNECTED;
}

bool WifiManager::accessPointMode() const { return accessPointMode_; }

String WifiManager::address() const {
  return accessPointMode_ ? WiFi.softAPIP().toString()
                          : WiFi.localIP().toString();
}

String WifiManager::ssid() const {
  return accessPointMode_ ? String(kAccessPointName) : WiFi.SSID();
}

int WifiManager::rssi() const {
  if (!connected()) {
    hasSmoothedRssi_ = false;
    smoothedRssi_ = kRssiDisconnected;
    return kRssiDisconnected;
  }

  const int currentRssi = WiFi.RSSI();
  if (!hasSmoothedRssi_) {
    smoothedRssi_ = currentRssi;
    hasSmoothedRssi_ = true;
  } else {
    smoothedRssi_ = ((smoothedRssi_ * 3) + currentRssi) / 4;
  }
  return smoothedRssi_;
}
