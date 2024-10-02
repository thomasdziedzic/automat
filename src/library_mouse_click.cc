// SPDX-FileCopyrightText: Copyright 2024 Automat Authors
// SPDX-License-Identifier: MIT
#include "library_mouse_click.hh"

#include "audio.hh"
#include "base.hh"
#include "widget.hh"

#if defined(_WIN32)
#include <windows.h>
#endif

#if defined(__linux__)
#include <xcb/xtest.h>

#pragma comment(lib, "xcb-xtest")
#endif

#include <include/core/SkAlphaType.h>
#include <include/core/SkBitmap.h>
#include <include/core/SkBlendMode.h>
#include <include/core/SkCanvas.h>
#include <include/core/SkColor.h>
#include <include/core/SkColorFilter.h>
#include <include/core/SkImage.h>
#include <include/core/SkSamplingOptions.h>
#include <include/gpu/GrDirectContext.h>
#include <include/gpu/GrRecordingContext.h>
#include <include/gpu/ganesh/SkImageGanesh.h>

#include "../build/generated/embedded.hh"
#include "argument.hh"
#include "drag_action.hh"
#include "pointer.hh"
#include "prototypes.hh"
#include "svg.hh"
#include "textures.hh"

#if defined(__linux__)
#include "linux_main.hh"
#endif

using namespace maf;

namespace automat::library {

const MouseClick MouseClick::lmb_down(gui::PointerButton::Left, true);
const MouseClick MouseClick::lmb_up(gui::PointerButton::Left, false);
const MouseClick MouseClick::rmb_down(gui::PointerButton::Right, true);
const MouseClick MouseClick::rmb_up(gui::PointerButton::Right, false);

__attribute__((constructor)) void RegisterMouseClick() {
  RegisterPrototype(MouseClick::lmb_down);
  RegisterPrototype(MouseClick::lmb_up);
  RegisterPrototype(MouseClick::rmb_down);
  RegisterPrototype(MouseClick::rmb_up);
}

static sk_sp<SkImage> MouseBaseImage(gui::DrawContext& ctx) {
  return MakeImageFromAsset(embedded::assets_mouse_base_webp, &ctx);
}

static sk_sp<SkImage> MouseLMBMask(gui::DrawContext& ctx) {
  return MakeImageFromAsset(embedded::assets_mouse_lmb_mask_webp, &ctx);
}

static sk_sp<SkImage> MouseRMBMask(gui::DrawContext& ctx) {
  return MakeImageFromAsset(embedded::assets_mouse_rmb_mask_webp, &ctx);
}

constexpr float kScale = 0.00005;

static sk_sp<SkImage> RenderMouseImage(gui::DrawContext& ctx, gui::PointerButton button,
                                       bool down) {
  auto& root_canvas = ctx.canvas;
  auto base = MouseBaseImage(ctx);
  auto mask = button == gui::PointerButton::Left ? MouseLMBMask(ctx) : MouseRMBMask(ctx);
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
    SkPath path = PathFromSVG(kArrowShape).makeScale(1 / kScale, 1 / kScale);
    SkPaint paint;
    paint.setBlendMode(SkBlendMode::kMultiply);
    paint.setAlphaf(0.9f);
    canvas.translate(button == gui::PointerButton::Left ? 85 : 285, 130);
    if (down) {
      paint.setColor(SkColorSetARGB(255, 255, 128, 128));
      canvas.scale(1, -1);
    } else {
      paint.setColor(SkColorSetARGB(255, 118, 235, 235));
    }
    canvas.drawPath(path, paint);
  }
  bitmap.setImmutable();
  auto raster_image = SkImages::RasterFromBitmap(bitmap);
#ifdef CPU_RENDERING
  return raster_image;
#else
  return SkImages::TextureFromImage(root_canvas.recordingContext()->asDirectContext(),
                                    raster_image.get(), skgpu::Mipmapped::kYes);
#endif
}

static sk_sp<SkImage> CachedMouseImage(gui::DrawContext dctx, gui::PointerButton button,
                                       bool down) {
  Str key = f("MouseImage:%d:%d", (int)button, (int)down);
  return CacheImage(dctx, key, [&dctx, button, down]() -> sk_sp<SkImage> {
    return RenderMouseImage(dctx, button, down);
  });
}

MouseClick::MouseClick(gui::PointerButton button, bool down) : button(button), down(down) {}
string_view MouseClick::Name() const {
  switch (button) {
    case gui::PointerButton::Left:
      if (down) {
        return "Mouse Left Down"sv;
      } else {
        return "Mouse Left Up"sv;
      }
    case gui::PointerButton::Right:
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
animation::Phase MouseClick::Draw(gui::DrawContext& ctx) const {
  auto& canvas = ctx.canvas;
  auto mouse_image = CachedMouseImage(ctx, button, down);
  canvas.save();
  canvas.scale(kScale, -kScale);
  canvas.translate(0, -mouse_image->height());
  SkSamplingOptions sampling = SkSamplingOptions(SkFilterMode::kLinear, SkMipmapMode::kLinear);
  canvas.drawImage(mouse_image, 0, 0, sampling);
  canvas.restore();
  return animation::Finished;
}
SkPath MouseClick::Shape(animation::Display*) const {
  return SkPath::Rect(SkRect::MakeXYWH(0, 0, 373 * kScale, 624 * kScale));
}

void MouseClick::Args(std::function<void(Argument&)> cb) { cb(next_arg); }

audio::Sound& MouseClick::NextSound() {
  return down ? embedded::assets_SFX_mouse_down_wav : embedded::assets_SFX_mouse_up_wav;
}

LongRunning* MouseClick::OnRun(Location& location) {
#if defined(_WIN32)
  INPUT input;
  input.type = INPUT_MOUSE;
  input.mi.dx = 0;
  input.mi.dy = 0;
  input.mi.mouseData = 0;
  input.mi.dwFlags = MOUSEEVENTF_ABSOLUTE;
  input.mi.time = 0;
  input.mi.dwExtraInfo = 0;
  switch (button) {
    case gui::PointerButton::Left:
      input.mi.dwFlags |= down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
      break;
    case gui::PointerButton::Right:
      input.mi.dwFlags |= down ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP;
      break;
    default:
      return nullptr;
  }
  SendInput(1, &input, sizeof(INPUT));
#endif
#if defined(__linux__)
  U8 type = down ? XCB_BUTTON_PRESS : XCB_BUTTON_RELEASE;
  U8 detail = button == gui::PointerButton::Left ? 1 : 3;
  xcb_test_fake_input(connection, type, detail, XCB_CURRENT_TIME, XCB_NONE, 0, 0, 0);
  xcb_flush(connection);
#endif
  return nullptr;
}

}  // namespace automat::library