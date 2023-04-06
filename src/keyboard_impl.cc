#include "keyboard_impl.h"

#include "font.h"

namespace automaton::gui {

CaretImpl::CaretImpl(KeyboardImpl &keyboard)
    : facade(*this), keyboard(keyboard) {}

CaretImpl::~CaretImpl() {}

void CaretImpl::PlaceIBeam(vec2 canvas_position) {
  float width = GetFont().line_thickness;
  float height = kLetterSize;
  shape = SkPath::Rect(SkRect::MakeXYWH(canvas_position.X - width / 2,
                                        canvas_position.Y, width, height));
  last_blink = time::now();
}

void CaretImpl::Draw(SkCanvas &canvas,
                     animation::State &animation_state) const {
  SkPaint paint;
  paint.setColor(SK_ColorBLACK);
  paint.setAntiAlias(true);
  double now = (animation_state.timer.now - last_blink).count();
  double seconds, subseconds;
  subseconds = modf(now, &seconds);
  if (subseconds < 0.5) {
    canvas.drawPath(shape, paint);
  }
}

KeyboardImpl::KeyboardImpl(WindowImpl &window, Keyboard &facade)
    : window(window), facade(facade) {
  window.keyboards.emplace_back(this);
}

KeyboardImpl::~KeyboardImpl() {
  auto it = std::find(window.keyboards.begin(), window.keyboards.end(), this);
  if (it != window.keyboards.end()) {
    window.keyboards.erase(it);
  }
}

void KeyboardImpl::Draw(SkCanvas &canvas,
                        animation::State &animation_state) const {
  for (auto &caret : carets) {
    caret->Draw(canvas, animation_state);
  }
}

void KeyboardImpl::KeyDown(Key key) {
  if (key.physical > AnsiKey::Unknown && key.physical < AnsiKey::Count) {
    pressed_keys.set((size_t)key.physical);
  }
  for (auto &caret : carets) {
    if (caret->owner) {
      caret->owner->KeyDown(caret->facade, key);
    }
  }
}

void KeyboardImpl::KeyUp(Key key) {
  if (key.physical > AnsiKey::Unknown && key.physical < AnsiKey::Count) {
    pressed_keys.reset((size_t)key.physical);
  }
  for (auto &caret : carets) {
    if (caret->owner) {
      caret->owner->KeyUp(caret->facade, key);
    }
  }
}

} // namespace automaton::gui
