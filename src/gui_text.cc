#include "gui_text.hh"

#include "font.hh"
#include "gui_constants.hh"

using namespace std;

namespace automat::gui {

Text::Text(string_view text) : text(text) {};

SkPath Text::Shape(animation::Display*) const {
  float w = GetFont().MeasureText(text);
  return SkPath::Rect(SkRect::MakeWH(w, kLetterSize));
}

animation::Phase Text::Draw(DrawContext& ctx) const {
  GetFont().DrawText(ctx.canvas, text, paint);
  return animation::Finished;
}

maf::Optional<Rect> Text::TextureBounds() const { return nullopt; }
}  // namespace automat::gui