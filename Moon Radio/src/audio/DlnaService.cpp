#include "DlnaService.h"
#include "../i18n/Language.h"

#include <esp_memory_utils.h>
#include <cstring>

namespace {
// Long remote names cannot grow the JSON response without bound. Keep UTF-8
// character boundaries while clipping display text; object IDs are kept whole.
SoapString quoted(const char* text, size_t limit = 512) {
  SoapString out("\"");
  size_t n = text ? strlen(text) : 0;
  if (n > limit) { n = limit; while (n && (static_cast<unsigned char>(text[n]) & 0xc0) == 0x80) --n; }
  for (size_t i = 0; i < n; ++i) {
    const unsigned char c = text[i];
    if (c == '"' || c == '\\') { out += '\\'; out += static_cast<char>(c); }
    else if (c < 32) { char escaped[7]; snprintf(escaped, sizeof(escaped), "\\u%04x", c); out += escaped; }
    else out += static_cast<char>(c);
  }
  out += '"'; return out;
}
SoapString number(size_t n) { char text[24]; snprintf(text, sizeof(text), "%u", static_cast<unsigned>(n)); return SoapString(text); }
bool supported(const soapObject_t& e) {
  if (e.isDirectory || e.fileType != fileTypeAudio) return false;
  const char* p = e.protInfo.c_str();
  return strstr(p, ":audio/mpeg:") || strstr(p, ":audio/mp3:") ||
         strstr(p, ":audio/aac:") || strstr(p, ":audio/aacp:") ||
         strstr(p, ":audio/x-aac:") || strstr(p, ":audio/flac:") ||
         strstr(p, ":audio/x-flac:") || strstr(p, ":audio/ogg:") ||
         strstr(p, ":application/ogg:") || strstr(p, ":audio/x-ogg:") ||
         strstr(p, ":audio/opus:") || strstr(p, ":audio/wav:") ||
         strstr(p, ":audio/x-wav:") || strstr(p, ":audio/mp4:");
}

// Executed once on the actual ESP32 before starting the first DLNA worker.
// Checks placement, growth, copies, vector moves, refusal and full release.
bool checkPsramStorage() {
  const size_t baseline = SoapMemory::used.load();
  SoapMemory::failed = false;
  bool ok = true;
  {
    SoapString s("  Árvíz & <zene>  "); s.trim();
    s.replace("&", "&amp;");
    ok = strcmp(s.c_str(), "Árvíz &amp; <zene>") == 0;
    SoapString copy(s);
    for (unsigned i = 0; i < 1024; ++i) s += 'x';
    ok = ok && esp_ptr_external_ram(s.c_str()) && copy.length() < s.length();
    SoapString alias("0123456789"); alias += alias;
    ok = ok && strcmp(alias.c_str(), "01234567890123456789") == 0;
    SoapVector<SoapString> rows;
    for (unsigned i = 0; i < 33; ++i) rows.push_back(copy);
    ok = ok && rows.size() == 33 && esp_ptr_external_ram(rows.begin()) &&
         strcmp(rows[32].c_str(), copy.c_str()) == 0 && !SoapMemory::failed.load();
    void* refused = SoapMemory::allocate(SoapMemory::budget + 1);
    ok = ok && !refused && SoapMemory::failed.load();
    SoapMemory::release(refused);
  }
  ok = ok && SoapMemory::used.load() == baseline;
  SoapMemory::failed = false;
  Serial.printf("[dlna] PSRAM storage self-check=%s\n", ok ? "OK" : "FAILED");
  return ok;
}
}

bool DlnaService::start() {
  if (task_) return true;
  if (!psramFound() || heap_caps_get_free_size(MALLOC_CAP_SPIRAM) < 768 * 1024 ||
      heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) < 128 * 1024 ||
      heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) < 48 * 1024) return false;
  if (!mutex_) mutex_ = xSemaphoreCreateMutex();
  if (!mutex_) return false;
  if (!checkPsramStorage()) return false;
  client_.setConnectionTimeout(3000);
  client_.setTimeout(3000);
  return xTaskCreatePinnedToCoreWithCaps(taskEntry, "dlna", 8192, this, 1,
               &task_, 0, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) == pdPASS;
}

bool DlnaService::submit(Command command) {
  if (!WiFi.isConnected() || !start()) return false;
  if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) < 48 * 1024) return false;
  xSemaphoreTake(mutex_, portMAX_DELAY);
  if (busy()) { xSemaphoreGive(mutex_); return false; }
  busy_ = true;
  command.generation = generation_.load();
  command_ = command;
  xSemaphoreGive(mutex_);
  xTaskNotifyGive(task_);
  return true;
}

bool DlnaService::scan() {
  Command c; c.op = Operation::Scan;
  return submit(c);
}

bool DlnaService::browse(unsigned server, const String& id, const String& title,
                         uint32_t offset, uint32_t revision) {
  if (!mutex_ || id.isEmpty() || id.length() > 512 || offset > 1000000) return false;
  xSemaphoreTake(mutex_, portMAX_DELAY);
  const bool valid = !busy() && revision == revision_ && server < servers_.size();
  xSemaphoreGive(mutex_);
  if (!valid) return false;
  Command c; c.op = Operation::Browse; c.server = server; c.offset = offset;
  strlcpy(c.id, id.c_str(), sizeof(c.id)); strlcpy(c.title, title.c_str(), sizeof(c.title));
  return submit(c);
}

void DlnaService::clearPublished() {
  servers_.reset(); entries_.reset();
  SoapString().swap(objectId_); SoapString().swap(heading_);
  serverView_ = true; more_ = false; offset_ = nextOffset_ = 0;
  error_ = ""; ++revision_;
}

void DlnaService::close() {
  // Playback state is owned by the main loop. Network state is freed by its
  // worker after any in-flight operation, never by deleting a running task.
  playing_.reset(); playingIndex_ = -1;
  if (!task_) return;
  xSemaphoreTake(mutex_, portMAX_DELAY);
  generation_.fetch_add(1);
  const bool notify = !busy();
  if (notify) {
    busy_ = true;
    command_ = Command{}; command_.generation = generation_.load();
  }
  xSemaphoreGive(mutex_);
  if (notify) xTaskNotifyGive(task_);
}

void DlnaService::taskEntry(void* context) { static_cast<DlnaService*>(context)->task(); }

void DlnaService::task() {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    const Command c = command_;
    SoapMemory::failed = false;
    soap_.setOperationDeadline(millis() + 20000);
    bool ok = true;
    soapObjectVect_t result;
    soapServerVect_t found;
    if (c.op == Operation::Scan) {
      soap_.seekServer(5);
      for (unsigned i = 0; i < soap_.getServerCount(); ++i) {
        soapServer_t s;
        if (soap_.getServerInfo(i, &s)) found.push_back(s);
      }
      ok = !SoapMemory::failed.load();
    } else if (c.op == Operation::Browse) {
      ok = soap_.browseServer(c.server, c.id, &result, c.offset, pageSize) &&
           !SoapMemory::failed.load() && soap_.lastNumberReturned() <= pageSize;
    }
    SoapString nextId, nextHeading;
    if (ok && c.op == Operation::Browse) { nextId = c.id; nextHeading = c.title; }
    const bool memoryFailed = SoapMemory::failed.load();
    ok = ok && !memoryFailed;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (c.generation != generation_.load() || c.op == Operation::Clear) {
      clearPublished(); soap_.clearServerList();
    } else if (!ok) {
      error_ = memoryFailed ? "memory" : "browse";
      // A failed scan has replaced the library's server indices. Invalidate
      // the old published indices too, instead of offering stale actions.
      if (c.op == Operation::Scan) { clearPublished(); soap_.clearServerList(); error_ = memoryFailed ? "memory" : "scan"; }
    } else if (c.op == Operation::Scan) {
      clearPublished(); servers_.swap(found);
      error_ = servers_.size() ? "" : "no_servers";
    } else {
      entries_.swap(result); objectId_.swap(nextId); heading_.swap(nextHeading);
      server_ = c.server; serverView_ = false; offset_ = c.offset;
      nextOffset_ = offset_ + soap_.lastNumberReturned();
      more_ = soap_.lastNumberReturned() > 0 && (soap_.lastTotalMatches() > 0 ?
              nextOffset_ < soap_.lastTotalMatches() : soap_.lastNumberReturned() == pageSize);
      error_ = ""; ++revision_;
    }
    xSemaphoreGive(mutex_);
    result.reset(); found.reset(); client_.stop(); udp_.stop();
    Serial.printf("[dlna] op=%u ok=%u psram=%u peak=%u internal=%u largest=%u psramFree=%u\n",
       static_cast<unsigned>(c.op), ok && !memoryFailed,
       static_cast<unsigned>(SoapMemory::used.load()), static_cast<unsigned>(SoapMemory::peak.load()),
       static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
       static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
       static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (c.generation != generation_.load()) { clearPublished(); soap_.clearServerList(); }
    busy_ = false;
    xSemaphoreGive(mutex_);
  }
}

DlnaService::State DlnaService::state() {
  State out;
  if (!mutex_) return out;
  xSemaphoreTake(mutex_, portMAX_DELAY);
  out = {busy(), serverView_, more_, server_, serverView_ ? servers_.size() : entries_.size(),
         revision_, offset_, nextOffset_, error_};
  xSemaphoreGive(mutex_);
  return out;
}

String DlnaService::heading() {
  if (!mutex_) return "DLNA";
  xSemaphoreTake(mutex_, portMAX_DELAY);
  String out = serverView_ ? String(Language::text("Médiaszerverek")) : String(heading_.c_str());
  xSemaphoreGive(mutex_);
  return out;
}

String DlnaService::nameAt(size_t index, uint32_t revision) {
  if (!mutex_) return String();
  xSemaphoreTake(mutex_, portMAX_DELAY);
  String out;
  const size_t count = serverView_ ? servers_.size() : entries_.size();
  if (revision == revision_ && index < count) {
    const char* name = serverView_ ? servers_[index].friendlyName.c_str() : entries_[index].name.c_str();
    size_t length = strlen(name);
    if (length > 256) { length = 256; while (length && (static_cast<unsigned char>(name[length]) & 0xc0) == 0x80) --length; }
    out = String(name, length);
    if (serverView_ || entries_[index].isDirectory) out = "[+] " + out;
    else if (!supported(entries_[index])) out = "[-] " + out;
  }
  xSemaphoreGive(mutex_);
  return out;
}

bool DlnaService::selection(size_t index, uint32_t revision, Location& out, bool& directory) {
  if (!mutex_) return false;
  xSemaphoreTake(mutex_, portMAX_DELAY);
  const size_t count = serverView_ ? servers_.size() : entries_.size();
  const bool valid = !busy() && revision == revision_ && index < count;
  if (valid) {
    directory = serverView_ || entries_[index].isDirectory;
    out.server = serverView_ ? index : server_; out.offset = 0;
    out.id = serverView_ ? "0" : entries_[index].id.c_str();
    out.title = serverView_ ? servers_[index].friendlyName.c_str() : entries_[index].name.c_str();
  }
  xSemaphoreGive(mutex_);
  return valid;
}

bool DlnaService::location(Location& out) {
  if (!mutex_) return false;
  xSemaphoreTake(mutex_, portMAX_DELAY);
  const bool valid = !serverView_ && !busy();
  if (valid) { out.server = server_; out.offset = offset_; out.id = objectId_; out.title = heading_; }
  xSemaphoreGive(mutex_);
  return valid;
}

bool DlnaService::showServers() {
  if (!mutex_) return false;
  xSemaphoreTake(mutex_, portMAX_DELAY);
  const bool ok = !busy();
  if (ok) { entries_.reset(); serverView_ = true; more_ = false; offset_ = nextOffset_ = 0; error_ = ""; ++revision_; }
  xSemaphoreGive(mutex_);
  return ok;
}

SoapString DlnaService::json() {
  if (!mutex_) return SoapString("{\"busy\":false,\"revision\":0,\"servers\":true,\"items\":[],\"error\":\"\"}");
  xSemaphoreTake(mutex_, portMAX_DELAY);
  const auto failures = SoapMemory::failures.load();
  SoapString out("{\"busy\":"); out += busy() ? "true" : "false";
  out += ",\"revision\":"; out += number(revision_);
  out += ",\"servers\":"; out += serverView_ ? "true" : "false";
  out += ",\"server\":"; out += number(server_);
  out += ",\"object\":"; out += quoted(objectId_.c_str());
  out += ",\"heading\":"; out += quoted(heading_.c_str(), 256);
  out += ",\"offset\":"; out += number(offset_);
  out += ",\"nextOffset\":"; out += number(nextOffset_);
  out += ",\"more\":"; out += more_ ? "true" : "false";
  out += ",\"error\":"; out += quoted(error_);
  out += ",\"items\":[";
  const size_t count = serverView_ ? servers_.size() : entries_.size();
  for (size_t i = 0; i < count; ++i) {
    if (i) out += ',';
    out += "{\"name\":";
    out += quoted(serverView_ ? servers_[i].friendlyName.c_str() : entries_[i].name.c_str(), 256);
    if (!serverView_) {
      const auto& e = entries_[i];
      out += ",\"id\":"; out += quoted(e.id.c_str());
      out += ",\"artist\":"; out += quoted(e.artist.c_str(), 128);
      out += ",\"directory\":"; out += e.isDirectory ? "true" : "false";
      out += ",\"playable\":"; out += supported(e) ? "true" : "false";
    }
    out += '}';
  }
  out += "],\"dlnaPsram\":"; out += number(SoapMemory::used.load());
  out += ",\"dlnaPeak\":"; out += number(SoapMemory::peak.load());
  out += ",\"budget\":"; out += number(SoapMemory::budget);
  out += ",\"internalFree\":"; out += number(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  out += ",\"internalLargest\":"; out += number(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  out += ",\"psramFree\":"; out += number(heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  out += '}';
  xSemaphoreGive(mutex_);
  if (SoapMemory::failures.load() != failures) return SoapString();
  return out;
}

bool DlnaService::track(const soapObject_t& entry, String& url, String& title) {
  if (!supported(entry) || entry.uri.length() > 2048 || !entry.downloadPort) return false;
  url = "http://" + entry.downloadIp.toString() + ":" + String(entry.downloadPort) + "/" + entry.uri.c_str();
  title = entry.artist.length() ? String(entry.artist.c_str()) + " - " + entry.name.c_str() : String(entry.name.c_str());
  return true;
}

bool DlnaService::play(size_t index, uint32_t revision, String& url, String& title) {
  if (!mutex_) return false;
  xSemaphoreTake(mutex_, portMAX_DELAY);
  if (busy() || serverView_ || revision != revision_ || index >= entries_.size() || !supported(entries_[index])) {
    xSemaphoreGive(mutex_); return false;
  }
  SoapMemory::failed = false;
  soapObjectVect_t queue;
  int selected = -1;
  for (size_t i = 0; i < entries_.size(); ++i) {
    if (!supported(entries_[i])) continue;
    if (i == index) selected = static_cast<int>(queue.size());
    queue.push_back(entries_[i]);
  }
  const bool ok = !SoapMemory::failed.load() && selected >= 0 && track(queue[selected], url, title);
  if (ok) { playing_.swap(queue); playingIndex_ = selected; }
  xSemaphoreGive(mutex_); return ok;
}

bool DlnaService::step(int delta, String& url, String& title) {
  for (int i = playingIndex_ + delta; i >= 0 && i < static_cast<int>(playing_.size()); i += delta) {
    if (track(playing_[i], url, title)) { playingIndex_ = i; return true; }
  }
  return false;
}
bool DlnaService::resume(String& url, String& title) {
  return playingIndex_ >= 0 && playingIndex_ < static_cast<int>(playing_.size()) && track(playing_[playingIndex_], url, title);
}
