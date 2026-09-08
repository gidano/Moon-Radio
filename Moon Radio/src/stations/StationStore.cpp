#include "StationStore.h"

#include <Arduino.h>

namespace {

String cleanField(String value) {
  value.trim();
  value.replace("\t", " ");
  value.replace("\r", " ");
  value.replace("\n", " ");
  return value;
}

}  // namespace

StationStore::StationStore(fs::FS& filesystem, const char* path)
    : filesystem_(filesystem), path_(path) {
  stations_.reserve(kMaxStations);
}

bool StationStore::parseLine(const String& raw, Station& station) {
  String line = raw;
  line.trim();
  if (line.isEmpty() || line.startsWith("#")) return false;

  int first = line.indexOf('\t');
  if (first < 0) first = line.indexOf('|');
  if (first < 0) return false;

  int second = line.indexOf('\t', first + 1);
  if (second < 0) second = line.indexOf('|', first + 1);

  station.name = line.substring(0, first);
  if (second >= 0) {
    station.url = line.substring(first + 1, second);
    station.logoName = line.substring(second + 1);
  } else {
    station.url = line.substring(first + 1);
    station.logoName = "nologo";
  }

  sanitize(station);
  return !station.name.isEmpty() && !station.url.isEmpty();
}

void StationStore::sanitize(Station& station) {
  station.name = cleanField(station.name);
  station.url = cleanField(station.url);
  station.logoName = cleanField(station.logoName);
  station.homepage = cleanField(station.homepage);
  if (station.logoName.isEmpty()) station.logoName = "nologo";
}

bool StationStore::load() {
  stations_.clear();

  File file = filesystem_.open(path_, FILE_READ);
  if (!file) {
    Serial.printf("[stations] Nem nyithato meg: %s\n", path_.c_str());
    return false;
  }

  while (file.available() && stations_.size() < kMaxStations) {
    Station station;
    if (parseLine(file.readStringUntil('\n'), station)) {
      stations_.push_back(std::move(station));
    }
  }
  file.close();
  loadHomepages();

  Serial.printf("[stations] %u allomas betoltve\n",
                static_cast<unsigned>(stations_.size()));
  return !stations_.empty();
}

bool StationStore::reload() { return load(); }

bool StationStore::save() const {
  const String temporaryPath = path_ + ".tmp";
  File file = filesystem_.open(temporaryPath, FILE_WRITE);
  if (!file) return false;

  bool ok = true;
  for (const Station& source : stations_) {
    Station station = source;
    sanitize(station);
    if (!file.printf("%s\t%s\t%s\n", station.name.c_str(),
                     station.url.c_str(), station.logoName.c_str())) {
      ok = false;
      break;
    }
  }
  file.close();

  if (!ok) {
    filesystem_.remove(temporaryPath);
    return false;
  }

  filesystem_.remove(path_);
  if (!filesystem_.rename(temporaryPath, path_)) {
    filesystem_.remove(temporaryPath);
    return false;
  }
  return saveHomepages();
}

void StationStore::loadHomepages() {
  File file = filesystem_.open(homepagePath_, FILE_READ);
  if (!file) return;
  while (file.available()) {
    String line = file.readStringUntil('\n');
    line.trim();
    const int separator = line.indexOf('\t');
    if (separator <= 0) continue;
    const String url = cleanField(line.substring(0, separator));
    const String homepage = cleanField(line.substring(separator + 1));
    if (url.isEmpty() || homepage.isEmpty()) continue;
    for (Station& station : stations_) {
      if (station.url == url) {
        station.homepage = homepage;
        break;
      }
    }
  }
  file.close();
}

bool StationStore::saveHomepages() const {
  const String temporaryPath = homepagePath_ + ".tmp";
  File file = filesystem_.open(temporaryPath, FILE_WRITE);
  if (!file) return false;
  bool ok = true;
  for (const Station& source : stations_) {
    Station station = source;
    sanitize(station);
    if (station.homepage.isEmpty()) continue;
    if (!file.printf("%s\t%s\n", station.url.c_str(),
                     station.homepage.c_str())) {
      ok = false;
      break;
    }
  }
  file.close();
  if (!ok) {
    filesystem_.remove(temporaryPath);
    return false;
  }
  filesystem_.remove(homepagePath_);
  if (!filesystem_.rename(temporaryPath, homepagePath_)) {
    filesystem_.remove(temporaryPath);
    return false;
  }
  return true;
}

const std::vector<Station>& StationStore::stations() const {
  return stations_;
}

size_t StationStore::count() const { return stations_.size(); }

const Station* StationStore::get(size_t index) const {
  return index < stations_.size() ? &stations_[index] : nullptr;
}

bool StationStore::add(const Station& source) {
  if (stations_.size() >= kMaxStations) return false;
  Station station = source;
  sanitize(station);
  if (station.name.isEmpty() || station.url.isEmpty()) return false;
  stations_.push_back(std::move(station));
  return save();
}

bool StationStore::update(size_t index, const Station& source) {
  if (index >= stations_.size()) return false;
  Station station = source;
  sanitize(station);
  if (station.name.isEmpty() || station.url.isEmpty()) return false;
  stations_[index] = std::move(station);
  return save();
}

bool StationStore::remove(size_t index) {
  if (index >= stations_.size()) return false;
  stations_.erase(stations_.begin() + index);
  return save();
}

bool StationStore::move(size_t from, size_t to) {
  if (from >= stations_.size() || to >= stations_.size()) return false;
  if (from == to) return true;

  Station station = std::move(stations_[from]);
  stations_.erase(stations_.begin() + from);
  stations_.insert(stations_.begin() + to, std::move(station));
  return save();
}
