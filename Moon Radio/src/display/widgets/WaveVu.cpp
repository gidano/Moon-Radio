#include "WaveVu.h"

#include <algorithm>
#include <cstdlib>

#include <esp_heap_caps.h>

#include "options.h"

LV_FONT_DECLARE(lv_font_montserrat_14);

namespace {

#if DISPLAY_PROFILE_ST7796 || DISPLAY_PROFILE_AXS15231B || !TOUCH_ENABLED
constexpr bool kUsePanelSafeBackground = true;
constexpr uint32_t kScreenBackgroundRgb = 0x080B12;
#else
constexpr bool kUsePanelSafeBackground = false;
constexpr uint32_t kScreenBackgroundRgb = 0x000000;
#endif

uint16_t rgb565Swapped(uint32_t rgb) {
  const uint16_t native =
      static_cast<uint16_t>(((rgb >> 19) & 0x1F) << 11 |
                            ((rgb >> 10) & 0x3F) << 5 |
                            ((rgb >> 3) & 0x1F));
  return static_cast<uint16_t>((native >> 8) | (native << 8));
}

uint16_t blend565Swapped(uint16_t source, uint8_t opacity,
                         uint32_t baseRgb = kScreenBackgroundRgb) {
  if (opacity >= 255) return source;
  // Background .sr565 files are stored in RGB565_SWAPPED order.
  const uint16_t native =
      static_cast<uint16_t>((source >> 8) | (source << 8));
  const uint8_t r = (native >> 11) & 0x1F;
  const uint8_t g = (native >> 5) & 0x3F;
  const uint8_t b = native & 0x1F;
  const uint8_t baseR = (baseRgb >> 19) & 0x1F;
  const uint8_t baseG = (baseRgb >> 10) & 0x3F;
  const uint8_t baseB = (baseRgb >> 3) & 0x1F;
  const uint8_t blendedR =
      static_cast<uint8_t>((static_cast<uint16_t>(r) * opacity +
                            static_cast<uint16_t>(baseR) * (255 - opacity)) /
                           255);
  const uint8_t blendedG =
      static_cast<uint8_t>((static_cast<uint16_t>(g) * opacity +
                            static_cast<uint16_t>(baseG) * (255 - opacity)) /
                           255);
  const uint8_t blendedB =
      static_cast<uint8_t>((static_cast<uint16_t>(b) * opacity +
                            static_cast<uint16_t>(baseB) * (255 - opacity)) /
                           255);
  const uint16_t blendedNative =
      static_cast<uint16_t>((blendedR << 11) | (blendedG << 5) | blendedB);
  return static_cast<uint16_t>((blendedNative >> 8) | (blendedNative << 8));
}

uint16_t blend565SwappedOver(uint16_t background, uint16_t foreground,
                             uint8_t opacity) {
  if (opacity == 255) return foreground;
  const uint16_t back = static_cast<uint16_t>((background >> 8) |
                                               (background << 8));
  const uint16_t front = static_cast<uint16_t>((foreground >> 8) |
                                                (foreground << 8));
  const uint8_t r = static_cast<uint8_t>((((front >> 11) & 0x1F) * opacity +
                                          ((back >> 11) & 0x1F) * (255 - opacity)) /
                                         255);
  const uint8_t g = static_cast<uint8_t>((((front >> 5) & 0x3F) * opacity +
                                          ((back >> 5) & 0x3F) * (255 - opacity)) /
                                         255);
  const uint8_t b = static_cast<uint8_t>(((front & 0x1F) * opacity +
                                          (back & 0x1F) * (255 - opacity)) /
                                         255);
  const uint16_t mixed = static_cast<uint16_t>((r << 11) | (g << 5) | b);
  return static_cast<uint16_t>((mixed >> 8) | (mixed << 8));
}

}  // namespace

void WaveVu::create(lv_obj_t* parent) {
  root_ = lv_obj_create(parent);
  lv_obj_set_size(root_, kWidth, kHeight);
  lv_obj_set_pos(root_, kLeft, kTop);
  lv_obj_set_style_pad_all(root_, 0, 0);
  lv_obj_set_style_border_width(root_, 0, 0);
  lv_obj_set_style_radius(root_, 0, 0);
  lv_obj_set_style_bg_opa(
      root_, kUsePanelSafeBackground ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(root_, lv_color_hex(0x000000), 0);
  lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(root_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(root_, touchEvent, LV_EVENT_PRESSED, this);
  lv_obj_add_event_cb(root_, touchEvent, LV_EVENT_RELEASED, this);

  const size_t pixelBytes =
      static_cast<size_t>(kWidth) * kHeight * sizeof(uint16_t);
  pixels_ = static_cast<uint16_t*>(
      heap_caps_malloc(pixelBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!pixels_) {
    pixels_ = static_cast<uint16_t*>(malloc(pixelBytes));
  }
  backgroundPixels_ = static_cast<uint16_t*>(
      heap_caps_malloc(pixelBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!backgroundPixels_) {
    backgroundPixels_ = static_cast<uint16_t*>(malloc(pixelBytes));
  }
  if (!pixels_ || !backgroundPixels_) return;
  std::fill_n(backgroundPixels_, static_cast<size_t>(kWidth) * kHeight,
              rgb565Swapped(kScreenBackgroundRgb));
  cacheFontGlyph('L', leftGlyph_);
  cacheFontGlyph('R', rightGlyph_);

  canvas_ = lv_canvas_create(root_);
  lv_canvas_set_buffer(canvas_, pixels_, kWidth, kHeight,
                       LV_COLOR_FORMAT_RGB565_SWAPPED);
  lv_obj_set_pos(canvas_, 0, 0);
  lv_obj_set_style_bg_opa(
      canvas_, kUsePanelSafeBackground ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(canvas_, lv_color_hex(0x000000), 0);
  lv_obj_clear_flag(canvas_, LV_OBJ_FLAG_CLICKABLE);
#if DISPLAY_PROFILE_AXS15231B
  // The AXS VU is never an LVGL-visible canvas.  create() renders an initial
  // idle frame before DisplayManager installs the native callback; leaving
  // this canvas visible made that stale, softened base frame survive under
  // the direct 240x62 VU stream.  Hide it before its very first render.
  lv_obj_add_flag(canvas_, LV_OBJ_FLAG_HIDDEN);
#endif

#if !DISPLAY_PROFILE_AXS15231B
  leftLabel_ = lv_label_create(root_);
  lv_label_set_text(leftLabel_, "L");
  lv_obj_set_pos(leftLabel_, 1, 8);
  lv_obj_set_style_text_font(leftLabel_, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(leftLabel_, lv_color_hex(0x94A3B8), 0);
  lv_obj_set_style_bg_opa(leftLabel_, LV_OPA_TRANSP, 0);
  lv_obj_clear_flag(leftLabel_, LV_OBJ_FLAG_CLICKABLE);

  rightLabel_ = lv_label_create(root_);
  lv_label_set_text(rightLabel_, "R");
  lv_obj_set_pos(rightLabel_, 1, 35);
  lv_obj_set_style_text_font(rightLabel_, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(rightLabel_, lv_color_hex(0x94A3B8), 0);
  lv_obj_set_style_bg_opa(rightLabel_, LV_OPA_TRANSP, 0);
  lv_obj_clear_flag(rightLabel_, LV_OBJ_FLAG_CLICKABLE);
#endif

  render();
}

void WaveVu::setBackgroundSource(const uint16_t* fullPixels, int32_t fullWidth,
                                 uint8_t opacity, bool enabled) {
  if (!backgroundPixels_) return;
  const size_t pixelCount = static_cast<size_t>(kWidth) * kHeight;
  if (!enabled || !fullPixels || fullWidth <= 0) {
    std::fill_n(backgroundPixels_, pixelCount,
                rgb565Swapped(kScreenBackgroundRgb));
    return;
  }

  for (int32_t row = 0; row < kHeight; ++row) {
    const uint16_t* source =
        fullPixels + (kTop + row) * fullWidth + kLeft;
    uint16_t* target = backgroundPixels_ + row * kWidth;
    for (int32_t col = 0; col < kWidth; ++col) {
      target[col] = blend565Swapped(source[col], opacity);
    }
  }
}

void WaveVu::update(const AudioLevels& levels) {
  if (!root_) return;

  bool changed = false;
  for (size_t index = 0; index < levels_.size(); ++index) {
    const uint8_t target =
        levels.spectrumValid ? levels.bands[index] : 0;
    const uint8_t nextLevel = approach(levels_[index], target);
    changed = changed || nextLevel != levels_[index];
    levels_[index] = nextLevel;
    const uint8_t oldPeak = peaks_[index];
    if (levels_[index] >= peaks_[index]) {
      peaks_[index] = levels_[index];
      peakHold_[index] = 8;
    } else if (peakHold_[index]) {
      --peakHold_[index];
    } else {
      peaks_[index] = peaks_[index] > 3 ? peaks_[index] - 3 : 0;
    }
    changed = changed || oldPeak != peaks_[index];
  }
  const uint8_t nextLeft = approach(left_, levels.left);
  const uint8_t nextRight = approach(right_, levels.right);
  changed = changed || nextLeft != left_ || nextRight != right_;
  left_ = nextLeft;
  right_ = nextRight;
  const uint8_t oldLeftPeak = leftPeak_;
  const uint8_t oldRightPeak = rightPeak_;
  if (left_ >= leftPeak_) {
    leftPeak_ = left_;
    leftHold_ = 8;
  } else if (leftHold_) {
    --leftHold_;
  } else {
    leftPeak_ = leftPeak_ > 3 ? leftPeak_ - 3 : 0;
  }
  if (right_ >= rightPeak_) {
    rightPeak_ = right_;
    rightHold_ = 8;
  } else if (rightHold_) {
    --rightHold_;
  } else {
    rightPeak_ = rightPeak_ > 3 ? rightPeak_ - 3 : 0;
  }
  changed = changed || oldLeftPeak != leftPeak_ || oldRightPeak != rightPeak_;
#if DISPLAY_PROFILE_AXS15231B
  // The AXS panel has a dedicated 15 FPS VU transport.  The audio envelope
  // can legitimately retain the same quantised value between two samples,
  // but suppressing that draw also made the panel cadence depend on the
  // content rather than its clock (typically 7--9 FPS).  Keep the rendering
  // clock continuous here; this changes no audio processing or smoothing.
  render();
#else
  if (changed) render();
#endif
}

lv_obj_t* WaveVu::object() const { return root_; }

WaveVu::Mode WaveVu::mode() const { return mode_; }

bool WaveVu::active() const { return mode_ != Mode::Off; }

void WaveVu::setNativeBlitCallback(void* context,
                                  NativeBlitCallback callback) {
  nativeBlitContext_ = context;
  nativeBlitCallback_ = callback;
#if DISPLAY_PROFILE_AXS15231B
  // The direct AXS transport is the sole owner of these 240x62 pixels.
  // Leaving LVGL's canvas visible causes its independently transformed copy
  // to be painted again whenever a normal UI object (for example the clock)
  // invalidates the full frame.  Keep the root visible for touch handling,
  // but hide only its bitmap so there is exactly one VU image on the panel.
  if (canvas_) {
    if (nativeBlitCallback_)
      lv_obj_add_flag(canvas_, LV_OBJ_FLAG_HIDDEN);
    else
      lv_obj_remove_flag(canvas_, LV_OBJ_FLAG_HIDDEN);
  }
#endif
}

void WaveVu::setModeChangedCallback(void* context,
                                    void (*callback)(void*, uint8_t)) {
  modeChangedContext_ = context;
  modeChangedCallback_ = callback;
}

void WaveVu::setMode(Mode mode) {
  if (mode_ == mode) return;
  mode_ = mode;
  render();
  invalidateModeObjects();
}

void WaveVu::touchEvent(lv_event_t* event) {
  WaveVu* vu = static_cast<WaveVu*>(lv_event_get_user_data(event));
  lv_indev_t* input = lv_indev_active();
  if (!vu || !input) return;
  lv_point_t point;
  lv_indev_get_point(input, &point);
  if (lv_event_get_code(event) == LV_EVENT_PRESSED) {
    vu->pressedAt_ = point;
    return;
  }
  if (abs(point.x - vu->pressedAt_.x) > 12 ||
      abs(point.y - vu->pressedAt_.y) > 12)
    return;
  vu->mode_ = static_cast<Mode>(
      (static_cast<uint8_t>(vu->mode_) + 1) % 3);
  vu->render();
  vu->invalidateModeObjects();
  if (vu->modeChangedCallback_) {
    vu->modeChangedCallback_(vu->modeChangedContext_,
                             static_cast<uint8_t>(vu->mode_));
  }
}

void WaveVu::render() {
  if (!pixels_ || !canvas_) return;
  switch (mode_) {
    case Mode::Spectrum:
      renderSpectrum();
      break;
    case Mode::Stereo:
      renderStereo();
      break;
    case Mode::Off:
      renderOff();
      break;
  }
  if (nativeBlitCallback_) {
#if DISPLAY_PROFILE_AXS15231B
    nativeBlitCallback_(nativeBlitContext_, kLeft, kTop, kWidth, kHeight,
                        pixels_);
    return;
#endif
  }
  lv_obj_invalidate(canvas_);
}

void WaveVu::invalidateModeObjects() {
#if DISPLAY_PROFILE_AXS15231B
  if (nativeBlitCallback_ && root_) lv_obj_invalidate(root_);
#endif
}

void WaveVu::renderSpectrum() {
  constexpr int32_t kMargin = 6;
  constexpr int32_t kGap = 4;
  constexpr int32_t kBarWidth = 15;
  constexpr int32_t kSegments = 8;
  constexpr int32_t kSegmentHeight = 4;
  constexpr int32_t kSegmentGap = 2;
  constexpr int32_t kBarHeight =
      kSegments * kSegmentHeight + (kSegments - 1) * kSegmentGap;
  const int32_t bottom = kHeight - 6;
  const int32_t top = bottom - kBarHeight + 1;
  const uint16_t background = rgb565Swapped(kScreenBackgroundRgb);
  const uint16_t inactive = rgb565Swapped(0x111827);
  const uint16_t active = rgb565Swapped(0xF8FAFC);
  const uint16_t peak = rgb565Swapped(0x22D3EE);

  if (leftLabel_) lv_obj_add_flag(leftLabel_, LV_OBJ_FLAG_HIDDEN);
  if (rightLabel_) lv_obj_add_flag(rightLabel_, LV_OBJ_FLAG_HIDDEN);
  clear(background);
  for (size_t band = 0; band < levels_.size(); ++band) {
    const int32_t x = kMargin + band * (kBarWidth + kGap);
    fillRect(x, top, kBarWidth, kBarHeight, inactive);

    const int32_t activeHeight =
        (static_cast<int32_t>(levels_[band]) * kBarHeight + 254) / 255;
    if (activeHeight > 0) {
      fillRect(x, bottom - activeHeight + 1, kBarWidth, activeHeight, active);
    }
  }

  for (int32_t segment = 1; segment < kSegments; ++segment) {
    const int32_t separatorY =
        bottom - segment * (kSegmentHeight + kSegmentGap) + 1;
    for (size_t band = 0; band < levels_.size(); ++band) {
      const int32_t x = kMargin + band * (kBarWidth + kGap);
      fillRect(x, separatorY, kBarWidth, kSegmentGap, background);
    }
  }

  for (size_t band = 0; band < peaks_.size(); ++band) {
    const int32_t x = kMargin + band * (kBarWidth + kGap);
    if (peaks_[band] > 3) {
      const int32_t peakY =
          bottom -
          (static_cast<int32_t>(peaks_[band]) * (kBarHeight - 1) / 255);
      fillRect(x, peakY - 1, kBarWidth, 2, peak);
    }
  }
}

void WaveVu::renderStereo() {
  constexpr int32_t kSegments = 28;
  constexpr int32_t kSegmentWidth = 6;
  constexpr int32_t kGap = 2;
  constexpr int32_t kPeakWidth = 2;
  constexpr int32_t kLaneLeft = 18;
  const uint8_t values[2] = {left_, right_};
  const uint8_t peaks[2] = {leftPeak_, rightPeak_};
  const uint16_t background = rgb565Swapped(kScreenBackgroundRgb);
  const uint16_t inactive = rgb565Swapped(0x111827);
  const uint16_t cyan = rgb565Swapped(0x22D3EE);
  const uint16_t yellow = rgb565Swapped(0xFACC15);
  const uint16_t red = rgb565Swapped(0xFF0000);
  const uint16_t peak = rgb565Swapped(0xF8FAFC);

#if DISPLAY_PROFILE_AXS15231B
  // The AXS path sends this canvas directly to the panel, after LVGL has
  // drawn its child labels.  Draw the two channel markers into the canvas so
  // they remain visible during every native VU refresh.
  if (leftLabel_) lv_obj_add_flag(leftLabel_, LV_OBJ_FLAG_HIDDEN);
  if (rightLabel_) lv_obj_add_flag(rightLabel_, LV_OBJ_FLAG_HIDDEN);
#else
  if (leftLabel_) lv_obj_remove_flag(leftLabel_, LV_OBJ_FLAG_HIDDEN);
  if (rightLabel_) lv_obj_remove_flag(rightLabel_, LV_OBJ_FLAG_HIDDEN);
#endif
  clear(background);
  for (int lane = 0; lane < 2; ++lane) {
    const int32_t y = 9 + lane * 27;
    const uint8_t active = static_cast<uint8_t>(
        (static_cast<uint16_t>(values[lane]) * kSegments + 254) / 255);
    int32_t peakSegment = -1;
    if (peaks[lane] > 3) {
      peakSegment = min<int32_t>(kSegments - 1, peaks[lane] * kSegments / 256);
    }
    for (int32_t segment = 0; segment < kSegments; ++segment) {
        const int32_t x = kLaneLeft + segment * (kSegmentWidth + kGap);
      uint16_t color = inactive;
      const bool blockedByPeak =
          peakSegment >= 0 &&
          x + kSegmentWidth >
              kLaneLeft + peakSegment * (kSegmentWidth + kGap);
      if (segment < active && !blockedByPeak) {
        color = segment >= 24 ? red : (segment >= 18 ? yellow : cyan);
      }
      fillRect(x, y, kSegmentWidth, 13, color);
    }
    if (peaks[lane] > 3) {
      const int32_t peakX =
          kLaneLeft + peakSegment * (kSegmentWidth + kGap) + kSegmentWidth -
          kPeakWidth;
      fillRect(peakX, y - 1, kPeakWidth, 15, peak);
    }
  }

#if DISPLAY_PROFILE_AXS15231B
  const uint16_t label = rgb565Swapped(0x94A3B8);
  drawCachedGlyph(leftGlyph_, 2, 10, label);
  drawCachedGlyph(rightGlyph_, 2, 37, label);
#endif
}

void WaveVu::cacheFontGlyph(uint32_t codepoint, CachedGlyph& glyph) {
  glyph = {};
  lv_font_glyph_dsc_t descriptor{};
  const lv_font_t* labelFont = &lv_font_montserrat_14;
  if (!lv_font_get_glyph_dsc(labelFont, &descriptor, codepoint,
                             0) ||
      descriptor.box_w > 20 || descriptor.box_h > 24) {
    return;
  }

  lv_draw_buf_t* buffer = lv_draw_buf_create(
      descriptor.box_w, descriptor.box_h, LV_COLOR_FORMAT_A8, LV_STRIDE_AUTO);
  if (!buffer) return;
  const auto* glyphBuffer = static_cast<const lv_draw_buf_t*>(
      lv_font_get_glyph_bitmap(&descriptor, buffer));
  const uint8_t* bitmap = glyphBuffer ? glyphBuffer->data : nullptr;
  if (bitmap) {
    const uint32_t stride =
        lv_draw_buf_width_to_stride(descriptor.box_w, LV_COLOR_FORMAT_A8);
    glyph.width = static_cast<uint8_t>(descriptor.box_w);
    glyph.height = static_cast<uint8_t>(descriptor.box_h);
    glyph.offsetX = static_cast<int8_t>(descriptor.ofs_x);
    glyph.offsetY = static_cast<int8_t>(descriptor.ofs_y);
    for (uint8_t row = 0; row < glyph.height; ++row) {
      std::copy_n(bitmap + row * stride, glyph.width,
                  glyph.alpha.begin() + row * 20);
    }
  }
  lv_font_glyph_release_draw_data(&descriptor);
  lv_draw_buf_destroy(buffer);
}

void WaveVu::drawCachedGlyph(const CachedGlyph& glyph, int32_t x, int32_t y,
                             uint16_t color) {
  if (!pixels_ || glyph.width == 0 || glyph.height == 0) return;
  for (uint8_t row = 0; row < glyph.height; ++row) {
    const int32_t targetY = y + row + glyph.offsetY;
    if (targetY < 0 || targetY >= kHeight) continue;
    for (uint8_t col = 0; col < glyph.width; ++col) {
      const int32_t targetX = x + col + glyph.offsetX;
      if (targetX < 0 || targetX >= kWidth) continue;
      const uint8_t opacity = glyph.alpha[row * 20 + col];
      if (opacity == 0) continue;
      uint16_t& target = pixels_[targetY * kWidth + targetX];
      target = blend565SwappedOver(target, color, opacity);
    }
  }
}

void WaveVu::renderOff() {
  if (leftLabel_) lv_obj_add_flag(leftLabel_, LV_OBJ_FLAG_HIDDEN);
  if (rightLabel_) lv_obj_add_flag(rightLabel_, LV_OBJ_FLAG_HIDDEN);
  clear(rgb565Swapped(kScreenBackgroundRgb));
  const uint16_t dot = rgb565Swapped(0x1E293B);
  for (int32_t index = 0; index < 12; ++index) {
    fillRect(10 + index * 19, 30, 5, 4, dot);
  }
}

void WaveVu::clear(uint16_t color) {
  const size_t pixelCount = static_cast<size_t>(kWidth) * kHeight;
  if (backgroundPixels_) {
    std::copy_n(backgroundPixels_, pixelCount, pixels_);
    return;
  }
  std::fill_n(pixels_, pixelCount, color);
}

void WaveVu::fillRect(int32_t x, int32_t y, int32_t width, int32_t height,
                      uint16_t color) {
  const int32_t x1 = std::max<int32_t>(0, x);
  const int32_t y1 = std::max<int32_t>(0, y);
  const int32_t x2 = std::min<int32_t>(kWidth, x + width);
  const int32_t y2 = std::min<int32_t>(kHeight, y + height);
  if (x1 >= x2 || y1 >= y2) return;
  for (int32_t row = y1; row < y2; ++row) {
    std::fill(pixels_ + row * kWidth + x1,
              pixels_ + row * kWidth + x2, color);
  }
}

uint8_t WaveVu::approach(uint8_t value, uint8_t target) {
  if (target == value) return value;
  if (target > value) return target;
  const uint8_t difference = value - target;
  return static_cast<uint8_t>(
      max<int>(target, value - max<int>(4, difference / 2)));
}
