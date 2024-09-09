#include "pointer.hh"

#include <cassert>

#include "action.hh"
#include "root.hh"
#include "time.hh"
#include "window.hh"

using namespace maf;

namespace automat::gui {

Pointer::Pointer(Window& window, Vec2 position)
    : window(window), pointer_position(position), button_down_position(), button_down_time() {
  AssertAutomatThread();
  window.pointers.push_back(this);
  assert(!window.keyboards.empty());
  if (window.keyboards.empty()) {
    keyboard = nullptr;
  } else {
    keyboard = window.keyboards.front();
    keyboard->pointer = this;
  }
}
Pointer::~Pointer() {
  AssertAutomatThread();
  if (!path.empty()) {
    path.back()->PointerLeave(*this, window.display);
  }
  if (keyboard) {
    keyboard->pointer = nullptr;
  }
  auto it = std::find(window.pointers.begin(), window.pointers.end(), this);
  if (it != window.pointers.end()) {
    window.pointers.erase(it);
  }
}
static void UpdatePath(Pointer& pointer) {
  Path& path = pointer.path;
  Path old_path = path;
  auto& display = pointer.window.display;

  path.clear();
  Vec2 point = pointer.pointer_position;

  Visitor dfs = [&](Span<Widget*> widgets) -> ControlFlow {
    for (auto w : widgets) {
      Vec2 transformed;
      if (!path.empty()) {
        transformed = path.back()->TransformToChild(*w, &display).mapPoint(point);
      } else {
        transformed = point;
      }

      auto shape = w->Shape(&display);
      path.push_back(w);
      std::swap(point, transformed);
      if (shape.contains(point.x, point.y)) {
        w->VisitChildren(dfs);
        return ControlFlow::Stop;
      } else if (w->TextureBounds() == std::nullopt) {
        if (w->VisitChildren(dfs) == ControlFlow::Stop) {
          return ControlFlow::Stop;
        }
      }
      std::swap(point, transformed);
      path.pop_back();
    }
    return ControlFlow::Continue;
  };

  Widget* window_arr[] = {&pointer.window};
  dfs(window_arr);

  for (Widget* old_w : old_path) {
    if (old_w == nullptr) {  // Ephemeral widgets (DragObjectAction) replace themselves with
                             // nullptr upon destruction.
      continue;
    }
    if (std::find(path.begin(), path.end(), old_w) == path.end()) {
      old_w->PointerLeave(pointer, display);
    }
  }
  for (Widget* new_w : path) {
    if (std::find(old_path.begin(), old_path.end(), new_w) == old_path.end()) {
      new_w->PointerOver(pointer, display);
    }
  }
}

void Pointer::Move(Vec2 position) {
  RunOnAutomatThread([=, this]() {
    Vec2 old_mouse_pos = pointer_position;
    pointer_position = position;
    if (button_down_time[kMouseMiddle] > time::kZero) {
      Vec2 delta = window.WindowToCanvas(position) - window.WindowToCanvas(old_mouse_pos);
      window.camera_x.Shift(-delta.x);
      window.camera_y.Shift(-delta.y);
      window.inertia = false;
    }
    if (action) {
      action->Update();
    }
    UpdatePath(*this);
    for (auto* cb : move_callbacks) {
      cb->PointerMove(*this, position);
    }
  });
}
void Pointer::Wheel(float delta) {
  RunOnAutomatThread([=, this]() {
    float factor = exp(delta / 4);
    window.zoom.target *= factor;
    // For small changes we skip the animation to increase responsiveness.
    if (fabs(delta) < 1.0) {
      Vec2 mouse_pre = window.WindowToCanvas(pointer_position);
      window.zoom.value *= factor;
      Vec2 mouse_post = window.WindowToCanvas(pointer_position);
      Vec2 mouse_delta = mouse_post - mouse_pre;
      window.camera_x.Shift(-mouse_delta.x);
      window.camera_y.Shift(-mouse_delta.y);
    }
    window.zoom.target = std::max(kMinZoom, window.zoom.target);
  });
}

void Pointer::ButtonDown(PointerButton btn) {
  if (btn == kButtonUnknown || btn >= kButtonCount) return;
  RunOnAutomatThread([=, this]() {
    button_down_position[btn] = pointer_position;
    button_down_time[btn] = time::SystemNow();
    UpdatePath(*this);

    if (action == nullptr && !path.empty()) {
      for (Widget* w : path) {
        action = w->CaptureButtonDownAction(*this, btn);
        if (action) {
          break;
        }
      }
      if (action == nullptr) {
        action = path.back()->ButtonDownAction(*this, btn);
      }
      if (action) {
        action->Begin();
        UpdatePath(*this);
      }
    }
  });
}

void Pointer::ButtonUp(PointerButton btn) {
  if (btn == kButtonUnknown || btn >= kButtonCount) return;
  RunOnAutomatThread([=, this]() {
    if (btn == kMouseLeft) {
      if (action) {
        action->End();
        action.reset();
      }
    }
    if (btn == kMouseMiddle) {
      time::Duration down_duration = time::SystemNow() - button_down_time[kMouseMiddle];
      Vec2 delta = pointer_position - button_down_position[kMouseMiddle];
      float delta_m = Length(delta);
      if ((down_duration < kClickTimeout) && (delta_m < kClickRadius)) {
        Vec2 canvas_pos = window.WindowToCanvas(pointer_position);
        window.camera_x.target = canvas_pos.x;
        window.camera_y.target = canvas_pos.y;
        window.zoom.target = 1;
        window.inertia = false;
      }
    }
    button_down_position[btn] = Vec2(0, 0);
    button_down_time[btn] = time::kZero;
  });
}
Pointer::IconType Pointer::Icon() const {
  if (icons.empty()) {
    return Pointer::kIconArrow;
  }
  return icons.back();
}
void Pointer::PushIcon(IconType icon) { icons.push_back(icon); }
void Pointer::PopIcon() { icons.pop_back(); }
Vec2 Pointer::PositionWithin(const Widget& widget) const {
  AssertAutomatThread();
  auto it = std::find(path.begin(), path.end(), &widget);
  auto end = it == path.end() ? path.end() : it + 1;
  Path sub_path(path.begin(), end);
  SkMatrix transform_down = TransformDown(sub_path, &window.display);
  return Vec2(transform_down.mapXY(pointer_position.x, pointer_position.y));
}
Vec2 Pointer::PositionWithinRootMachine() const {
  gui::Path root_machine_path;
  root_machine_path.push_back(path.front());
  root_machine_path.push_back(root_machine);
  SkMatrix transform_down = TransformDown(root_machine_path, &window.display);
  return Vec2(transform_down.mapXY(pointer_position.x, pointer_position.y));
}
animation::Display& Pointer::AnimationContext() const { return window.display; }

Str Pointer::ToStr() const {
  Str ret;
  for (Widget* w : path) {
    if (!ret.empty()) {
      ret += " -> ";
    }
    ret += w->Name();
    ret += PositionWithin(*w).ToStrMetric();
  }
  return ret;
}

}  // namespace automat::gui
