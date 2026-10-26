#pragma once
#include <Arduino.h>
#include <FS.h>
#include <vector>

class SdLibrary {
 public:
  struct Entry { String name; String path; bool directory; };
  bool mount();
  fs::FS& filesystem();
  bool list(const String& path, size_t offset, std::vector<Entry>& entries,
            bool& more);
  bool playable(const String& path);
  String adjacent(const String& path, int delta);
  static bool normalize(String path, String& result);
  static bool supported(String path);
 private:
  bool mounted_{false};
};
