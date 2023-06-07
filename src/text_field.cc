#include "text_field.hh"

#include <include/core/SkColor.h>
#include <include/core/SkFontTypes.h>
#include <include/core/SkMatrix.h>
#include <src/base/SkUTF.h>

#include <memory>
#include <numeric>

#include "font.hh"
#include "log.hh"
#include "root.hh"

namespace automat::gui {

void TextField::PointerOver(Pointer& pointer, animation::Context& actx) {
  pointer.PushIcon(Pointer::kIconIBeam);
  hover_ptr[actx].Increment();
}

void TextField::PointerLeave(Pointer& pointer, animation::Context& actx) {
  pointer.PopIcon();
  hover_ptr[actx].Decrement();
}

void DrawDebugTextOutlines(SkCanvas& canvas, std::string* text) {
  const char* c_str = text->c_str();
  size_t byte_length = text->size();
  Font& font = GetFont();
  int glyph_count = font.sk_font.countText(c_str, byte_length, SkTextEncoding::kUTF8);
  SkGlyphID glyphs[glyph_count];
  font.sk_font.textToGlyphs(c_str, byte_length, SkTextEncoding::kUTF8, glyphs, glyph_count);

  SkScalar widths[glyph_count];
  SkRect bounds[glyph_count];
  font.sk_font.getWidthsBounds(glyphs, glyph_count, widths, bounds, nullptr);

  // Draw glyph outlines for debugging
  canvas.save();
  canvas.scale(font.font_scale, -font.font_scale);
  SkPaint outline;
  outline.setStyle(SkPaint::kStroke_Style);
  outline.setColor(SkColorSetRGB(0xff, 0x00, 0x00));
  SkPaint line;
  line.setStyle(SkPaint::kStroke_Style);
  line.setColor(SkColorSetRGB(0x00, 0x80, 0x00));
  for (int i = 0; i < glyph_count; ++i) {
    auto& b = bounds[i];
    canvas.drawRect(b, outline);
    canvas.drawLine(0, 0, widths[i], 0, line);
    canvas.drawCircle(0, 0, 0.5, line);
    canvas.translate(widths[i], 0);
  }
  canvas.scale(1 / font.font_scale, -1 / font.font_scale);
  canvas.restore();
}

SkRRect TextField::ShapeRRect() const {
  return SkRRect::MakeRectXY(SkRect::MakeXYWH(0, 0, width, kTextFieldHeight), kTextCornerRadius,
                             kTextCornerRadius);
}

static SkPaint kDefaultTextPaint = []() {
  SkPaint paint;
  paint.setColor(SK_ColorBLACK);
  paint.setAntiAlias(true);
  return paint;
}();

static SkPaint kDefaultBackgroundPaint = []() {
  SkPaint paint;
  paint.setColor(SK_ColorWHITE);
  paint.setAntiAlias(true);
  return paint;
}();

const SkPaint& TextField::GetTextPaint() const { return kDefaultTextPaint; }
const SkPaint& TextField::GetBackgroundPaint() const { return kDefaultBackgroundPaint; }

void TextField::Draw(DrawContext& ctx) const {
  auto& canvas = ctx.canvas;
  auto& actx = ctx.animation_context;
  auto& hover = hover_ptr[actx].animation;
  hover.Tick(actx);

  Font& font = GetFont();
  SkRRect rrect = ShapeRRect();
  canvas.drawRRect(rrect, GetBackgroundPaint());
  if (hover.value > 0.0001) {
    SkPaint hover_outline;
    hover_outline.setColor(SkColorSetRGB(0xff, 0x00, 0x00));
    hover_outline.setStyle(SkPaint::kStroke_Style);
    hover_outline.setStrokeWidth(hover.value * 0.0005);
    canvas.drawRRect(rrect, hover_outline);
  }
  canvas.translate(kTextMargin, (kTextFieldHeight - kLetterSize) / 2);
  SkRect underline_rect =
      SkRect::MakeXYWH(0, -font.line_thickness, width - 2 * kTextMargin, -font.line_thickness);
  canvas.drawRect(underline_rect, GetTextPaint());
  if (text) {
    font.DrawText(canvas, *text, GetTextPaint());
    // DrawDebugTextOutlines(canvas, text);
  }
}

SkPath TextField::Shape() const { return SkPath::RRect(ShapeRRect()); }

static Vec2 GetPositionFromIndex(std::string_view text, int index) {
  return Vec2(kTextMargin + GetFont().PositionFromIndex(text, index),
              (kTextFieldHeight - kLetterSize) / 2);
}

void UpdateCaret(TextField& text_field, Caret& caret) {
  int index = text_field.caret_positions[&caret].index;
  Vec2 caret_pos = GetPositionFromIndex(*text_field.text, index);
  caret.PlaceIBeam(caret_pos);
}

struct TextSelectAction : Action {
  TextField& text_field;
  Caret* caret = nullptr;

  TextSelectAction(TextField& text_field) : text_field(text_field) {}

  int GetIndexFromPointer(Pointer& pointer) {
    Vec2 local = pointer.PositionWithin(text_field);
    return GetFont().IndexFromPosition(*text_field.text, local.x - kTextMargin);
  }

  void UpdateCaretFromPointer(Pointer& pointer) {
    auto it = text_field.caret_positions.find(caret);
    // The caret might have been released.
    if (it == text_field.caret_positions.end()) {
      return;
    }
    int index = GetIndexFromPointer(pointer);
    if (index != it->second.index) {
      it->second.index = index;
      UpdateCaret(text_field, *caret);
    }
  }

  void Begin(Pointer& pointer) override {
    int index = GetIndexFromPointer(pointer);
    Vec2 pos = GetPositionFromIndex(*text_field.text, index);
    caret = &text_field.RequestCaret(pointer.Keyboard(), pointer.Path(), pos);
    text_field.caret_positions[caret] = {.index = index};
  }
  void Update(Pointer& pointer) override { UpdateCaretFromPointer(pointer); }
  void End() override {}
  void DrawAction(DrawContext&) override {}
};

std::unique_ptr<Action> TextField::ButtonDownAction(Pointer&, PointerButton btn) {
  if (btn == PointerButton::kMouseLeft) {
    return std::make_unique<TextSelectAction>(*this);
  }
  return nullptr;
}

void TextField::ReleaseCaret(Caret& caret) { caret_positions.erase(&caret); }

std::string FilterControlCharacters(const std::string& text) {
  std::string clean = "";
  const char* ptr = text.c_str();
  const char* end = ptr + text.size();
  while (ptr < end) {
    const char* start = ptr;
    SkUnichar uni = SkUTF::NextUTF8(&ptr, end);
    if (uni < 0x20) {
      continue;
    }
    clean.append(start, ptr);
  }
  return clean;
}

void TextField::KeyDown(Caret& caret, Key k) {
  switch (k.physical) {
    case AnsiKey::Delete: {
      int begin = caret_positions[&caret].index;
      int end = GetFont().NextIndex(*text, begin);
      if (end != begin) {
        text->erase(begin, end - begin);
        // No need to update caret after delete.
      }
      break;
    }
    case AnsiKey::Backspace: {
      int& i_ref = caret_positions[&caret].index;
      int end = i_ref;
      if (i_ref > 0) {
        i_ref = GetFont().PrevIndex(*text, i_ref);
        text->erase(i_ref, end - i_ref);
        UpdateCaret(*this, caret);
      }
      break;
    }
    case AnsiKey::Left: {
      int& i_ref = caret_positions[&caret].index;
      if (i_ref > 0) {
        i_ref = GetFont().PrevIndex(*text, i_ref);
        UpdateCaret(*this, caret);
      }
      break;
    }
    case AnsiKey::Right: {
      int& i_ref = caret_positions[&caret].index;
      if (i_ref < text->size()) {
        i_ref = GetFont().NextIndex(*text, i_ref);
        UpdateCaret(*this, caret);
      }
      break;
    }
    case AnsiKey::Home: {
      caret_positions[&caret].index = 0;
      UpdateCaret(*this, caret);
      break;
    }
    case AnsiKey::End: {
      caret_positions[&caret].index = text->size();
      UpdateCaret(*this, caret);
      break;
    }
    default: {
      std::string clean = FilterControlCharacters(k.text);
      if (!clean.empty()) {
        text->insert(caret_positions[&caret].index, clean);
        caret_positions[&caret].index += clean.size();
        UpdateCaret(*this, caret);
        std::string text_hex = "";
        for (char c : clean) {
          text_hex += f(" %02x", (uint8_t)c);
        }
      }
    }
  }
}

void TextField::KeyUp(Caret&, Key) {}

}  // namespace automat::gui
