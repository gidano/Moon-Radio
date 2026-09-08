#include "NowPlayingScreen.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <LittleFS.h>
#include <LovyanGFX.hpp>
#include <esp_heap_caps.h>
#define LODEPNG_NO_COMPILE_CPP
#include <libs/lodepng/lodepng.h>
#include <time.h>

#include "../../time/DateNameday.h"
#include "options.h"

namespace {

constexpr lv_coord_t kHeaderVolumeLeft = 430;
constexpr lv_coord_t kHeaderVolumeTop = 8;
constexpr lv_coord_t kHeaderVolumeBarsLeft = 434;
constexpr lv_coord_t kHeaderVolumeBarsTop = 8;
constexpr uint32_t kHeaderVolumeActiveColor = 0x7DD3FC;
constexpr uint32_t kHeaderVolumeInactiveColor = 0x334155;

constexpr uint32_t kVolumePopupDurationMs = 1500;
constexpr uint32_t kIpVisibleDurationMs = 10000;
constexpr lv_coord_t kHeaderTitleTop = 6;
constexpr lv_coord_t kHeaderTitleHeight = 24;
constexpr lv_coord_t kBufferBarLeft = 158;
constexpr lv_coord_t kBufferBarTop = 73;
constexpr lv_coord_t kBufferBarWidth = 308;
constexpr lv_coord_t kBufferBarHeight = 3;
constexpr uint32_t kBufferBarUpdateMs = 1000;
constexpr bool kMoonPhaseEnabled = true;
constexpr int kMoonPhaseCount = 10;
constexpr double kPi = 3.14159265358979323846;
constexpr int kMoonImageWidth = 90;
constexpr int kMoonImageHeight = 82;
constexpr lv_coord_t kMoonImageLeft = 264;
constexpr lv_coord_t kMoonImageTop = 237;

void setLabelTextIfChanged(lv_obj_t* label, const char* text) {
  if (!label || !text) return;
  const char* current = lv_label_get_text(label);
  if (!current || strcmp(current, text) != 0) lv_label_set_text(label, text);
}

void setObjectHiddenIfChanged(lv_obj_t* object, bool hidden) {
  if (!object) return;
  const bool isHidden = lv_obj_has_flag(object, LV_OBJ_FLAG_HIDDEN);
  if (hidden == isHidden) return;
  if (hidden)
    lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
  else
    lv_obj_remove_flag(object, LV_OBJ_FLAG_HIDDEN);
}

void updateHeaderVolumeLabel(lv_obj_t* label, const char* text) {
  if (!label || !text) return;
  const char* current = lv_label_get_text(label);
  const bool textChanged = !current || strcmp(current, text) != 0;
  const bool needsPosition = lv_obj_get_x(label) != kHeaderVolumeLeft ||
                             lv_obj_get_y(label) != kHeaderVolumeTop;
#if DISPLAY_PROFILE_AXS15231B
  if (!textChanged && !needsPosition) return;
#endif
  if (textChanged) lv_label_set_text(label, text);
  lv_obj_set_width(label, LV_SIZE_CONTENT);
  lv_obj_update_layout(label);
  lv_obj_set_pos(label, kHeaderVolumeLeft, kHeaderVolumeTop);
}

uint8_t volumeToBars(uint8_t value) {
  if (value == 0) return 0;
  if (value <= 4) return 1;
  if (value <= 8) return 2;
  if (value <= 12) return 3;
  if (value <= 16) return 4;
  return 5;
}

int extractWifiRssi(const String& wifiText) {
  for (int i = 0; i < wifiText.length(); ++i) {
    const char ch = wifiText[i];
    if (ch == '-' || (ch >= '0' && ch <= '9')) {
      return wifiText.substring(i).toInt();
    }
  }
  return -1000;
}

bool pointInsideObject(lv_obj_t* object, const lv_point_t& point) {
  if (!object || lv_obj_has_flag(object, LV_OBJ_FLAG_HIDDEN)) return false;
  lv_area_t area{};
  lv_obj_get_coords(object, &area);
  return point.x >= area.x1 && point.x <= area.x2 && point.y >= area.y1 &&
         point.y <= area.y2;
}

int moonPhaseIndex(const tm& localTime) {
  tm moonTime = localTime;
  moonTime.tm_hour = 12;
  moonTime.tm_min = 0;
  moonTime.tm_sec = 0;
  const time_t current = mktime(&moonTime);
  if (current <= 0) return 0;

  // Reference new moon: 2026-08-12 17:36 UTC. The image set uses ten visual
  // states, with extra waxing/waning near-full images around the full moon.
  constexpr double kReferenceNewMoon = 1786556160.0;
  constexpr double kSynodicMonthDays = 29.53058867;
  double days = (static_cast<double>(current) - kReferenceNewMoon) / 86400.0;
  double cycle = days / kSynodicMonthDays;
  double phase = cycle - floor(cycle);
  if (phase < 0.0) phase += 1.0;
  const double ageDays = phase * kSynodicMonthDays;

  if (ageDays < 1.7 || ageDays >= 28.3) return 0;
  if (ageDays < 4.7) return 1;
  if (ageDays < 7.4) return 2;
  if (ageDays < 10.4) return 3;
  if (ageDays < 13.2) return 4;
  if (ageDays < 17.0) return 5;
  if (ageDays < 19.7) return 6;
  if (ageDays < 22.6) return 7;
  if (ageDays < 25.5) return 8;
  return 9;
}

uint8_t moonAlphaFromPixel(uint8_t r, uint8_t g, uint8_t b, uint8_t pngAlpha,
                           int x, int y) {
  constexpr float kMoonCenterX = (kMoonImageWidth - 1) * 0.5f;
  constexpr float kMoonCenterY = (kMoonImageHeight - 1) * 0.5f;
  constexpr float kMoonRadiusX = 40.5f;
  constexpr float kMoonRadiusY = 39.0f;
  const float dx = (static_cast<float>(x) - kMoonCenterX) / kMoonRadiusX;
  const float dy = (static_cast<float>(y) - kMoonCenterY) / kMoonRadiusY;
  const bool insideMoonDisc = (dx * dx + dy * dy) <= 1.0f;
  if (insideMoonDisc) return pngAlpha;

  const float lum = 0.299f * r + 0.587f * g + 0.114f * b;
  if (lum <= 2.0f) return 0;
  if (lum >= 14.0f) return pngAlpha;
  const float t = (lum - 2.0f) / 10.0f;
  const float smooth = t * t * (3.0f - 2.0f * t);
  return static_cast<uint8_t>(pngAlpha * smooth + 0.5f);
}

}  // namespace

void NowPlayingScreen::create(FontManager& fonts,
                              const NowPlayingActions& actions) {
  fonts_ = &fonts;
  actions_ = actions;

  // The LVGL object is recreated by the display manager in several modes.
  // The previous source must not suppress assigning the source to the new
  // image object.
  currentLogo_.clear();
  logoPath_.clear();
  currentMoon_.clear();
  moonPath_.clear();
  lastMoonPhase_ = -1;
  currentBufferPercent_ = 255;
  lastBufferBarUpdateAt_ = 0;
  if (moonPixels_) {
    free(moonPixels_);
    moonPixels_ = nullptr;
    moonDescriptor_ = {};
  }
  if (logoPixels_) {
    free(logoPixels_);
    logoPixels_ = nullptr;
    logoDescriptor_ = {};
  }
  lv_obj_t* screen = lv_screen_active();
  lv_obj_clean(screen);
  lv_obj_set_style_bg_color(screen, lv_color_hex(0x080B12), 0);
  lv_obj_set_style_text_color(screen, lv_color_hex(0xF1F5F9), 0);
  lv_obj_set_style_text_font(screen, fonts.regular(), 0);
  lv_obj_add_flag(screen, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(screen, screenTapEvent, LV_EVENT_PRESSED, this);
  lv_obj_add_event_cb(screen, screenTapEvent, LV_EVENT_RELEASED, this);

  backgroundImage_ = lv_image_create(screen);
  lv_obj_set_pos(backgroundImage_, 0, 0);
  lv_obj_set_size(backgroundImage_, 480, 320);
  lv_obj_clear_flag(backgroundImage_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(backgroundImage_, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t* top = lv_obj_create(screen);
  lv_obj_set_size(top, 480, 36);
  lv_obj_set_pos(top, 0, 0);
  lv_obj_set_style_bg_opa(top, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(top, 0, 0);
  lv_obj_set_style_radius(top, 0, 0);
  lv_obj_set_style_pad_all(top, 0, 0);
  lv_obj_clear_flag(top, LV_OBJ_FLAG_SCROLLABLE);

  wifiLabel_ = lv_label_create(screen);
  lv_label_set_text(wifiLabel_, "Wi-Fi...");
  lv_obj_set_pos(wifiLabel_, 8, 8);
  lv_obj_set_width(wifiLabel_, 118);
  lv_obj_set_height(wifiLabel_, 20);
  lv_obj_set_style_text_color(wifiLabel_, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(wifiLabel_, fonts.small(), 0);
  lv_label_set_long_mode(wifiLabel_, LV_LABEL_LONG_DOT);
  lv_obj_add_flag(wifiLabel_, LV_OBJ_FLAG_HIDDEN);

  wifiTouchArea_ = lv_obj_create(screen);
  lv_obj_set_pos(wifiTouchArea_, 4, 4);
  lv_obj_set_size(wifiTouchArea_, 138, 28);
  lv_obj_set_style_bg_opa(wifiTouchArea_, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(wifiTouchArea_, 0, 0);
  lv_obj_set_style_radius(wifiTouchArea_, 0, 0);
  lv_obj_set_style_pad_all(wifiTouchArea_, 0, 0);
  lv_obj_clear_flag(wifiTouchArea_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(wifiTouchArea_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(wifiTouchArea_, wifiEvent, LV_EVENT_CLICKED, this);
  lv_obj_move_foreground(wifiTouchArea_);

  for (uint8_t index = 0; index < 4; ++index) {
    wifiSignalBars_[index] = lv_obj_create(screen);
    lv_obj_set_size(wifiSignalBars_[index], 5, 6 + index * 3);
    lv_obj_set_pos(wifiSignalBars_[index], 10 + index * 8, 20 - index * 3);
    lv_obj_set_style_bg_color(wifiSignalBars_[index], lv_color_hex(0x334155), 0);
    lv_obj_set_style_bg_opa(wifiSignalBars_[index], LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(wifiSignalBars_[index], 0, 0);
    lv_obj_set_style_radius(wifiSignalBars_[index], 2, 0);
    lv_obj_set_style_pad_all(wifiSignalBars_[index], 0, 0);
    lv_obj_clear_flag(wifiSignalBars_[index], LV_OBJ_FLAG_SCROLLABLE);
  }

  appLabel_ = lv_label_create(screen);
  lv_label_set_text(appLabel_, "Moon Radio");
  lv_obj_set_pos(appLabel_, 158, kHeaderTitleTop);
  lv_obj_set_width(appLabel_, 160);
  lv_obj_set_height(appLabel_, kHeaderTitleHeight);
  lv_obj_set_style_text_color(appLabel_, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(appLabel_, fonts.small(), 0);
  lv_obj_set_style_text_align(appLabel_, LV_TEXT_ALIGN_CENTER, 0);

  appTouchArea_ = lv_obj_create(screen);
  lv_obj_set_pos(appTouchArea_, 150, 4);
  lv_obj_set_size(appTouchArea_, 176, 28);
  lv_obj_set_style_bg_opa(appTouchArea_, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(appTouchArea_, 0, 0);
  lv_obj_set_style_radius(appTouchArea_, 0, 0);
  lv_obj_set_style_pad_all(appTouchArea_, 0, 0);
  lv_obj_clear_flag(appTouchArea_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(appTouchArea_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(appTouchArea_, clockAreaEvent, LV_EVENT_CLICKED, this);
  lv_obj_move_foreground(appTouchArea_);

  bufferLabel_ = lv_label_create(screen);
  lv_label_set_text(bufferLabel_, "");
  lv_obj_add_flag(bufferLabel_, LV_OBJ_FLAG_HIDDEN);

  clockLabel_ = lv_label_create(screen);
  lv_label_set_text(clockLabel_, "--:--");
  lv_obj_set_pos(clockLabel_, 350, 286);
  lv_obj_set_width(clockLabel_, 92);
  lv_obj_set_height(clockLabel_, 28);
  lv_obj_set_style_text_color(clockLabel_, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(clockLabel_, fonts.large(), 0);
  lv_obj_set_style_text_align(clockLabel_, LV_TEXT_ALIGN_RIGHT, 0);

  ipLabel_ = lv_label_create(screen);
  lv_label_set_text(ipLabel_, "IP: --.--.--.--");
  lv_obj_set_pos(ipLabel_, 128, kHeaderTitleTop);
  lv_obj_set_width(ipLabel_, 224);
  lv_obj_set_height(ipLabel_, kHeaderTitleHeight);
  lv_obj_set_style_text_color(ipLabel_, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(ipLabel_, fonts.small(), 0);
  lv_obj_set_style_text_align(ipLabel_, LV_TEXT_ALIGN_CENTER, 0);

  clockSecondsLabel_ = lv_label_create(screen);
  lv_label_set_text(clockSecondsLabel_, "00");
  lv_obj_set_pos(clockSecondsLabel_, 444, 288);
  lv_obj_set_width(clockSecondsLabel_, 34);
  lv_obj_set_height(clockSecondsLabel_, 18);
  lv_obj_set_style_text_color(clockSecondsLabel_, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(clockSecondsLabel_, fonts.small(), 0);
  lv_obj_set_style_text_align(clockSecondsLabel_, LV_TEXT_ALIGN_LEFT, 0);

  clockSecondsUnderline_ = lv_obj_create(screen);
  lv_obj_set_pos(clockSecondsUnderline_, 444, 308);
  lv_obj_set_size(clockSecondsUnderline_, 24, 1);
  lv_obj_set_style_bg_color(clockSecondsUnderline_, lv_color_hex(0x94A3B8), 0);
  lv_obj_set_style_border_width(clockSecondsUnderline_, 0, 0);
  lv_obj_set_style_radius(clockSecondsUnderline_, 0, 0);
  lv_obj_set_style_pad_all(clockSecondsUnderline_, 0, 0);
  lv_obj_clear_flag(clockSecondsUnderline_, LV_OBJ_FLAG_SCROLLABLE);

  clockTouchArea_ = lv_obj_create(screen);
  lv_obj_set_pos(clockTouchArea_, 286, 282);
  lv_obj_set_size(clockTouchArea_, 180, 34);
  lv_obj_set_style_bg_opa(clockTouchArea_, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(clockTouchArea_, 0, 0);
  lv_obj_set_style_radius(clockTouchArea_, 0, 0);
  lv_obj_set_style_pad_all(clockTouchArea_, 0, 0);
  lv_obj_clear_flag(clockTouchArea_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(clockTouchArea_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(clockTouchArea_);

  volumeLabel_ = lv_label_create(top);
  lv_label_set_text(volumeLabel_, "8/21");
  lv_obj_set_height(volumeLabel_, 20);
  lv_obj_set_style_text_color(volumeLabel_, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(volumeLabel_, fonts.small(), 0);
  lv_obj_set_style_text_align(volumeLabel_, LV_TEXT_ALIGN_LEFT, 0);
  lv_obj_set_style_bg_opa(volumeLabel_, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(volumeLabel_, 0, 0);
  lv_obj_set_style_pad_all(volumeLabel_, 0, 0);
  lv_obj_add_flag(volumeLabel_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(volumeLabel_, 8);
  lv_obj_add_event_cb(volumeLabel_, headerVolumeEvent, LV_EVENT_CLICKED, this);
  updateHeaderVolumeLabel(volumeLabel_, "8/21");

  headerVolumeIcon_ = lv_label_create(top);
  lv_label_set_text(headerVolumeIcon_, LV_SYMBOL_VOLUME_MAX);
  lv_obj_set_pos(headerVolumeIcon_, 384, 6);
  lv_obj_set_width(headerVolumeIcon_, LV_SIZE_CONTENT);
  lv_obj_set_height(headerVolumeIcon_, 22);
  lv_obj_set_style_text_color(headerVolumeIcon_, lv_color_hex(0xE2E8F0), 0);
  lv_obj_set_style_text_font(headerVolumeIcon_, fonts.symbol(), 0);
  lv_obj_add_flag(headerVolumeIcon_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(headerVolumeIcon_, 6);
  lv_obj_add_event_cb(headerVolumeIcon_, headerVolumeEvent, LV_EVENT_CLICKED,
                      this);

  for (uint8_t index = 0; index < 5; ++index) {
    volumeBars_[index] = lv_obj_create(top);
    lv_obj_set_size(volumeBars_[index], 5, 6 + index * 2);
    lv_obj_set_pos(volumeBars_[index], kHeaderVolumeBarsLeft + index * 7,
                   kHeaderVolumeBarsTop + (12 - index * 2));
    lv_obj_set_style_bg_color(volumeBars_[index],
                              lv_color_hex(kHeaderVolumeInactiveColor), 0);
    lv_obj_set_style_bg_opa(volumeBars_[index], LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(volumeBars_[index], 0, 0);
    lv_obj_set_style_radius(volumeBars_[index], 2, 0);
    lv_obj_set_style_pad_all(volumeBars_[index], 0, 0);
    lv_obj_clear_flag(volumeBars_[index], LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(volumeBars_[index], LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(volumeBars_[index], headerVolumeEvent, LV_EVENT_CLICKED,
                        this);
  }

  headerVolumeTouchArea_ = lv_obj_create(top);
  lv_obj_set_pos(headerVolumeTouchArea_, 376, 4);
  lv_obj_set_size(headerVolumeTouchArea_, 100, 28);
  lv_obj_set_style_bg_opa(headerVolumeTouchArea_, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(headerVolumeTouchArea_, 0, 0);
  lv_obj_set_style_radius(headerVolumeTouchArea_, 0, 0);
  lv_obj_set_style_pad_all(headerVolumeTouchArea_, 0, 0);
  lv_obj_clear_flag(headerVolumeTouchArea_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(headerVolumeTouchArea_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(headerVolumeTouchArea_, headerVolumeEvent,
                      LV_EVENT_CLICKED, this);
  lv_obj_move_foreground(headerVolumeTouchArea_);

  logoPanel_ = lv_obj_create(screen);
  lv_obj_set_size(logoPanel_, 128, 128);
  lv_obj_set_pos(logoPanel_, 14, 48);
  lv_obj_set_style_bg_opa(logoPanel_, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(logoPanel_, 0, 0);
  lv_obj_set_style_radius(logoPanel_, 12, 0);
  lv_obj_set_style_clip_corner(logoPanel_, true, 0);
  lv_obj_set_style_pad_all(logoPanel_, 0, 0);
  lv_obj_clear_flag(logoPanel_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(logoPanel_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(logoPanel_, logoPanelEvent, LV_EVENT_CLICKED, this);

  logoImage_ = lv_image_create(logoPanel_);
  lv_obj_center(logoImage_);
  lv_obj_add_flag(logoImage_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(logoImage_, logoPanelEvent, LV_EVENT_CLICKED, this);
  lv_image_set_scale(logoImage_, 384);

  diagnosticsLabel_ = lv_label_create(logoPanel_);
  lv_obj_set_pos(diagnosticsLabel_, 8, 7);
  lv_obj_set_size(diagnosticsLabel_, 112, 118);
  lv_obj_set_style_bg_opa(diagnosticsLabel_, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(diagnosticsLabel_, 0, 0);
  lv_obj_set_style_pad_all(diagnosticsLabel_, 0, 0);
  lv_obj_set_style_text_font(diagnosticsLabel_, fonts.compact(), 0);
  lv_obj_set_style_text_color(diagnosticsLabel_, lv_color_hex(0xCBD5E1), 0);
  lv_obj_set_style_text_line_space(diagnosticsLabel_, 0, 0);
  lv_obj_add_flag(diagnosticsLabel_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(diagnosticsLabel_, logoPanelEvent, LV_EVENT_CLICKED,
                      this);
  lv_obj_add_flag(diagnosticsLabel_, LV_OBJ_FLAG_HIDDEN);
  lv_label_set_text(diagnosticsLabel_,
                    "DIAG\nBUFFER --\nCPU0  --%\nCPU1  --%\nTEMP  --.-C\nRAM   ----k\nPSRAM ----k");

  logoTouchArea_ = lv_obj_create(logoPanel_);
  lv_obj_set_pos(logoTouchArea_, 0, 0);
  lv_obj_set_size(logoTouchArea_, 128, 128);
  lv_obj_set_style_bg_opa(logoTouchArea_, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(logoTouchArea_, 0, 0);
  lv_obj_set_style_radius(logoTouchArea_, 0, 0);
  lv_obj_set_style_pad_all(logoTouchArea_, 0, 0);
  lv_obj_clear_flag(logoTouchArea_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(logoTouchArea_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(logoTouchArea_, logoPanelEvent, LV_EVENT_CLICKED, this);
  lv_obj_move_foreground(logoTouchArea_);

  stationLabel_ = lv_label_create(screen);
  lv_obj_set_pos(stationLabel_, 158, 40);
  lv_obj_set_width(stationLabel_, 308);
  lv_obj_set_height(stationLabel_, 34);
  lv_obj_set_style_text_font(stationLabel_, fonts.large(), 0);
  lv_label_set_long_mode(stationLabel_, LV_LABEL_LONG_DOT);
  lv_label_set_text(stationLabel_, "Állomás betöltése");
  lv_obj_add_flag(stationLabel_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(stationLabel_, 8);
  lv_obj_add_event_cb(stationLabel_, stationEvent, LV_EVENT_CLICKED, this);

  bufferBarTrack_ = lv_obj_create(screen);
  lv_obj_set_pos(bufferBarTrack_, kBufferBarLeft, kBufferBarTop);
  lv_obj_set_size(bufferBarTrack_, kBufferBarWidth, kBufferBarHeight);
  lv_obj_set_style_bg_color(bufferBarTrack_, lv_color_hex(0x1E293B), 0);
  lv_obj_set_style_bg_opa(bufferBarTrack_, LV_OPA_60, 0);
  lv_obj_set_style_border_width(bufferBarTrack_, 0, 0);
  lv_obj_set_style_radius(bufferBarTrack_, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_pad_all(bufferBarTrack_, 0, 0);
  lv_obj_clear_flag(bufferBarTrack_, LV_OBJ_FLAG_SCROLLABLE);

  bufferBarFill_ = lv_obj_create(bufferBarTrack_);
  lv_obj_set_pos(bufferBarFill_, 0, 0);
  lv_obj_set_size(bufferBarFill_, 0, kBufferBarHeight);
  lv_obj_set_style_bg_color(bufferBarFill_, lv_color_hex(0x7DD3FC), 0);
  lv_obj_set_style_bg_opa(bufferBarFill_, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(bufferBarFill_, 0, 0);
  lv_obj_set_style_radius(bufferBarFill_, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_pad_all(bufferBarFill_, 0, 0);
  lv_obj_clear_flag(bufferBarFill_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(bufferBarFill_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(bufferBarTrack_, LV_OBJ_FLAG_HIDDEN);

  titleLabel_ = lv_label_create(screen);
  lv_obj_set_pos(titleLabel_, 158, 76);
  lv_obj_set_width(titleLabel_, 308);
  // Keep metadata within exactly three complete text rows on every display.
  // A fixed pixel height let the top of a fourth Roboto row show through
  // above the audio-information line on some profiles.
  const lv_coord_t titleHeight =
      static_cast<lv_coord_t>(3 * lv_font_get_line_height(fonts.regular()));
  lv_obj_set_height(titleLabel_, titleHeight);
  lv_obj_set_style_max_height(titleLabel_, titleHeight, 0);
  lv_obj_remove_flag(titleLabel_, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  lv_label_set_long_mode(titleLabel_, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_color(titleLabel_, lv_color_hex(0xCBD5E1), 0);
  lv_obj_set_style_text_font(titleLabel_, fonts.regular(), 0);
  lv_label_set_text(titleLabel_, "");

  infoLabel_ = lv_label_create(screen);
  lv_obj_set_pos(infoLabel_, 158, 157);
  lv_obj_set_width(infoLabel_, 308);
  lv_obj_set_style_text_color(infoLabel_, lv_color_hex(0x7DD3FC), 0);
  lv_obj_set_style_text_font(infoLabel_, fonts.regular(), 0);
  lv_label_set_long_mode(infoLabel_, LV_LABEL_LONG_DOT);
  lv_label_set_text(infoLabel_, "Kapcsolódás...");

  dateLabel_ = lv_label_create(screen);
  lv_obj_set_pos(dateLabel_, 14, 188);                                  // dátum sor pozíció magasság
  lv_obj_set_width(dateLabel_, LV_SIZE_CONTENT);
  lv_obj_set_height(dateLabel_, 24);
  lv_obj_set_style_text_color(dateLabel_, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(dateLabel_, fonts.regular(), 0);
  lv_obj_set_style_text_line_space(dateLabel_, 0, 0);
  lv_obj_set_style_text_align(dateLabel_, LV_TEXT_ALIGN_LEFT, 0);
  lv_label_set_text(dateLabel_, "--");

  namedayLabel_ = lv_label_create(screen);
  lv_obj_set_pos(namedayLabel_, 14, 188);                               // névnap sor pozíció magasság
  lv_obj_set_width(namedayLabel_, 452);
  lv_obj_set_height(namedayLabel_, 24);
  lv_obj_set_style_text_color(namedayLabel_, lv_color_hex(0xFFD54A), 0);
  lv_obj_set_style_text_font(namedayLabel_, fonts.regular(), 0);
  lv_obj_set_style_text_line_space(namedayLabel_, 0, 0);
  lv_obj_set_style_text_align(namedayLabel_, LV_TEXT_ALIGN_LEFT, 0);
  lv_label_set_long_mode(namedayLabel_, LV_LABEL_LONG_DOT);
  lv_label_set_text(namedayLabel_, "");

  weatherIconHost_ = lv_obj_create(screen);
  lv_obj_set_pos(weatherIconHost_, 14, 209);
  lv_obj_set_size(weatherIconHost_, 48, 48);
  lv_obj_set_style_bg_opa(weatherIconHost_, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(weatherIconHost_, 0, 0);
  lv_obj_set_style_radius(weatherIconHost_, 0, 0);
  lv_obj_set_style_pad_all(weatherIconHost_, 0, 0);
  lv_obj_clear_flag(weatherIconHost_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(weatherIconHost_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(weatherIconHost_, 4);
  lv_obj_add_event_cb(weatherIconHost_, weatherEvent, LV_EVENT_CLICKED, this);
  lv_obj_add_event_cb(weatherIconHost_, weatherEvent, LV_EVENT_PRESSED, this);

  weatherIconImage_ = lv_image_create(weatherIconHost_);
  lv_obj_set_pos(weatherIconImage_, 0, 0);
  lv_obj_set_size(weatherIconImage_, 48, 48);
  lv_obj_clear_flag(weatherIconImage_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(weatherIconImage_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(weatherIconImage_, weatherEvent, LV_EVENT_CLICKED, this);
  lv_obj_add_event_cb(weatherIconImage_, weatherEvent, LV_EVENT_PRESSED, this);

  weatherSunCenter_ = lv_obj_create(weatherIconHost_);
  lv_obj_set_size(weatherSunCenter_, 20, 20);
  lv_obj_set_pos(weatherSunCenter_, 11, 11);
  lv_obj_set_style_bg_color(weatherSunCenter_, lv_color_hex(0xFACC15), 0);
  lv_obj_set_style_bg_opa(weatherSunCenter_, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(weatherSunCenter_, 0, 0);
  lv_obj_set_style_radius(weatherSunCenter_, LV_RADIUS_CIRCLE, 0);
  lv_obj_add_flag(weatherSunCenter_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(weatherSunCenter_, weatherEvent, LV_EVENT_CLICKED, this);
  for (uint8_t i = 0; i < 4; ++i) {
    weatherSunRays_[i] = lv_obj_create(weatherIconHost_);
    lv_obj_set_style_bg_color(weatherSunRays_[i], lv_color_hex(0xFACC15), 0);
    lv_obj_set_style_bg_opa(weatherSunRays_[i], LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(weatherSunRays_[i], 0, 0);
    lv_obj_set_style_radius(weatherSunRays_[i], 1, 0);
    lv_obj_add_flag(weatherSunRays_[i], LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(weatherSunRays_[i], weatherEvent, LV_EVENT_CLICKED, this);
  }
  lv_obj_set_size(weatherSunRays_[0], 4, 10);
  lv_obj_set_pos(weatherSunRays_[0], 19, 0);
  lv_obj_set_size(weatherSunRays_[1], 4, 10);
  lv_obj_set_pos(weatherSunRays_[1], 19, 32);
  lv_obj_set_size(weatherSunRays_[2], 10, 4);
  lv_obj_set_pos(weatherSunRays_[2], 0, 19);
  lv_obj_set_size(weatherSunRays_[3], 10, 4);
  lv_obj_set_pos(weatherSunRays_[3], 32, 19);

  weatherCloudBase_ = lv_obj_create(weatherIconHost_);
  lv_obj_set_size(weatherCloudBase_, 28, 11);
  lv_obj_set_pos(weatherCloudBase_, 7, 23);
  lv_obj_set_style_bg_color(weatherCloudBase_, lv_color_hex(0xCBD5E1), 0);
  lv_obj_set_style_bg_opa(weatherCloudBase_, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(weatherCloudBase_, 0, 0);
  lv_obj_set_style_radius(weatherCloudBase_, 5, 0);
  lv_obj_add_flag(weatherCloudBase_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(weatherCloudBase_, weatherEvent, LV_EVENT_CLICKED, this);
  for (uint8_t i = 0; i < 2; ++i) {
    weatherCloudPuffs_[i] = lv_obj_create(weatherIconHost_);
    lv_obj_set_style_bg_color(weatherCloudPuffs_[i], lv_color_hex(0xCBD5E1), 0);
    lv_obj_set_style_bg_opa(weatherCloudPuffs_[i], LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(weatherCloudPuffs_[i], 0, 0);
    lv_obj_set_style_radius(weatherCloudPuffs_[i], LV_RADIUS_CIRCLE, 0);
    lv_obj_add_flag(weatherCloudPuffs_[i], LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(weatherCloudPuffs_[i], weatherEvent, LV_EVENT_CLICKED, this);
  }
  lv_obj_set_size(weatherCloudPuffs_[0], 18, 18);
  lv_obj_set_pos(weatherCloudPuffs_[0], 9, 11);
  lv_obj_set_size(weatherCloudPuffs_[1], 16, 16);
  lv_obj_set_pos(weatherCloudPuffs_[1], 22, 13);

  for (uint8_t i = 0; i < 2; ++i) {
    weatherRainDrops_[i] = lv_obj_create(weatherIconHost_);
    lv_obj_set_size(weatherRainDrops_[i], 5, 11);
    lv_obj_set_style_bg_color(weatherRainDrops_[i], lv_color_hex(0x38BDF8), 0);
    lv_obj_set_style_bg_opa(weatherRainDrops_[i], LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(weatherRainDrops_[i], 0, 0);
    lv_obj_set_style_radius(weatherRainDrops_[i], 2, 0);
    lv_obj_add_flag(weatherRainDrops_[i], LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(weatherRainDrops_[i], weatherEvent, LV_EVENT_CLICKED, this);
  }
  lv_obj_set_pos(weatherRainDrops_[0], 13, 28);
  lv_obj_set_pos(weatherRainDrops_[1], 24, 28);

  weatherLabel_ = lv_label_create(screen);
  lv_obj_set_pos(weatherLabel_, 72, 215);
  lv_obj_set_width(weatherLabel_, 384);
  lv_obj_set_height(weatherLabel_, 20);
  lv_obj_set_style_text_color(weatherLabel_, lv_color_hex(0xC0FEDD), 0);	            // időjárás sor szín
  lv_obj_set_style_text_font(weatherLabel_, fonts.small(), 0);
  lv_obj_set_style_text_align(weatherLabel_, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(weatherLabel_, LV_LABEL_LONG_DOT);
  lv_label_set_text(weatherLabel_, "");
  lv_obj_add_flag(weatherIconHost_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(weatherLabel_, LV_OBJ_FLAG_HIDDEN);

  weatherTouchArea_ = lv_obj_create(screen);
  lv_obj_set_pos(weatherTouchArea_, 14, 209);
  lv_obj_set_size(weatherTouchArea_, 48, 48);
  lv_obj_set_style_bg_opa(weatherTouchArea_, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(weatherTouchArea_, 0, 0);
  lv_obj_set_style_radius(weatherTouchArea_, 0, 0);
  lv_obj_set_style_pad_all(weatherTouchArea_, 0, 0);
  lv_obj_clear_flag(weatherTouchArea_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(weatherTouchArea_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(weatherTouchArea_, 8);
  lv_obj_add_event_cb(weatherTouchArea_, weatherEvent, LV_EVENT_CLICKED, this);
  lv_obj_add_event_cb(weatherTouchArea_, weatherEvent, LV_EVENT_PRESSED, this);
  lv_obj_move_foreground(weatherTouchArea_);

  volumePopup_ = lv_obj_create(screen);
  lv_obj_set_size(volumePopup_, 336, 112);
  lv_obj_set_pos(volumePopup_, 72, 104);
  lv_obj_set_style_bg_color(volumePopup_, lv_color_hex(0x020617), 0);
  lv_obj_set_style_bg_opa(volumePopup_, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(volumePopup_, lv_color_hex(0x475569), 0);
  lv_obj_set_style_border_width(volumePopup_, 2, 0);
  lv_obj_set_style_radius(volumePopup_, 16, 0);
  lv_obj_set_style_pad_all(volumePopup_, 0, 0);
  lv_obj_set_style_shadow_width(volumePopup_, 18, 0);
  lv_obj_set_style_shadow_opa(volumePopup_, LV_OPA_40, 0);
  lv_obj_set_style_shadow_color(volumePopup_, lv_color_hex(0x000000), 0);
  lv_obj_clear_flag(volumePopup_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(volumePopup_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(volumePopup_);

  lv_obj_t* volumeTitle = lv_label_create(volumePopup_);
  lv_label_set_text(volumeTitle,
                    reinterpret_cast<const char*>(u8"HANGER\u0150"));
  lv_obj_set_pos(volumeTitle, 16, 10);
  lv_obj_set_width(volumeTitle, 160);
  lv_obj_set_style_text_color(volumeTitle, lv_color_hex(0x94A3B8), 0);
  lv_obj_set_style_text_font(volumeTitle, fonts.regular(), 0);

  volumePopupIcon_ = lv_label_create(volumePopup_);
  lv_label_set_text(volumePopupIcon_, LV_SYMBOL_VOLUME_MAX);
  lv_obj_set_pos(volumePopupIcon_, 18, 44);
  lv_obj_set_style_text_color(volumePopupIcon_, lv_color_hex(0xE2E8F0), 0);
  lv_obj_set_style_text_font(volumePopupIcon_, fonts.symbol(), 0);

  volumePopupValueLabel_ = lv_label_create(volumePopup_);
  lv_label_set_text(volumePopupValueLabel_, "8/21");
  lv_obj_set_pos(volumePopupValueLabel_, 260, 12);
  lv_obj_set_width(volumePopupValueLabel_, 60);
  lv_obj_set_style_text_align(volumePopupValueLabel_, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_style_text_color(volumePopupValueLabel_, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(volumePopupValueLabel_, fonts.small(), 0);

  volumeSlider_ = lv_slider_create(volumePopup_);
  lv_obj_set_size(volumeSlider_, 250, 14);
  lv_obj_set_pos(volumeSlider_, 54, 52);
  lv_slider_set_range(volumeSlider_, 0, 21);
  lv_obj_set_style_bg_color(volumeSlider_, lv_color_hex(0x1E293B), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(volumeSlider_, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(volumeSlider_, 7, LV_PART_MAIN);
  lv_obj_set_style_bg_color(volumeSlider_, lv_color_hex(0x22D3EE), LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(volumeSlider_, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_obj_set_style_radius(volumeSlider_, 7, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(volumeSlider_, lv_color_hex(0xFFFFFF), LV_PART_KNOB);
  lv_obj_set_style_bg_opa(volumeSlider_, LV_OPA_COVER, LV_PART_KNOB);
  lv_obj_set_style_radius(volumeSlider_, LV_RADIUS_CIRCLE, LV_PART_KNOB);
  lv_obj_set_style_pad_all(volumeSlider_, 6, LV_PART_KNOB);
  lv_obj_add_event_cb(volumeSlider_, volumeSliderEvent, LV_EVENT_VALUE_CHANGED, this);
  lv_obj_add_event_cb(volumeSlider_, volumeSliderEvent, LV_EVENT_RELEASED, this);

  vu_.create(screen);
  vu_.setModeChangedCallback(actions_.context,
                             actions_.visualizerModeChanged);

  moonHost_ = lv_obj_create(screen);
  lv_obj_set_pos(moonHost_, kMoonImageLeft, kMoonImageTop);
  lv_obj_set_size(moonHost_, kMoonImageWidth, kMoonImageHeight);
  lv_obj_set_style_bg_opa(moonHost_, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(moonHost_, 0, 0);
  lv_obj_set_style_radius(moonHost_, 0, 0);
  lv_obj_set_style_pad_all(moonHost_, 0, 0);
  lv_obj_clear_flag(moonHost_, LV_OBJ_FLAG_SCROLLABLE);

  moonImage_ = lv_image_create(moonHost_);
  lv_obj_center(moonImage_);
  lv_obj_clear_flag(moonImage_, LV_OBJ_FLAG_SCROLLABLE);
  lv_image_set_scale(moonImage_, 256);

  loadMoonPhasePng(0);
  updateLogo("nologo");
  showDiagnostics(false);
  ipVisible_ = true;
  volumeGraphicVisible_ = true;
  ipVisibleUntil_ = millis() + kIpVisibleDurationMs;
  updateClockIpVisibility(millis());
  updateHeaderVolumeDisplay();
}

lv_obj_t* NowPlayingScreen::makeButton(
    lv_obj_t* parent, const char* text, int x, const lv_font_t* font,
    lv_event_cb_t callback, NowPlayingScreen* screen) {
  lv_obj_t* button = lv_button_create(parent);
  lv_obj_set_size(button, 110, 42);
  lv_obj_set_pos(button, x, 208);
  lv_obj_set_style_bg_color(button, lv_color_hex(0x1E293B), 0);
  lv_obj_set_style_radius(button, 12, 0);
  lv_obj_set_ext_click_area(button, 8);
  lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, screen);

  lv_obj_t* label = lv_label_create(button);
  lv_obj_set_style_text_font(label, font, 0);
  lv_label_set_text(label, text);
  lv_obj_center(label);
  return button;
}

void NowPlayingScreen::update(const AudioSnapshot& audio,
                              const String& logoName,
                              const String& wifiText, const String& ipText,
                              const WeatherSnapshot& weather,
                              const DiagnosticsSnapshot& diagnostics,
                              bool backgroundEnabled,
                              const String& backgroundPath,
                              uint8_t backgroundOpacity) {
  if (!stationLabel_) return;
  updateBackground(backgroundEnabled, backgroundPath, backgroundOpacity);
  setLabelTextIfChanged(stationLabel_, audio.stationName.c_str());
  const bool playbackStopped =
      audio.paused || (!audio.running && !audio.connecting);
  String displayTitle;
  if (playbackStopped) {
    displayTitle = audio.statusText;
    displayTitle.trim();
    if (displayTitle == "Leállítva") displayTitle = "";
  } else {
    displayTitle = audio.streamTitle;
  }
  setLabelTextIfChanged(titleLabel_, displayTitle.c_str());
  char volumeText[8];
  snprintf(volumeText, sizeof(volumeText), "%u/21", audio.volume);
  if (currentVolume_ != audio.volume) {
    currentVolume_ = audio.volume;
    if (volumeSlider_) {
      sliderSyncing_ = true;
      lv_slider_set_value(volumeSlider_, currentVolume_, LV_ANIM_OFF);
      sliderSyncing_ = false;
    }
    setLabelTextIfChanged(volumePopupValueLabel_, volumeText);
    updateHeaderVolumeDisplay();
  }
  updateHeaderVolumeLabel(volumeLabel_, volumeText);
  const uint32_t bufferKb = (audio.bufferFilledBytes + 512U) / 1024U;
  char bufferText[16];
  if (bufferKb > 0) {
    snprintf(bufferText, sizeof(bufferText), "%luk",
             static_cast<unsigned long>(bufferKb));
  } else if (!audio.stateCode.isEmpty()) {
    strlcpy(bufferText, audio.stateCode.c_str(), sizeof(bufferText));
  } else {
    strlcpy(bufferText, "0k", sizeof(bufferText));
  }
  updateWifiHeader(wifiText);
  ipText_ = ipText.isEmpty() ? "IP: --.--.--.--" : "IP: " + ipText;
  setLabelTextIfChanged(ipLabel_, ipText_.c_str());
  if (playButtonLabel_)
    setLabelTextIfChanged(playButtonLabel_,
                          playbackStopped ? "Lejátszás" : "Szünet");

  String info;
  if (playbackStopped) {
    info = "Nincs lejátszás";
  } else {
    if (!audio.codec.isEmpty()) info += audio.codec;
    if (audio.bitrateKbps > 0) {
      if (!info.isEmpty()) info += " • ";
      info += String(audio.bitrateKbps) + " kbps";
    }
    if (audio.sampleRate > 0) {
      if (!info.isEmpty()) info += " • ";
      info += String(audio.sampleRate / 1000.0f, 1) + " kHz";
    }
    if (audio.bitsPerSample > 0) {
      if (!info.isEmpty()) info += " • ";
      info += String(audio.bitsPerSample) + " bit";
    }
    if (info.isEmpty()) {
      if (!audio.statusText.isEmpty())
        info = audio.statusText;
      else if (audio.running)
        info = "Lejátszás";
      else if (audio.connecting)
        info = "Kapcsolódás...";
      else
        info = "Nincs lejátszás";
    }
  }
  setLabelTextIfChanged(infoLabel_, info.c_str());

  time_t now = time(nullptr);
  struct tm localTime {};
  if (now > 100000 && localtime_r(&now, &localTime)) {
    char timeText[6];
    char secondsText[4];
    strftime(timeText, sizeof(timeText), "%H:%M", &localTime);
    strftime(secondsText, sizeof(secondsText), "%S", &localTime);
    setLabelTextIfChanged(clockLabel_, timeText);
    setLabelTextIfChanged(clockSecondsLabel_, secondsText);
    const String dateText = DateNameday::formatDatePart(localTime);
    const String namedayText = DateNameday::namedayText(localTime);
    if (dateText != lastDateText_ || namedayText != lastNamedayText_) {
      lastDateText_ = dateText;
      lastNamedayText_ = namedayText;
      setLabelTextIfChanged(dateLabel_, dateText.c_str());
      lv_obj_set_width(dateLabel_, LV_SIZE_CONTENT);
      lv_obj_update_layout(dateLabel_);
      setLabelTextIfChanged(namedayLabel_, namedayText.c_str());
      lv_obj_set_width(namedayLabel_, LV_SIZE_CONTENT);
      lv_obj_update_layout(namedayLabel_);

      constexpr lv_coord_t rowLeft = 14;
      constexpr lv_coord_t rowRight = 466;
      constexpr lv_coord_t rowY = 184;
      constexpr lv_coord_t gap = 12;
      const lv_coord_t rowWidth = rowRight - rowLeft;
      const lv_coord_t dateWidth = lv_obj_get_width(dateLabel_);
      const lv_coord_t namedayNaturalWidth = lv_obj_get_width(namedayLabel_);
      const lv_coord_t totalNaturalWidth =
          dateWidth + gap + namedayNaturalWidth;

      if (totalNaturalWidth <= rowWidth) {
        const lv_coord_t groupX = rowLeft + (rowWidth - totalNaturalWidth) / 2;
        lv_obj_set_pos(dateLabel_, groupX, rowY);
        lv_obj_set_pos(namedayLabel_, groupX + dateWidth + gap, rowY);
        lv_obj_set_width(namedayLabel_, namedayNaturalWidth);
      } else {
        lv_obj_set_pos(dateLabel_, rowLeft, rowY);
        const lv_coord_t namedayX = rowLeft + dateWidth + gap;
        lv_obj_set_pos(namedayLabel_, namedayX, rowY);
        lv_obj_set_width(namedayLabel_,
                         std::max<lv_coord_t>(20, rowRight - namedayX));
      }
    }
    if (kMoonPhaseEnabled) {
      updateMoonPhase(localTime);
    } else {
      if (moonImage_) lv_obj_add_flag(moonImage_, LV_OBJ_FLAG_HIDDEN);
      if (moonHost_) lv_obj_add_flag(moonHost_, LV_OBJ_FLAG_HIDDEN);
    }
  } else {
      setLabelTextIfChanged(clockLabel_, "--:--");
      setLabelTextIfChanged(clockSecondsLabel_, "--");
      setLabelTextIfChanged(dateLabel_, "--");
      setLabelTextIfChanged(namedayLabel_, "");
  }
  updateClockIpVisibility(millis());
  updateBufferBar(audio);
  updateWeather(weather);
  updateLogo(logoName);
  updateDiagnostics(diagnostics, bufferText);
}

void NowPlayingScreen::updateBufferBar(const AudioSnapshot& audio) {
  if (!bufferBarTrack_ || !bufferBarFill_) return;
  const bool visible =
      bufferBarVisible_ && (audio.running || audio.connecting ||
                            audio.bufferFilledBytes > 0);
  if (!visible) {
#if DISPLAY_PROFILE_AXS15231B
    setObjectHiddenIfChanged(bufferBarTrack_, true);
    setObjectHiddenIfChanged(bufferBarFill_, true);
#else
    lv_obj_add_flag(bufferBarTrack_, LV_OBJ_FLAG_HIDDEN);
#endif
    currentBufferPercent_ = 255;
    lastBufferBarUpdateAt_ = 0;
    return;
  }

#if DISPLAY_PROFILE_AXS15231B
  setObjectHiddenIfChanged(bufferBarTrack_, false);
#else
  lv_obj_remove_flag(bufferBarTrack_, LV_OBJ_FLAG_HIDDEN);
#endif
  const uint8_t percent = std::min<uint8_t>(audio.bufferPercent, 100);
  if (percent == currentBufferPercent_) return;
  const uint32_t now = millis();
  if (currentBufferPercent_ != 255 &&
      now - lastBufferBarUpdateAt_ < kBufferBarUpdateMs) {
    return;
  }
  currentBufferPercent_ = percent;
  lastBufferBarUpdateAt_ = now;

  if (percent == 0) {
#if DISPLAY_PROFILE_AXS15231B
    setObjectHiddenIfChanged(bufferBarFill_, true);
#else
    lv_obj_add_flag(bufferBarFill_, LV_OBJ_FLAG_HIDDEN);
#endif
    lv_obj_set_width(bufferBarFill_, 0);
    return;
  }

  const lv_coord_t width =
      std::max<lv_coord_t>(kBufferBarHeight,
                           (kBufferBarWidth * percent + 50) / 100);
  lv_obj_set_width(bufferBarFill_, width);
#if DISPLAY_PROFILE_AXS15231B
  setObjectHiddenIfChanged(bufferBarFill_, false);
#else
  lv_obj_remove_flag(bufferBarFill_, LV_OBJ_FLAG_HIDDEN);
#endif
}

void NowPlayingScreen::updateBackground(bool enabled, const String& path,
                                        uint8_t opacity) {
  if (!backgroundImage_) return;
  const bool backgroundChanged =
      backgroundEnabled_ != enabled || backgroundPath_ != path ||
      backgroundOpacity_ != opacity;
  backgroundEnabled_ = enabled;
  backgroundOpacity_ = opacity;

  if (!enabled) {
    if (backgroundChanged) {
      lv_obj_add_flag(backgroundImage_, LV_OBJ_FLAG_HIDDEN);
      vu_.setBackgroundSource(nullptr, 0, 255, false);
    }
    return;
  }

  if (backgroundPath_ != path) {
    backgroundPath_ = path;
    if (!loadBackgroundRgb565(path)) {
      lv_obj_add_flag(backgroundImage_, LV_OBJ_FLAG_HIDDEN);
      vu_.setBackgroundSource(nullptr, 0, 255, false);
      return;
    }
  }

  if (backgroundChanged) {
    vu_.setBackgroundSource(reinterpret_cast<const uint16_t*>(backgroundPixels_),
                            480, opacity, true);
    lv_obj_set_style_opa(backgroundImage_, opacity, 0);
    lv_obj_remove_flag(backgroundImage_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_background(backgroundImage_);
  }
}

void NowPlayingScreen::updateVu(const AudioLevels& levels) {
  vu_.update(levels);
}

void NowPlayingScreen::setNativeVisualizerBlit(
    void* context, WaveVu::NativeBlitCallback callback) {
  vu_.setNativeBlitCallback(context, callback);
}


void NowPlayingScreen::updateWifiIndicator(const String& wifiText) {
  const int rssi = extractWifiRssi(wifiText);
  uint8_t activeBars = 0;
  if (rssi >= -55) activeBars = 4;
  else if (rssi >= -67) activeBars = 3;
  else if (rssi >= -75) activeBars = 2;
  else if (rssi >= -85) activeBars = 1;

#if DISPLAY_PROFILE_AXS15231B
  if (activeBars == lastWifiSignalBars_) return;
#endif
  lastWifiSignalBars_ = activeBars;

  for (uint8_t index = 0; index < 4; ++index) {
    if (!wifiSignalBars_[index]) continue;
    lv_color_t color = lv_color_hex(0x334155);
    if (index < activeBars) {
      if (activeBars >= 4) color = lv_color_hex(0x22C55E);
      else if (activeBars == 3) color = lv_color_hex(0x84CC16);
      else if (activeBars == 2) color = lv_color_hex(0xFACC15);
      else color = lv_color_hex(0xF97316);
    }
    lv_obj_set_style_bg_color(wifiSignalBars_[index], color, 0);
  }
}

void NowPlayingScreen::updateWifiHeader(const String& wifiText) {
  lastWifiText_ = wifiText;
  int rssi = extractWifiRssi(wifiText);
  String detail = wifiText;
  if (rssi > -1000) detail += " dBm";
  setLabelTextIfChanged(wifiLabel_, detail.c_str());
  updateWifiIndicator(wifiText);

  const bool showText = wifiDetailsVisible_;
#if DISPLAY_PROFILE_AXS15231B
  setObjectHiddenIfChanged(wifiLabel_, !showText);
  for (auto* bar : wifiSignalBars_) {
    setObjectHiddenIfChanged(bar, showText);
  }
#else
  if (wifiLabel_) {
    if (showText)
      lv_obj_remove_flag(wifiLabel_, LV_OBJ_FLAG_HIDDEN);
    else
      lv_obj_add_flag(wifiLabel_, LV_OBJ_FLAG_HIDDEN);
  }
  for (auto* bar : wifiSignalBars_) {
    if (!bar) continue;
    if (showText)
      lv_obj_add_flag(bar, LV_OBJ_FLAG_HIDDEN);
    else
      lv_obj_remove_flag(bar, LV_OBJ_FLAG_HIDDEN);
  }
#endif
}

void NowPlayingScreen::showVolumePopup() {
  if (!volumePopup_) return;
  if (volumeSlider_) {
    sliderSyncing_ = true;
    lv_slider_set_value(volumeSlider_, currentVolume_, LV_ANIM_OFF);
    sliderSyncing_ = false;
  }
  char text[8];
  snprintf(text, sizeof(text), "%u/21", currentVolume_);
  setLabelTextIfChanged(volumePopupValueLabel_, text);
  lv_obj_remove_flag(volumePopup_, LV_OBJ_FLAG_HIDDEN);
  volumePopupVisibleUntil_ = millis() + kVolumePopupDurationMs;
  lv_obj_move_foreground(volumePopup_);
}

void NowPlayingScreen::updateHeaderVolumeDisplay() {
  const uint8_t activeBars = volumeToBars(currentVolume_);
  for (uint8_t index = 0; index < 5; ++index) {
    if (!volumeBars_[index]) continue;
    lv_obj_set_style_bg_color(
        volumeBars_[index],
        lv_color_hex(index < activeBars ? kHeaderVolumeActiveColor
                                        : kHeaderVolumeInactiveColor),
        0);
    if (volumeGraphicVisible_)
      lv_obj_remove_flag(volumeBars_[index], LV_OBJ_FLAG_HIDDEN);
    else
      lv_obj_add_flag(volumeBars_[index], LV_OBJ_FLAG_HIDDEN);
  }

  if (volumeLabel_) {
    if (volumeGraphicVisible_)
      lv_obj_add_flag(volumeLabel_, LV_OBJ_FLAG_HIDDEN);
    else
      lv_obj_remove_flag(volumeLabel_, LV_OBJ_FLAG_HIDDEN);
  }

  if (headerVolumeIcon_) {
    if (volumeGraphicVisible_)
      lv_obj_add_flag(headerVolumeIcon_, LV_OBJ_FLAG_HIDDEN);
    else
      lv_obj_remove_flag(headerVolumeIcon_, LV_OBJ_FLAG_HIDDEN);
  }
}

void NowPlayingScreen::updateWeather(const WeatherSnapshot& weather) {
  if (!weatherLabel_ || !weatherIconHost_) return;

  const bool show = weather.enabled;
  const bool showSummary = weather.valid && !weather.summary.isEmpty();
  const bool needsRestore =
      show &&
      (lv_obj_has_flag(weatherLabel_, LV_OBJ_FLAG_HIDDEN) ||
       lv_obj_has_flag(weatherIconHost_, LV_OBJ_FLAG_HIDDEN));
  const bool weatherChanged =
      show != lastWeatherVisible_ ||
      showSummary != !lastWeatherSummary_.isEmpty() ||
      weather.summary != lastWeatherSummary_ ||
      weather.icon != lastWeatherIcon_;
  lastWeatherVisible_ = show;
  lastWeatherSummary_ = showSummary ? weather.summary : String();
  lastWeatherIcon_ =
      showSummary ? weather.icon : WeatherIconKind::WifiError;

  if (!weatherChanged && !needsRestore) return;

  if (!show) {
    lv_obj_add_flag(weatherLabel_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(weatherIconHost_, LV_OBJ_FLAG_HIDDEN);
    if (weatherTouchArea_) lv_obj_add_flag(weatherTouchArea_, LV_OBJ_FLAG_HIDDEN);
    if (weatherIconImage_) lv_obj_add_flag(weatherIconImage_, LV_OBJ_FLAG_HIDDEN);
    return;
  }

  if (showSummary) {
    setLabelTextIfChanged(weatherLabel_, weather.summary.c_str());
    lv_obj_remove_flag(weatherLabel_, LV_OBJ_FLAG_HIDDEN);
  } else {
    setLabelTextIfChanged(weatherLabel_, "");
    lv_obj_add_flag(weatherLabel_, LV_OBJ_FLAG_HIDDEN);
  }
  lv_obj_remove_flag(weatherIconHost_, LV_OBJ_FLAG_HIDDEN);
  if (weatherTouchArea_) lv_obj_remove_flag(weatherTouchArea_, LV_OBJ_FLAG_HIDDEN);
  if (weatherTouchArea_) {
    lv_obj_set_pos(weatherTouchArea_, 11, 206);
    lv_obj_set_size(weatherTouchArea_, lv_obj_get_width(weatherIconHost_) + 6,
                    lv_obj_get_height(weatherIconHost_) + 6);
    lv_obj_move_foreground(weatherTouchArea_);
  }

  if (weatherIconImage_) lv_obj_add_flag(weatherIconImage_, LV_OBJ_FLAG_HIDDEN);

  if (weatherSunCenter_) {
    lv_obj_add_flag(weatherSunCenter_, LV_OBJ_FLAG_HIDDEN);
  }
  for (auto* ray : weatherSunRays_) {
    if (!ray) continue;
    lv_obj_add_flag(ray, LV_OBJ_FLAG_HIDDEN);
  }
  if (weatherCloudBase_) {
    lv_obj_add_flag(weatherCloudBase_, LV_OBJ_FLAG_HIDDEN);
  }
  for (auto* puff : weatherCloudPuffs_) {
    if (!puff) continue;
    lv_obj_add_flag(puff, LV_OBJ_FLAG_HIDDEN);
  }
  for (auto* drop : weatherRainDrops_) {
    if (!drop) continue;
    lv_obj_add_flag(drop, LV_OBJ_FLAG_HIDDEN);
  }

  String weatherIconPath;
  switch (weather.icon) {
    case WeatherIconKind::Sun:
      weatherIconPath = "/weather_icons_48/weather_sun.sr565";
      break;
    case WeatherIconKind::Rain:
      weatherIconPath = "/weather_icons_48/weather_rain.sr565";
      break;
    case WeatherIconKind::Storm:
      weatherIconPath = "/weather_icons_48/weather_storm.sr565";
      break;
    case WeatherIconKind::WifiError:
      weatherIconPath = "/weather_icons_48/wifi_error.sr565";
      break;
    case WeatherIconKind::Cloud:
    default:
      weatherIconPath = "/weather_icons_48/weather_cloud.sr565";
      break;
  }

  if (weatherIconImage_ && loadWeatherIconRgb565(weatherIconPath)) {
    lv_obj_remove_flag(weatherIconImage_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_invalidate(weatherIconHost_);
    return;
  }

  switch (weather.icon) {
    case WeatherIconKind::Sun:
      if (weatherSunCenter_) lv_obj_remove_flag(weatherSunCenter_, LV_OBJ_FLAG_HIDDEN);
      for (auto* ray : weatherSunRays_) {
        if (!ray) continue;
        lv_obj_remove_flag(ray, LV_OBJ_FLAG_HIDDEN);
      }
      break;

    case WeatherIconKind::Rain:
      if (weatherCloudBase_) lv_obj_remove_flag(weatherCloudBase_, LV_OBJ_FLAG_HIDDEN);
      for (auto* puff : weatherCloudPuffs_) {
        if (!puff) continue;
        lv_obj_remove_flag(puff, LV_OBJ_FLAG_HIDDEN);
      }
      for (auto* drop : weatherRainDrops_) {
        if (!drop) continue;
        lv_obj_remove_flag(drop, LV_OBJ_FLAG_HIDDEN);
      }
      break;

    case WeatherIconKind::Cloud:
    default:
      if (weatherCloudBase_) lv_obj_remove_flag(weatherCloudBase_, LV_OBJ_FLAG_HIDDEN);
      for (auto* puff : weatherCloudPuffs_) {
        if (!puff) continue;
        lv_obj_remove_flag(puff, LV_OBJ_FLAG_HIDDEN);
      }
      break;
  }

  lv_obj_invalidate(weatherIconHost_);
}

void NowPlayingScreen::updateClockIpVisibility(uint32_t now) {
  if (ipVisible_ && ipVisibleUntil_ != 0 &&
      static_cast<int32_t>(now - ipVisibleUntil_) >= 0) {
    ipVisible_ = false;
    ipVisibleUntil_ = 0;
  }

  setObjectHiddenIfChanged(ipLabel_, !ipVisible_);
  setObjectHiddenIfChanged(appLabel_, ipVisible_);
  setObjectHiddenIfChanged(clockLabel_, false);
  setObjectHiddenIfChanged(clockSecondsLabel_, false);
  setObjectHiddenIfChanged(clockSecondsUnderline_, false);
}

void NowPlayingScreen::updateMoonPhase(const tm& localTime) {
  if (!moonHost_ || !moonImage_) return;
  const int phase = moonPhaseIndex(localTime);
  if (phase == lastMoonPhase_ &&
      !lv_obj_has_flag(moonImage_, LV_OBJ_FLAG_HIDDEN)) {
    return;
  }

  if (loadMoonPhasePng(phase)) {
    lastMoonPhase_ = phase;
    lv_obj_remove_flag(moonImage_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(moonHost_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(moonHost_);
    lv_obj_invalidate(moonImage_);
    return;
  }

  lv_obj_add_flag(moonImage_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(moonHost_, LV_OBJ_FLAG_HIDDEN);
  lastMoonPhase_ = -1;
}

void NowPlayingScreen::setVolumeIndicator(uint8_t value) {
  currentVolume_ = value;
  char text[8];
  snprintf(text, sizeof(text), "%u/21", value);
  updateHeaderVolumeLabel(volumeLabel_, text);
  setLabelTextIfChanged(volumePopupValueLabel_, text);
  if (volumeSlider_) {
    sliderSyncing_ = true;
    lv_slider_set_value(volumeSlider_, value, LV_ANIM_OFF);
    sliderSyncing_ = false;
  }
  if (volumePopup_) {
    showVolumePopup();
  }
  updateHeaderVolumeDisplay();
}

void NowPlayingScreen::setVisualizerMode(WaveVu::Mode mode) {
  vu_.setMode(mode);
}

WaveVu::Mode NowPlayingScreen::visualizerMode() const { return vu_.mode(); }

void NowPlayingScreen::setHeaderIpVisible(bool visible) {
  ipVisible_ = visible;
  ipVisibleUntil_ = 0;
  if (ipVisible_) setLabelTextIfChanged(ipLabel_, ipText_.c_str());
  updateClockIpVisibility(millis());
}

void NowPlayingScreen::showStartupIp(uint32_t durationMs) {
  ipVisible_ = true;
  ipVisibleUntil_ = millis() + durationMs;
  setLabelTextIfChanged(ipLabel_, ipText_.c_str());
  updateClockIpVisibility(millis());
}

bool NowPlayingScreen::headerIpVisible() const { return ipVisible_; }

void NowPlayingScreen::setWifiDetailsVisible(bool visible) {
  wifiDetailsVisible_ = visible;
  updateWifiHeader(lastWifiText_);
}

bool NowPlayingScreen::wifiDetailsVisible() const {
  return wifiDetailsVisible_;
}

void NowPlayingScreen::setVolumeGraphicVisible(bool visible) {
  volumeGraphicVisible_ = visible;
  updateHeaderVolumeDisplay();
}

bool NowPlayingScreen::volumeGraphicVisible() const {
  return volumeGraphicVisible_;
}

void NowPlayingScreen::setBufferBarVisible(bool visible) {
  bufferBarVisible_ = visible;
  if (!visible && bufferBarTrack_) {
    lv_obj_add_flag(bufferBarTrack_, LV_OBJ_FLAG_HIDDEN);
    currentBufferPercent_ = 255;
    lastBufferBarUpdateAt_ = 0;
  }
}

bool NowPlayingScreen::bufferBarVisible() const {
  return bufferBarVisible_;
}

void NowPlayingScreen::setDiagnosticsVisible(bool visible) {
  showDiagnostics(visible);
}

bool NowPlayingScreen::diagnosticsVisible() const {
  return diagnosticsVisible_;
}

bool NowPlayingScreen::visualizerActive() const { return vu_.active(); }

void NowPlayingScreen::loop(uint32_t now) {
  updateClockIpVisibility(now);
  if (volumePopup_ && !(lv_obj_has_flag(volumePopup_, LV_OBJ_FLAG_HIDDEN)) &&
      volumePopupVisibleUntil_ != 0 &&
      static_cast<int32_t>(now - volumePopupVisibleUntil_) >= 0) {
    lv_obj_add_flag(volumePopup_, LV_OBJ_FLAG_HIDDEN);
    volumePopupVisibleUntil_ = 0;
  }
}

void NowPlayingScreen::updateLogo(const String& requestedName) {
  String source = requestedName;
  source.trim();
  if (source.isEmpty()) source = "/logos/nologo.png";

  String lower = source;
  lower.toLowerCase();
  if (lower.endsWith(".sr565")) {
    if (loadRgb565Thumbnail(source)) {
      currentLogo_ = source;
      return;
    }
    // Never pass an invalid/custom cache file to LVGL as if it were a normal
    // image file. The fallback remains a valid, drawable local PNG.
    source = "/logos/nologo.png";
  }

  String filePath = source;
  if (!filePath.startsWith("/")) {
    filePath = "/logos/" + filePath;
    const int slash = filePath.lastIndexOf('/');
    if (filePath.lastIndexOf('.') <= slash) filePath += ".png";
  }
  if (!LittleFS.exists(filePath)) filePath = "/logos/nologo.png";

  // Compare the resolved file, not only the requested station value. Several
  // stations can legitimately resolve to the same local fallback image.
  const String resolvedPath = "S:" + filePath;
  if (source == currentLogo_ && resolvedPath == logoPath_) {
#if !DISPLAY_PROFILE_AXS15231B
    if (logoTouchArea_) lv_obj_move_foreground(logoTouchArea_);
#endif
    return;
  }

  currentLogo_ = source;
  logoPath_ = resolvedPath;
  lv_image_set_scale(logoImage_, 384);
  lv_image_set_src(logoImage_, logoPath_.c_str());
  if (logoTouchArea_) lv_obj_move_foreground(logoTouchArea_);
  if (diagnosticsVisible_)
    lv_obj_add_flag(logoImage_, LV_OBJ_FLAG_HIDDEN);
  else
    lv_obj_remove_flag(logoImage_, LV_OBJ_FLAG_HIDDEN);
  if (logoPixels_) {
    free(logoPixels_);
    logoPixels_ = nullptr;
    logoDescriptor_ = {};
  }
}

bool NowPlayingScreen::loadRgb565Thumbnail(const String& path) {
  File file = LittleFS.open(path, FILE_READ);
  if (!file || file.size() != 8 + 128 * 128 * 2) {
    if (file) file.close();
    return false;
  }
  uint8_t header[8]{};
  const bool valid =
      file.read(header, sizeof(header)) == sizeof(header) &&
      header[0] == 'S' && header[1] == 'R' && header[2] == '5' &&
      header[3] == '7' && header[4] == 128 && header[5] == 0 &&
      header[6] == 128 && header[7] == 0;
  if (!valid) {
    file.close();
    return false;
  }

  constexpr size_t pixelBytes = 128 * 128 * 2;
  uint8_t* pixels = static_cast<uint8_t*>(
      heap_caps_malloc(pixelBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!pixels) pixels = static_cast<uint8_t*>(malloc(pixelBytes));
  if (!pixels || file.read(pixels, pixelBytes) != pixelBytes) {
    if (pixels) free(pixels);
    file.close();
    return false;
  }
  file.close();

  if (logoPixels_) free(logoPixels_);
  logoPixels_ = pixels;
  logoDescriptor_ = {};
  logoDescriptor_.header.magic = LV_IMAGE_HEADER_MAGIC;
  // A .sr565 fájl a LovyanGFX sprite nyers pufferét tartalmazza, pontosan
  // úgy, mint a SimpleRadio. Ez bájtcserélt RGB565, ezért az LVGL-nek is
  // ennek megfelelő forrásformátumként kell átadni.
  logoDescriptor_.header.cf = LV_COLOR_FORMAT_RGB565_SWAPPED;
  logoDescriptor_.header.flags = 0;
  logoDescriptor_.header.w = 128;
  logoDescriptor_.header.h = 128;
  logoDescriptor_.header.stride = 256;
  logoDescriptor_.data_size = pixelBytes;
  logoDescriptor_.data = logoPixels_;
  lv_image_set_scale(logoImage_, 256);
  lv_image_set_src(logoImage_, &logoDescriptor_);
  if (logoTouchArea_) lv_obj_move_foreground(logoTouchArea_);
  if (diagnosticsVisible_)
    lv_obj_add_flag(logoImage_, LV_OBJ_FLAG_HIDDEN);
  else
    lv_obj_remove_flag(logoImage_, LV_OBJ_FLAG_HIDDEN);
  return true;
}

bool NowPlayingScreen::loadWeatherIconRgb565(const String& path) {
  File file = LittleFS.open(path, FILE_READ);
  if (!file || file.size() != 8 + 48 * 48 * 2) {
    if (file) file.close();
    return false;
  }

  uint8_t header[8]{};
  const bool valid =
      file.read(header, sizeof(header)) == sizeof(header) &&
      header[0] == 'S' && header[1] == 'R' && header[2] == '5' &&
      header[3] == '7' && header[4] == 48 && header[5] == 0 &&
      header[6] == 48 && header[7] == 0;
  if (!valid) {
    file.close();
    return false;
  }

  constexpr size_t pixelCount = 48 * 48;
  constexpr size_t colorBytes = pixelCount * 2;
  constexpr size_t alphaBytes = pixelCount;
  constexpr size_t totalBytes = colorBytes + alphaBytes;

  uint8_t* rawPixels = static_cast<uint8_t*>(
      heap_caps_malloc(colorBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!rawPixels) rawPixels = static_cast<uint8_t*>(malloc(colorBytes));
  if (!rawPixels || file.read(rawPixels, colorBytes) != colorBytes) {
    if (rawPixels) free(rawPixels);
    file.close();
    return false;
  }
  file.close();

  uint8_t* pixels = static_cast<uint8_t*>(
      heap_caps_malloc(totalBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!pixels) pixels = static_cast<uint8_t*>(malloc(totalBytes));
  if (!pixels) {
    free(rawPixels);
    return false;
  }

  uint8_t* colorPlane = pixels;
  uint8_t* alphaPlane = pixels + colorBytes;
  for (size_t i = 0; i < pixelCount; ++i) {
    const uint8_t lo = rawPixels[i * 2];
    const uint8_t hi = rawPixels[i * 2 + 1];
    colorPlane[i * 2] = hi;
    colorPlane[i * 2 + 1] = lo;

    const uint16_t color565 =
        (static_cast<uint16_t>(hi) << 8) | static_cast<uint16_t>(lo);
    const uint8_t r = static_cast<uint8_t>((color565 >> 11) & 0x1F);
    const uint8_t g = static_cast<uint8_t>((color565 >> 5) & 0x3F);
    const uint8_t b = static_cast<uint8_t>(color565 & 0x1F);
    const bool nearBlack = (r <= 1) && (g <= 2) && (b <= 1);
    alphaPlane[i] = nearBlack ? 0 : 255;
  }
  free(rawPixels);

  if (weatherIconPixels_) free(weatherIconPixels_);
  weatherIconPixels_ = pixels;
  weatherIconDescriptor_ = {};
  weatherIconDescriptor_.header.magic = LV_IMAGE_HEADER_MAGIC;
  weatherIconDescriptor_.header.cf = LV_COLOR_FORMAT_RGB565A8;
  weatherIconDescriptor_.header.flags = 0;
  weatherIconDescriptor_.header.w = 48;
  weatherIconDescriptor_.header.h = 48;
  weatherIconDescriptor_.header.stride = 96;
  weatherIconDescriptor_.data_size = totalBytes;
  weatherIconDescriptor_.data = weatherIconPixels_;
  lv_image_set_src(weatherIconImage_, &weatherIconDescriptor_);
  return true;
}

bool NowPlayingScreen::loadMoonPhasePng(int phase) {
  if (!moonHost_ || !moonImage_ || phase < 0 || phase >= kMoonPhaseCount) {
    return false;
  }
  String source = "/moon_phases/moon_phase_" + String(phase) + ".png";
  source.trim();
  if (source.isEmpty()) return false;

  String filePath = source;
  if (!filePath.startsWith("/")) {
    filePath = "/moon_phases/" + filePath;
    const int slash = filePath.lastIndexOf('/');
    if (filePath.lastIndexOf('.') <= slash) filePath += ".png";
  }
  if (!LittleFS.exists(filePath)) return false;

  const String resolvedPath = "S:" + filePath;
  if (source == currentMoon_ && resolvedPath == moonPath_) {
    lv_obj_center(moonImage_);
    lv_obj_remove_flag(moonImage_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(moonHost_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(moonHost_);
    return true;
  }

  File file = LittleFS.open(filePath, FILE_READ);
  bool alphaLoaded = false;
  uint8_t* pixels = nullptr;
  size_t dataSize = 0;
  lv_color_format_t colorFormat = LV_COLOR_FORMAT_UNKNOWN;

  if (file) {
  const size_t pngBytes = file.size();
  uint8_t* png = static_cast<uint8_t*>(malloc(pngBytes));
  if (!png) {
    file.close();
    } else {
      const bool readOk = file.read(png, pngBytes) == pngBytes;
      file.close();
      if (readOk) {
        unsigned char* rgba = nullptr;
        unsigned decodedWidth = 0;
        unsigned decodedHeight = 0;
        const unsigned error =
            lodepng_decode32(&rgba, &decodedWidth, &decodedHeight, png, pngBytes);
        if (!error && rgba && decodedWidth == kMoonImageWidth &&
            decodedHeight == kMoonImageHeight) {
          constexpr size_t pixelCount = kMoonImageWidth * kMoonImageHeight;
          constexpr size_t colorBytes = pixelCount * 2;
          constexpr size_t alphaBytes = pixelCount;
          constexpr size_t totalBytes = colorBytes + alphaBytes;
          pixels = static_cast<uint8_t*>(
              heap_caps_malloc(totalBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
          if (!pixels) pixels = static_cast<uint8_t*>(malloc(totalBytes));
          if (pixels) {
            uint8_t* colorPlane = pixels;
            uint8_t* alphaPlane = pixels + colorBytes;
            for (size_t i = 0; i < pixelCount; ++i) {
              const uint8_t r = rgba[i * 4 + 0];
              const uint8_t g = rgba[i * 4 + 1];
              const uint8_t b = rgba[i * 4 + 2];
              const uint8_t a = rgba[i * 4 + 3];
              const uint16_t color565 =
                  (static_cast<uint16_t>(r & 0xF8) << 8) |
                  (static_cast<uint16_t>(g & 0xFC) << 3) |
                  static_cast<uint16_t>(b >> 3);
              colorPlane[i * 2] = static_cast<uint8_t>(color565 >> 8);
              colorPlane[i * 2 + 1] = static_cast<uint8_t>(color565 & 0xFF);
              const int x = static_cast<int>(i % kMoonImageWidth);
              const int y = static_cast<int>(i / kMoonImageWidth);
              alphaPlane[i] = moonAlphaFromPixel(r, g, b, a, x, y);
            }
            dataSize = totalBytes;
            colorFormat = LV_COLOR_FORMAT_RGB565A8;
            alphaLoaded = true;
          }
        }
        if (rgba) lv_free(rgba);
      }
      free(png);
    }
  }

  if (!alphaLoaded) {
    lgfx::LGFX_Sprite sprite;
    sprite.setPsram(true);
    sprite.setColorDepth(16);
    if (!sprite.createSprite(kMoonImageWidth, kMoonImageHeight)) return false;
    sprite.fillSprite(0x0000);
    const bool drawn = sprite.drawPngFile(
        LittleFS, filePath.c_str(), 0, 0, kMoonImageWidth, kMoonImageHeight,
        0, 0, 1.0f, 1.0f, top_left);
    sprite.releasePngMemory();
    if (!drawn) {
      sprite.deleteSprite();
      return false;
    }
    constexpr size_t pixelCount = kMoonImageWidth * kMoonImageHeight;
    constexpr size_t colorBytes = pixelCount * 2;
    constexpr size_t alphaBytes = pixelCount;
    constexpr size_t fallbackBytes = colorBytes + alphaBytes;
    pixels = static_cast<uint8_t*>(
        heap_caps_malloc(fallbackBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!pixels) pixels = static_cast<uint8_t*>(malloc(fallbackBytes));
    if (!pixels) {
      sprite.deleteSprite();
      return false;
    }
    const uint8_t* spritePixels = static_cast<const uint8_t*>(sprite.getBuffer());
    uint8_t* colorPlane = pixels;
    uint8_t* alphaPlane = pixels + colorBytes;
    for (size_t i = 0; i < pixelCount; ++i) {
      const uint8_t lo = spritePixels[i * 2];
      const uint8_t hi = spritePixels[i * 2 + 1];
      colorPlane[i * 2] = hi;
      colorPlane[i * 2 + 1] = lo;
      const uint16_t color565 =
          (static_cast<uint16_t>(hi) << 8) | static_cast<uint16_t>(lo);
      const uint8_t r = static_cast<uint8_t>(((color565 >> 11) & 0x1F) << 3);
      const uint8_t g = static_cast<uint8_t>(((color565 >> 5) & 0x3F) << 2);
      const uint8_t b = static_cast<uint8_t>((color565 & 0x1F) << 3);
      const int x = static_cast<int>(i % kMoonImageWidth);
      const int y = static_cast<int>(i / kMoonImageWidth);
      alphaPlane[i] = moonAlphaFromPixel(r, g, b, 255, x, y);
    }
    sprite.deleteSprite();
    dataSize = fallbackBytes;
    colorFormat = LV_COLOR_FORMAT_RGB565A8;
  }

  if (moonPixels_) free(moonPixels_);
  moonPixels_ = pixels;
  moonDescriptor_ = {};
  moonDescriptor_.header.magic = LV_IMAGE_HEADER_MAGIC;
  moonDescriptor_.header.cf = colorFormat;
  moonDescriptor_.header.flags = 0;
  moonDescriptor_.header.w = kMoonImageWidth;
  moonDescriptor_.header.h = kMoonImageHeight;
  moonDescriptor_.header.stride = kMoonImageWidth * 2;
  moonDescriptor_.data_size = dataSize;
  moonDescriptor_.data = moonPixels_;
  currentMoon_ = source;
  moonPath_ = resolvedPath;

  lv_image_set_scale(moonImage_, 256);
  lv_image_set_src(moonImage_, &moonDescriptor_);
  lv_obj_center(moonImage_);
  lv_obj_remove_flag(moonImage_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_flag(moonHost_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(moonHost_);
  return true;
}

bool NowPlayingScreen::loadBackgroundRgb565(const String& path) {
  File file = LittleFS.open(path, FILE_READ);
  if (!file || file.size() != 8 + 480 * 320 * 2) {
    if (file) file.close();
    return false;
  }

  uint8_t header[8]{};
  const bool valid =
      file.read(header, sizeof(header)) == sizeof(header) &&
      header[0] == 'S' && header[1] == 'R' && header[2] == '5' &&
      header[3] == '7' && header[4] == 224 && header[5] == 1 &&
      header[6] == 64 && header[7] == 1;
  if (!valid) {
    file.close();
    return false;
  }

  constexpr size_t pixelBytes = 480 * 320 * 2;
  uint8_t* pixels = static_cast<uint8_t*>(
      heap_caps_malloc(pixelBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!pixels) pixels = static_cast<uint8_t*>(malloc(pixelBytes));
  if (!pixels || file.read(pixels, pixelBytes) != pixelBytes) {
    if (pixels) free(pixels);
    file.close();
    return false;
  }
  file.close();

  if (backgroundPixels_) free(backgroundPixels_);
  backgroundPixels_ = pixels;
  backgroundDescriptor_ = {};
  backgroundDescriptor_.header.magic = LV_IMAGE_HEADER_MAGIC;
  backgroundDescriptor_.header.cf = LV_COLOR_FORMAT_RGB565_SWAPPED;
  backgroundDescriptor_.header.flags = 0;
  backgroundDescriptor_.header.w = 480;
  backgroundDescriptor_.header.h = 320;
  backgroundDescriptor_.header.stride = 960;
  backgroundDescriptor_.data_size = pixelBytes;
  backgroundDescriptor_.data = backgroundPixels_;
  lv_image_set_src(backgroundImage_, &backgroundDescriptor_);
  return true;
}

void NowPlayingScreen::updateDiagnostics(const DiagnosticsSnapshot& diagnostics,
                                         const char* bufferText) {
  if (!diagnosticsLabel_ || !diagnosticsVisible_) return;
  char text[144];
  const unsigned ramKb = diagnostics.freeInternalHeap / 1024U;
  const unsigned psramKb = diagnostics.freePsram / 1024U;
  char cpu0[8] = " --%";
  char cpu1[8] = " --%";
  char temperature[10] = "  --.-C";
  if (diagnostics.cpuValid) {
    snprintf(cpu0, sizeof(cpu0), "%3u%%", diagnostics.cpu0Percent);
    snprintf(cpu1, sizeof(cpu1), "%3u%%", diagnostics.cpu1Percent);
  }
  if (diagnostics.temperatureValid) {
    snprintf(temperature, sizeof(temperature), "%5.1fC",
             diagnostics.temperatureC);
  }
  snprintf(text, sizeof(text),
           "DIAG %uMHz\nBUFFER %s\nCPU0 %s\nCPU1 %s\nTEMP%s\nRAM %5uk\nPSRAM%5uk",
           diagnostics.cpuFrequencyMhz,
           (bufferText && bufferText[0]) ? bufferText : "--", cpu0, cpu1,
           temperature, ramKb, psramKb);
  setLabelTextIfChanged(diagnosticsLabel_, text);
}

void NowPlayingScreen::showDiagnostics(bool show) {
  diagnosticsVisible_ = show;
  if (diagnosticsLabel_) {
    if (show)
      lv_obj_remove_flag(diagnosticsLabel_, LV_OBJ_FLAG_HIDDEN);
    else
      lv_obj_add_flag(diagnosticsLabel_, LV_OBJ_FLAG_HIDDEN);
  }
  if (logoImage_) {
    if (show)
      lv_obj_add_flag(logoImage_, LV_OBJ_FLAG_HIDDEN);
    else
      lv_obj_remove_flag(logoImage_, LV_OBJ_FLAG_HIDDEN);
  }
  if (logoTouchArea_) lv_obj_move_foreground(logoTouchArea_);
}

void NowPlayingScreen::previousEvent(lv_event_t* event) {
  auto* screen =
      static_cast<NowPlayingScreen*>(lv_event_get_user_data(event));
  if (screen && screen->actions_.previous)
    screen->actions_.previous(screen->actions_.context);
}

void NowPlayingScreen::toggleEvent(lv_event_t* event) {
  auto* screen =
      static_cast<NowPlayingScreen*>(lv_event_get_user_data(event));
  if (screen && screen->actions_.toggle)
    screen->actions_.toggle(screen->actions_.context);
}

void NowPlayingScreen::nextEvent(lv_event_t* event) {
  auto* screen =
      static_cast<NowPlayingScreen*>(lv_event_get_user_data(event));
  if (screen && screen->actions_.next)
    screen->actions_.next(screen->actions_.context);
}

void NowPlayingScreen::logoPanelEvent(lv_event_t* event) {
  auto* screen =
      static_cast<NowPlayingScreen*>(lv_event_get_user_data(event));
  if (!screen) return;
  screen->showDiagnostics(!screen->diagnosticsVisible_);
  if (screen->actions_.diagnosticsVisibleChanged) {
    screen->actions_.diagnosticsVisibleChanged(screen->actions_.context,
                                               screen->diagnosticsVisible_);
  }
}

void NowPlayingScreen::wifiEvent(lv_event_t* event) {
  auto* screen =
      static_cast<NowPlayingScreen*>(lv_event_get_user_data(event));
  if (!screen) return;
  screen->wifiDetailsVisible_ = !screen->wifiDetailsVisible_;
  screen->updateWifiHeader(screen->lastWifiText_);
  if (screen->actions_.wifiDetailsVisibleChanged) {
    screen->actions_.wifiDetailsVisibleChanged(screen->actions_.context,
                                               screen->wifiDetailsVisible_);
  }
}

void NowPlayingScreen::stationEvent(lv_event_t* event) {
  auto* screen =
      static_cast<NowPlayingScreen*>(lv_event_get_user_data(event));
  if (screen && screen->actions_.openPresets)
    screen->actions_.openPresets(screen->actions_.context);
}

void NowPlayingScreen::screenTapEvent(lv_event_t* event) {
  auto* screen =
      static_cast<NowPlayingScreen*>(lv_event_get_user_data(event));
  lv_indev_t* input = lv_indev_active();
  if (!screen || !input ||
      lv_event_get_target(event) != lv_screen_active())
    return;
  lv_point_t point;
  lv_indev_get_point(input, &point);
  if (lv_event_get_code(event) == LV_EVENT_PRESSED) {
    screen->screenPressedAt_ = point;
    return;
  }
  if (abs(point.x - screen->screenPressedAt_.x) > 12 ||
      abs(point.y - screen->screenPressedAt_.y) > 12)
    return;

  if (pointInsideObject(screen->weatherTouchArea_, point) ||
      pointInsideObject(screen->weatherIconHost_, point)) {
    return;
  }

  if (screen->actions_.toggle)
    screen->actions_.toggle(screen->actions_.context);
}

void NowPlayingScreen::headerVolumeEvent(lv_event_t* event) {
  auto* screen =
      static_cast<NowPlayingScreen*>(lv_event_get_user_data(event));
  if (!screen) return;
  screen->volumeGraphicVisible_ = !screen->volumeGraphicVisible_;
  screen->updateHeaderVolumeDisplay();
  screen->showVolumePopup();
  if (screen->actions_.volumeGraphicVisibleChanged) {
    screen->actions_.volumeGraphicVisibleChanged(screen->actions_.context,
                                                 screen->volumeGraphicVisible_);
  }
}

void NowPlayingScreen::clockAreaEvent(lv_event_t* event) {
  auto* screen =
      static_cast<NowPlayingScreen*>(lv_event_get_user_data(event));
  if (!screen) return;
  if (screen->ipVisible_) {
    screen->ipVisible_ = false;
    screen->ipVisibleUntil_ = 0;
  } else {
    screen->ipVisible_ = true;
    screen->ipVisibleUntil_ = millis() + kIpVisibleDurationMs;
    setLabelTextIfChanged(screen->ipLabel_, screen->ipText_.c_str());
  }
  screen->updateClockIpVisibility(millis());
  if (screen->actions_.headerIpVisibleChanged) {
    screen->actions_.headerIpVisibleChanged(screen->actions_.context,
                                            screen->ipVisible_);
  }
}

void NowPlayingScreen::weatherEvent(lv_event_t* event) {
  auto* screen =
      static_cast<NowPlayingScreen*>(lv_event_get_user_data(event));
  if (!screen) return;

  lv_event_code_t code = lv_event_get_code(event);
  if (code != LV_EVENT_PRESSED && code != LV_EVENT_CLICKED &&
      code != LV_EVENT_RELEASED && code != LV_EVENT_SHORT_CLICKED) {
    return;
  }

  lv_event_stop_bubbling(event);

  static uint32_t lastWeatherTapMs = 0;
  const uint32_t now = millis();
  if (now - lastWeatherTapMs < 250) return;
  lastWeatherTapMs = now;

  if (screen->actions_.cycleWeatherMode)
    screen->actions_.cycleWeatherMode(screen->actions_.context);
}

void NowPlayingScreen::volumeSliderEvent(lv_event_t* event) {
  auto* screen =
      static_cast<NowPlayingScreen*>(lv_event_get_user_data(event));
  if (!screen || !screen->volumeSlider_) return;

  if (lv_event_get_code(event) == LV_EVENT_VALUE_CHANGED) {
    const int value = lv_slider_get_value(screen->volumeSlider_);
    char text[8];
    snprintf(text, sizeof(text), "%d/21", value);
    setLabelTextIfChanged(screen->volumePopupValueLabel_, text);
    updateHeaderVolumeLabel(screen->volumeLabel_, text);
    if (!screen->sliderSyncing_ && screen->actions_.setVolume) {
      screen->actions_.setVolume(screen->actions_.context,
                                 static_cast<uint8_t>(value));
    }
    screen->showVolumePopup();
  } else if (lv_event_get_code(event) == LV_EVENT_RELEASED) {
    screen->volumePopupVisibleUntil_ = millis() + kVolumePopupDurationMs;
  }
}
