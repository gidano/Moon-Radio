#pragma once

#include <FS.h>
#include <vector>

#include "Station.h"

class StationStore {
 public:
  static constexpr size_t kMaxStations = 120;

  explicit StationStore(fs::FS& filesystem,
                        const char* path = "/stations.txt");

  bool load();
  bool save() const;
  bool reload();

  const std::vector<Station>& stations() const;
  size_t count() const;
  const Station* get(size_t index) const;

  bool add(const Station& station);
  bool update(size_t index, const Station& station);
  bool remove(size_t index);
  bool move(size_t from, size_t to);

  static bool parseLine(const String& raw, Station& station);

 private:
  static void sanitize(Station& station);
  void loadHomepages();
  bool saveHomepages() const;

  fs::FS& filesystem_;
  String path_;
  String homepagePath_{"/station_homepages.txt"};
  std::vector<Station> stations_;
};
