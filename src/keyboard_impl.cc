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

enum class CaretAnimAction { Keep, Delete };

static CaretAnimAction DrawCaret(SkCanvas &canvas, CaretAnimation &anim,
                                 CaretImpl *caret,
                                 animation::State &animation_state) {
  if (caret) {
    anim.last_blink = caret->last_blink;
    if (anim.shape.isInterpolatable(caret->shape)) {
      SkPath out;
      float weight = 1 - anim.delta_fraction.Tick(animation_state);
      anim.shape.interpolate(caret->shape, weight, &out);
      anim.shape = out;
    } else {
      anim.shape = caret->shape;
    }
  }
  SkPaint paint;
  paint.setColor(SK_ColorBLACK);
  paint.setAntiAlias(true);
  double now = (animation_state.timer.now - anim.last_blink).count();
  double seconds, subseconds;
  subseconds = modf(now, &seconds);
  if (subseconds < 0.5) {
    canvas.drawPath(anim.shape, paint);
  }
  if (caret == nullptr) {
    return CaretAnimAction::Delete;
  } else {
    return CaretAnimAction::Keep;
  }
}

void KeyboardImpl::Draw(SkCanvas &canvas,
                        animation::State &animation_state) const {
  // Iterate through each Caret & CaretAnimation, and draw them.
  // After a Caret has been removed, its CaretAnimation is kept around for some
  // time to animate it out.
  auto &anim_carets = anim[animation_state].carets;
  auto anim_it = anim_carets.begin();
  auto caret_it = carets.begin();
  while (anim_it != anim_carets.end() && caret_it != carets.end()) {
    if (anim_it->first < caret_it->get()) {
      // Caret was removed.
      auto a = DrawCaret(canvas, anim_it->second, nullptr, animation_state);
      if (a == CaretAnimAction::Delete) {
        anim_it = anim_carets.erase(anim_it);
      } else {
        ++anim_it;
      }
    } else if (anim_it->first > caret_it->get()) {
      // Caret was added.
      auto new_it =
          anim_carets.emplace(std::make_pair(caret_it->get(), CaretAnimation()))
              .first;
      DrawCaret(canvas, new_it->second, caret_it->get(), animation_state);
      ++caret_it;
    } else {
      DrawCaret(canvas, anim_it->second, caret_it->get(), animation_state);
      ++anim_it;
      ++caret_it;
    }
  }
  while (anim_it != anim_carets.end()) {
    // Caret at end was removed.
    auto a = DrawCaret(canvas, anim_it->second, nullptr, animation_state);
    if (a == CaretAnimAction::Delete) {
      anim_it = anim_carets.erase(anim_it);
    } else {
      ++anim_it;
    }
  }
  while (caret_it != carets.end()) {
    // Caret at end was added.
    auto new_it =
        anim_carets.emplace(std::make_pair(caret_it->get(), CaretAnimation()))
            .first;
    DrawCaret(canvas, new_it->second, caret_it->get(), animation_state);
    ++caret_it;
  }
}

void KeyboardImpl::KeyDown(Key key) {
  if (key.physical > AnsiKey::Unknown && key.physical < AnsiKey::Count) {
    pressed_keys.set((size_t)key.physical);
  }
  if (key.physical == AnsiKey::Escape) {
    for (auto &caret : carets) {
      caret->owner->ReleaseCaret(caret->facade);
    }
    carets.clear();
  } else {
    for (auto &caret : carets) {
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
