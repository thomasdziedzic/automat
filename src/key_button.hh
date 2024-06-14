#pragma once

#include "gui_button.hh"

namespace automat::library {

using namespace automat::gui;

static constexpr float kKeyHeight = gui::kMinimalTouchableSize;
static constexpr float kBaseKeyWidth = kKeyHeight;

static constexpr float kKeyLetterSize = 2.4_mm;
static constexpr float kKeyLetterSizeMM = kKeyLetterSize * 1000;

// Margins around the key face
static constexpr float kKeyTopSide = 0.5_mm;
static constexpr float kKeySide = 1_mm;
static constexpr float kKeyBottomSide = 1.5_mm;

static constexpr float kKeyFaceRadius = 1_mm;
static constexpr float kKeyBaseRadius = kKeyFaceRadius;

// static constexpr float kKeySpareHeight = kKeyHeight - kKeyLetterSize;
// static constexpr float kKeyFaceHeight = kKeyHeight - kKeyTopSide - kKeyBottomSide;

struct KeyButton : gui::Button {
  float width;
  std::function<void(gui::Pointer&)> activate;
  KeyButton(std::unique_ptr<Widget> child, SkColor color, float width);
  void Activate(gui::Pointer&) override;
  SkRRect RRect() const override;
  void DrawButtonFace(gui::DrawContext&, SkColor bg, SkColor fg) const override;
};

std::unique_ptr<Widget> MakeKeyLabelWidget(maf::StrView label);

static constexpr SkColor kKeyEnabledColor = "#f3a75b"_color;
static constexpr SkColor kKeyDisabledColor = "#f4efea"_color;
static constexpr SkColor kKeyGrabbingColor = "#f15555"_color;

static SkColor KeyColor(bool enabled) { return enabled ? kKeyEnabledColor : kKeyDisabledColor; }

}  // namespace automat::library