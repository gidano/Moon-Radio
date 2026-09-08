#include "PresetStore.h"

namespace {

constexpr char kPresetDirectory[] = "/presets";
constexpr char kLabelPath[] = "/presets/banks.txt";
constexpr const char* kDefaultLabels[PresetStore::kBankCount] = {
    "KEDV", "ROCK", "MAGYAR", "CHILL", "VEGYES"};

}  // namespace

PresetStore::PresetStore(fs::FS& filesystem) : filesystem_(filesystem) {}

bool PresetStore::begin() {
  if (!filesystem_.exists(kPresetDirectory) &&
      !filesystem_.mkdir(kPresetDirectory)) {
    Serial.println("[presets] A /presets mappa nem hozhato letre");
    return false;
  }
  loadLabels();
  bank_ = 0;
  return loadBank();
}

bool PresetStore::selectBank(uint8_t bank) {
  if (bank >= kBankCount) return false;
  bank_ = bank;
  return loadBank();
}

uint8_t PresetStore::bank() const { return bank_; }

const String& PresetStore::bankLabel(uint8_t bank) const {
  static const String empty;
  return bank < kBankCount ? labels_[bank] : empty;
}

const RadioPreset* PresetStore::slot(uint8_t slot) const {
  return slot < kSlotCount ? &slots_[slot] : nullptr;
}

bool PresetStore::saveSlot(uint8_t slot, const String& name,
                           const String& url) {
  if (slot >= kSlotCount) return false;
  const String cleanName = cleanField(name);
  const String cleanUrl = cleanField(url);
  if (cleanName.isEmpty() || cleanUrl.isEmpty()) return false;
  slots_[slot].name = cleanName;
  slots_[slot].url = cleanUrl;
  return writeBank();
}

bool PresetStore::clearSlot(uint8_t slot) {
  if (slot >= kSlotCount) return false;
  slots_[slot] = {};
  return writeBank();
}

String PresetStore::cleanField(String value) {
  value.replace('\t', ' ');
  value.replace('\r', ' ');
  value.replace('\n', ' ');
  value.trim();
  while (value.indexOf("  ") >= 0) value.replace("  ", " ");
  return value;
}

String PresetStore::bankPath(uint8_t bank) const {
  return String(kPresetDirectory) + "/bank" + String(bank) + ".txt";
}

void PresetStore::loadLabels() {
  for (uint8_t index = 0; index < kBankCount; ++index)
    labels_[index] = kDefaultLabels[index];

  File file = filesystem_.open(kLabelPath, FILE_READ);
  if (!file) return;
  uint8_t index = 0;
  while (file.available() && index < kBankCount) {
    String label = file.readStringUntil('\n');
    label.replace("\r", "");
    label = cleanField(label);
    if (!label.isEmpty()) labels_[index] = label;
    ++index;
  }
  file.close();
}

bool PresetStore::loadBank() {
  for (auto& slot : slots_) slot = {};

  File file = filesystem_.open(bankPath(bank_), FILE_READ);
  if (!file) return true;

  uint8_t index = 0;
  while (file.available() && index < kSlotCount) {
    String line = file.readStringUntil('\n');
    if (line.endsWith("\r")) line.remove(line.length() - 1);
    const int separator = line.indexOf('\t');
    if (separator >= 0) {
      slots_[index].name = cleanField(line.substring(0, separator));
      slots_[index].url = cleanField(line.substring(separator + 1));
      if (slots_[index].empty()) slots_[index] = {};
    }
    ++index;
  }
  file.close();
  return true;
}

bool PresetStore::writeBank() const {
  const String path = bankPath(bank_);
  File file = filesystem_.open(path, FILE_WRITE);
  if (!file) {
    Serial.printf("[presets] Nem irhato: %s\n", path.c_str());
    return false;
  }
  for (uint8_t index = 0; index < kSlotCount; ++index) {
    if ((!slots_[index].name.isEmpty() &&
         file.print(slots_[index].name) != slots_[index].name.length()) ||
        file.print('\t') == 0 ||
        file.println(slots_[index].url) == 0) {
      file.close();
      return false;
    }
  }
  file.close();
  return true;
}
