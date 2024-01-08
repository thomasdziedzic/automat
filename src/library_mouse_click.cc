#include "library_mouse_click.hh"

#include <include/core/SkAlphaType.h>
#include <include/core/SkBitmap.h>
#include <include/core/SkBlendMode.h>
#include <include/core/SkCanvas.h>
#include <include/core/SkColor.h>
#include <include/core/SkColorFilter.h>
#include <include/core/SkImage.h>
#include <include/core/SkSamplingOptions.h>

#include "../build/generated/embedded.hh"
#include "argument.hh"
#include "drag_action.hh"
#include "pointer.hh"
#include "prototypes.hh"
#include "virtual_fs.hh"

using namespace maf;

namespace automat::library {

const MouseClick MouseClick::lmb_down(gui::PointerButton::kMouseLeft, true);
const MouseClick MouseClick::lmb_up(gui::PointerButton::kMouseLeft, false);
const MouseClick MouseClick::rmb_down(gui::PointerButton::kMouseRight, true);
const MouseClick MouseClick::rmb_up(gui::PointerButton::kMouseRight, false);

__attribute__((constructor)) void RegisterMouseClick() {
  RegisterPrototype(MouseClick::lmb_down);
  RegisterPrototype(MouseClick::lmb_up);
  RegisterPrototype(MouseClick::rmb_down);
  RegisterPrototype(MouseClick::rmb_up);
}

static sk_sp<SkImage> MakeImageFromAsset(fs::VFile& asset) {
  auto& content = asset.content;
  auto data = SkData::MakeWithoutCopy(content.data(), content.size());
  auto image = SkImages::DeferredFromEncodedData(data, SkAlphaType::kUnpremul_SkAlphaType);
  return image;
}

static sk_sp<SkImage>& MouseBaseImage() {
  static auto image = MakeImageFromAsset(embedded::assets_mouse_base_webp);
  return image;
}

static sk_sp<SkImage>& MouseLMBMask() {
  static auto image = MakeImageFromAsset(embedded::assets_mouse_lmb_mask_webp);
  return image;
}

static sk_sp<SkImage>& MouseRMBMask() {
  static auto image = MakeImageFromAsset(embedded::assets_mouse_rmb_mask_webp);
  return image;
}

static sk_sp<SkImage>& ArrowImage() {
  static auto image = MakeImageFromAsset(embedded::assets_arrow_webp);
  return image;
}

static sk_sp<SkImage> RenderMouseImage(gui::PointerButton button, bool down) {
  auto base = MouseBaseImage();
  auto mask = button == gui::kMouseLeft ? MouseLMBMask() : MouseRMBMask();
  auto arrow = ArrowImage();
  SkBitmap bitmap;
  SkSamplingOptions sampling;
  bitmap.allocN32Pixels(base->width(), base->height());
  SkCanvas canvas(bitmap);
  {  // Select LMB
    SkPaint paint;
    canvas.drawImage(base, 0, 0);
    paint.setBlendMode(SkBlendMode::kSrcIn);
    canvas.drawImage(mask, 0, 0, sampling, &paint);
    canvas.drawColor(down ? SK_ColorRED : SK_ColorCYAN, SkBlendMode::kSrcIn);
  }
  {  // Draw Mouse Base
    SkPaint paint;
    paint.setBlendMode(SkBlendMode::kScreen);
    canvas.drawImage(base, 0, 0, sampling, &paint);
  }
  {  // Draw arrow
    SkPaint paint;
    paint.setBlendMode(SkBlendMode::kMultiply);
    paint.setColorFilter(SkColorFilters::Blend(
        down ? SkColorSetARGB(255, 255, 128, 128) : SkColorSetARGB(255, 118, 235, 235),
        SkBlendMode::kSrcIn));
    paint.setAlphaf(0.9f);
    canvas.translate(button == gui::kMouseLeft ? 35 : 235, 80);
    if (down) {
    } else {
      canvas.translate(arrow->width() / 2.f, arrow->height() / 2.f);
      canvas.scale(1, down ? 1 : -1);
      canvas.translate(-arrow->width() / 2.f, 0);
    }
    canvas.scale(0.5, 0.5);
    canvas.drawImage(arrow, 0, 0, sampling, &paint);
  }
  bitmap.setImmutable();
  return SkImages::RasterFromBitmap(bitmap);
}

static sk_sp<SkImage>& CachedMouseImage(gui::PointerButton button, bool down) {
  switch (button) {
    case gui::PointerButton::kMouseLeft:
      if (down) {
        static auto image = RenderMouseImage(button, down);
        return image;
      } else {
        static auto image = RenderMouseImage(button, down);
        return image;
      }
    case gui::PointerButton::kMouseRight:
      if (down) {
        static auto image = RenderMouseImage(button, down);
        return image;
      } else {
        static auto image = RenderMouseImage(button, down);
        return image;
      }
    default:
      static auto image = RenderMouseImage(button, down);
      return image;
  }
}

constexpr float kScale = 0.00005;

MouseClick::MouseClick(gui::PointerButton button, bool down) : button(button), down(down) {}
string_view MouseClick::Name() const {
  switch (button) {
    case gui::PointerButton::kMouseLeft:
      if (down) {
        return "Mouse Left Down"sv;
      } else {
        return "Mouse Left Up"sv;
      }
    case gui::PointerButton::kMouseRight:
      if (down) {
        return "Mouse Right Down"sv;
      } else {
        return "Mouse Right Up"sv;
      }
    default:
      return "Mouse Unknown Click"sv;
  }
}
std::unique_ptr<Object> MouseClick::Clone() const {
  return std::make_unique<MouseClick>(button, down);
}
void MouseClick::Draw(gui::DrawContext& ctx) const {
  auto& canvas = ctx.canvas;
  auto& mouse_image = CachedMouseImage(button, down);
  canvas.save();
  canvas.scale(kScale, -kScale);
  canvas.translate(0, -mouse_image->height());
  SkSamplingOptions sampling = SkSamplingOptions(SkFilterMode::kLinear, SkMipmapMode::kLinear);
  canvas.drawImage(mouse_image, 0, 0, sampling);
  canvas.restore();
}
SkPath MouseClick::Shape() const {
  auto& mouse_base_image = MouseBaseImage();
  return SkPath::Rect(SkRect::MakeXYWH(0, 0, mouse_base_image->width() * kScale,
                                       mouse_base_image->height() * kScale));
}
std::unique_ptr<Action> MouseClick::ButtonDownAction(gui::Pointer& pointer,
                                                     gui::PointerButton btn) {
  if (btn != gui::PointerButton::kMouseLeft) {
    return nullptr;
  }
  auto& path = pointer.Path();
  if (path.size() < 2) {
    return nullptr;
  }
  auto* parent = path[path.size() - 2];
  Location* location = dynamic_cast<Location*>(parent);
  if (!location) {
    return nullptr;
  }
  std::unique_ptr<DragLocationAction> action = std::make_unique<DragLocationAction>(location);
  action->contact_point = pointer.PositionWithin(*this);
  return action;
}

void MouseClick::Args(std::function<void(Argument&)> cb) { cb(then_arg); }

}  // namespace automat::library