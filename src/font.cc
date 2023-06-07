#include "font.hh"

#include <include/core/SkData.h>
#include <modules/skshaper/include/SkShaper.h>
#include <modules/skunicode/include/SkUnicode.h>
#include <src/base/SkUTF.h>

#include "generated/assets.hh"
#include "log.hh"

#pragma comment(lib, "skshaper")
#pragma comment(lib, "skunicode")
#ifdef _WIN32
#pragma comment(lib, "Advapi32")
#endif

namespace automat::gui {

std::unique_ptr<Font> Font::Make(float letter_size_mm) {
  constexpr float kMilimetersPerInch = 25.4;
  constexpr float kPointsPerInch = 72;
  // We want text to be `letter_size_mm` tall (by cap size).
  float letter_size_pt = letter_size_mm / kMilimetersPerInch * kPointsPerInch;
  float font_size_guess = letter_size_pt / 0.7f;  // this was determined empirically
  // Create the font using the approximate size.
  // TODO: embed & use Noto Color Emoji
  auto data = SkData::MakeWithoutCopy(assets::NotoSans_wght__ttf, assets::NotoSans_wght__ttf_size);
  auto typeface = SkTypeface::MakeFromData(data);
  SkFont sk_font(typeface, font_size_guess, 1.f, 0.f);
  SkFontMetrics metrics;
  sk_font.getMetrics(&metrics);
  sk_font.setBaselineSnap(false);
  sk_font.setSubpixel(true);
  // The `fCapHeight` is the height of the capital letters.
  float font_scale = 0.001 * letter_size_mm / metrics.fCapHeight;
  float line_thickness = metrics.fUnderlineThickness * font_scale;
  return std::make_unique<Font>(sk_font, font_scale, line_thickness);
}

struct LineRunHandler : public SkShaper::RunHandler {
  LineRunHandler(std::string_view utf8_text) : utf8_text(utf8_text), offset() {}
  sk_sp<SkTextBlob> makeBlob() { return builder.make(); }

  void beginLine() override {}
  void runInfo(const RunInfo& info) override {}
  void commitRunInfo() override {}
  Buffer runBuffer(const RunInfo& info) override {
    int glyphCount = SkTFitsIn<int>(info.glyphCount) ? info.glyphCount : INT_MAX;
    int utf8RangeSize = SkTFitsIn<int>(info.utf8Range.size()) ? info.utf8Range.size() : INT_MAX;

    run_buffer = &builder.allocRunTextPos(info.fFont, glyphCount, utf8RangeSize);
    if (run_buffer->utf8text && !utf8_text.empty()) {
      memcpy(run_buffer->utf8text, utf8_text.data() + info.utf8Range.begin(), utf8RangeSize);
    }

    return {run_buffer->glyphs,  // buffer that will be filled with glyph IDs
            run_buffer->points(), nullptr, run_buffer->clusters, offset};
  }
  void commitRunBuffer(const RunInfo& info) override {
    for (int i = 0; i < info.glyphCount; ++i) {
      run_buffer->clusters[i] -= info.utf8Range.begin();
    }
    offset += info.fAdvance;
  }
  void commitLine() override {}

  // All values use scaled text units.
  // Scaled text units have flipped Y axis and are significantly larger than
  // meters.

  std::string_view utf8_text;
  Vec2 offset;  // Position where the letters will be placed (baseline).
  SkTextBlobBuilder builder;

  // Temporary used between `runBuffer` and `commitRunBuffer`.
  // glyphs[i] begins at utf8_text[clusters[i] + cluster_offset]
  const SkTextBlobBuilder::RunBuffer* run_buffer;
};

struct MeasureLineRunHandler : public LineRunHandler {
  // Arrays indexed by glyph index.
  std::vector<float> positions;
  std::vector<int> utf8_indices;

  MeasureLineRunHandler(std::string_view utf8_text) : LineRunHandler(utf8_text) {}

  void commitRunBuffer(const RunInfo& info) override {
    for (int i = 0; i < info.glyphCount; ++i) {
      positions.push_back(run_buffer->points()[i].x() + offset.x);
      utf8_indices.push_back(run_buffer->clusters[i]);
    }
    LineRunHandler::commitRunBuffer(info);
  }
  void commitLine() override {
    positions.push_back(offset.x);
    if (utf8_indices.empty()) {
      utf8_indices.push_back(0);
    } else {
      const char* ptr = utf8_text.data() + utf8_indices.back();
      const char* end = utf8_text.data() + utf8_text.size();
      SkUTF::NextUTF8(&ptr, end);
      int idx = ptr - utf8_text.data();
      utf8_indices.push_back(idx);
    }
    LineRunHandler::commitLine();
  }

  int IndexFromPosition(float x) {
    for (int i = 1; i < positions.size(); ++i) {
      float center = (positions[i - 1] + positions[i]) / 2;
      if (x < center) {
        return utf8_indices[i - 1];
      }
    }
    return utf8_indices.back();
  }
};

SkShaper& GetShaper() {
  thread_local std::unique_ptr<SkShaper> shaper =
      SkShaper::MakeShapeDontWrapOrReorder(SkUnicode::MakeIcuBasedUnicode());
  return *shaper;
}

int Font::PrevIndex(std::string_view text, int index) {
  if (index == 0) {
    return 0;
  }
  SkShaper& shaper = GetShaper();
  MeasureLineRunHandler run_handler(text);
  shaper.shape(text.data(), index, sk_font, true, 0, &run_handler);
  if (run_handler.utf8_indices.size() > 1) {
    return run_handler.utf8_indices[run_handler.utf8_indices.size() - 2];
  }
  return run_handler.utf8_indices.back();
}

int Font::NextIndex(std::string_view text, int index) {
  if (index + 1 >= text.size()) {
    return text.size();
  }
  SkShaper& shaper = GetShaper();
  text = text.substr(index);
  MeasureLineRunHandler run_handler(text);
  shaper.shape(text.data(), text.size(), sk_font, true, 0, &run_handler);
  if (run_handler.utf8_indices.size() > 1) {
    return index + run_handler.utf8_indices[1];
  }
  return index + run_handler.utf8_indices[0];
}

float Font::PositionFromIndex(std::string_view text, int index) {
  if (index == 0) {
    return 0;
  }
  SkShaper& shaper = GetShaper();
  LineRunHandler run_handler(text);
  shaper.shape(text.data(), index, sk_font, true, 0, &run_handler);
  return run_handler.offset.x * font_scale;
}

int Font::IndexFromPosition(std::string_view text, float x) {
  x /= font_scale;
  SkShaper& shaper = GetShaper();
  MeasureLineRunHandler run_handler(text);
  shaper.shape(text.data(), text.size(), sk_font, true, 0, &run_handler);
  return run_handler.IndexFromPosition(x);
}

void Font::DrawText(SkCanvas& canvas, std::string_view text, const SkPaint& paint) {
  canvas.scale(font_scale, -font_scale);
  SkShaper& shaper = GetShaper();
  LineRunHandler run_handler(text);
  shaper.shape(text.data(), text.size(), sk_font, true, 0, &run_handler);

  sk_sp<SkTextBlob> text_blob = run_handler.makeBlob();
  canvas.drawTextBlob(text_blob, 0, 0, paint);

  canvas.scale(1 / font_scale, -1 / font_scale);
}

float Font::MeasureText(std::string_view text) { return PositionFromIndex(text, text.size()); }

Font& GetFont() {
  static std::unique_ptr<Font> font = Font::Make(kLetterSizeMM);
  return *font;
}

}  // namespace automat::gui