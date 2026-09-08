#pragma once

#include <Arduino.h>
#include <FS.h>

struct RadioPreset {
  String name;
  String url;

  bool empty() const { return name.isEmpty() || url.isEmpty(); }
};

class PresetStore {
 public:
  static constexpr uint8_t kBankCount = 5;
  static constexpr uint8_t kSlotCount = 8;

  explicit PresetStore(fs::FS& filesystem);

  bool begin();
  bool selectBank(uint8_t bank);
  uint8_t bank() const;
  const String& bankLabel(uint8_t bank) const;
  const RadioPreset* slot(uint8_t slot) const;
  bool saveSlot(uint8_t slot, const String& name, const String& url);
  bool clearSlot(uint8_t slot);

 private:
  static String cleanField(String value);
  String bankPath(uint8_t bank) const;
  void loadLabels();
  bool loadBank();
  bool writeBank() const;

  fs::FS& filesystem_;
  uint8_t bank_{0};
  String labels_[kBankCount];
  RadioPreset slots_[kSlotCount];
};
