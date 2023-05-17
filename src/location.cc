#include "location.h"

#include <include/effects/SkGradientShader.h>

#include "base.h"
#include "color.h"
#include "drag_action.h"
#include "font.h"
#include "format.h"
#include "gui_constants.h"

namespace automaton {

constexpr float kFrameCornerRadius = 0.001;

Location::Location(Location *parent)
    : parent(parent), name_text_field(this, &name, 0.03), run_button(this),
      run_task(this) {}

void *Location::Nearby(std::function<void *(Location &)> callback) {
  if (auto parent_machine = ParentAs<Machine>()) {
    // TODO: sort by distance
    for (auto &other : parent_machine->locations) {
      if (auto ret = callback(*other)) {
        return ret;
      }
    }
  }
  return nullptr;
}

bool Location::HasError() {
  if (error != nullptr)
    return true;
  if (auto machine = ThisAs<Machine>()) {
    if (!machine->children_with_errors.empty())
      return true;
  }
  return false;
}

Error *Location::GetError() {
  if (error != nullptr)
    return error.get();
  if (auto machine = ThisAs<Machine>()) {
    if (!machine->children_with_errors.empty())
      return (*machine->children_with_errors.begin())->GetError();
  }
  return nullptr;
}

void Location::ClearError() {
  if (error == nullptr) {
    return;
  }
  error.reset();
  if (auto machine = ParentAs<Machine>()) {
    machine->ClearChildError(*this);
  }
}

Object *Location::Follow() {
  if (Pointer *ptr = object->AsPointer()) {
    return ptr->Follow(*this);
  }
  return object.get();
}

void Location::Put(unique_ptr<Object> obj) {
  if (object == nullptr) {
    object = std::move(obj);
    return;
  }
  if (Pointer *ptr = object->AsPointer()) {
    ptr->Put(*this, std::move(obj));
  } else {
    object = std::move(obj);
  }
}

unique_ptr<Object> Location::Take() {
  if (Pointer *ptr = object->AsPointer()) {
    return ptr->Take(*this);
  }
  return std::move(object);
}

Connection *Location::ConnectTo(Location &other, string_view label,
                                Connection::PointerBehavior pointer_behavior) {
  object->Args([&](Argument &arg) {
    if (arg.name == label &&
        arg.precondition >= Argument::kRequiresConcreteType) {
      std::string error;
      arg.CheckRequirements(*this, &other, other.object.get(), error);
      if (error.empty()) {
        pointer_behavior = Connection::kTerminateHere;
      }
    }
  });
  Connection *c = new Connection(*this, other, pointer_behavior);
  outgoing.emplace(label, c);
  other.incoming.emplace(label, c);
  object->ConnectionAdded(*this, label, *c);
  return c;
}

void Location::ScheduleRun() { run_task.Schedule(); }

void Location::ScheduleLocalUpdate(Location &updated) {
  (new UpdateTask(this, &updated))->Schedule();
}

void Location::ScheduleErrored(Location &errored) {
  (new ErroredTask(this, &errored))->Schedule();
}

SkPath Location::Shape() const {
  SkRect object_bounds;
  if (object) {
    object_bounds = object->Shape().getBounds();
  } else {
    object_bounds = SkRect::MakeEmpty();
  }
  float outset = 0.001 - kBorderWidth / 2;
  SkRect bounds = object_bounds.makeOutset(outset, outset);
  if (bounds.width() < name_text_field.width + 2 * 0.001) {
    bounds.fRight = bounds.fLeft + name_text_field.width + 2 * 0.001;
  }
  bounds.fBottom += gui::kTextFieldHeight + 0.001;
  // expand the bounds to include the run button
  SkPath run_button_shape = run_button.Shape();
  bounds.fTop -= run_button_shape.getBounds().height() + 0.001;
  return SkPath::RRect(bounds, kFrameCornerRadius, kFrameCornerRadius);
}

gui::VisitResult Location::VisitImmediateChildren(gui::WidgetVisitor &visitor) {
  if (object) {
    auto result = visitor(*object, SkMatrix::I(), SkMatrix::I());
    if (result != gui::VisitResult::kContinue) {
      return result;
    }
  }
  SkPath my_shape = Shape();
  SkRect my_bounds = my_shape.getBounds();
  SkMatrix name_text_field_transform_down =
      SkMatrix::Translate(-my_bounds.left() - 0.001,
                          -my_bounds.bottom() + gui::kTextFieldHeight + 0.001);
  SkMatrix name_text_field_transform_up =
      SkMatrix::Translate(my_bounds.left() + 0.001,
                          my_bounds.bottom() - gui::kTextFieldHeight - 0.001);
  auto result = visitor(name_text_field, name_text_field_transform_down,
                        name_text_field_transform_up);
  if (result != gui::VisitResult::kContinue) {
    return result;
  }
  SkPath run_button_shape = run_button.Shape();
  SkRect run_bounds = run_button_shape.getBounds();
  SkMatrix run_button_transform_down =
      SkMatrix::Translate(-(my_bounds.centerX() - run_bounds.centerX()),
                          -(my_bounds.top() - run_bounds.fTop) - 0.001);
  SkMatrix run_button_transform_up =
      SkMatrix::Translate(my_bounds.centerX() - run_bounds.centerX(),
                          my_bounds.top() - run_bounds.fTop + 0.001);
  result =
      visitor(run_button, run_button_transform_down, run_button_transform_up);
  return result;
}

vec2 Location::AnimatedPosition(animation::State &animation_state) const {
  vec2 ret = position;
  if (drag_action) {
    ret.X += drag_action->round_x[animation_state];
    ret.Y += drag_action->round_y[animation_state];
  }
  return ret;
}

void Location::Draw(SkCanvas &canvas, animation::State &animation_state) const {
  SkPath my_shape = Shape();
  SkRect bounds = my_shape.getBounds();
  SkPaint frame_bg;
  SkColor frame_bg_colors[2] = {0xffcccccc, 0xffaaaaaa};
  SkPoint gradient_pts[2] = {{0, bounds.bottom()}, {0, bounds.top()}};
  sk_sp<SkShader> frame_bg_shader = SkGradientShader::MakeLinear(
      gradient_pts, frame_bg_colors, nullptr, 2, SkTileMode::kClamp);
  frame_bg.setShader(frame_bg_shader);
  canvas.drawPath(my_shape, frame_bg);

  SkPaint frame_border;
  SkColor frame_border_colors[2] = {
      color::AdjustLightness(frame_bg_colors[0], 5),
      color::AdjustLightness(frame_bg_colors[1], -5)};
  sk_sp<SkShader> frame_border_shader = SkGradientShader::MakeLinear(
      gradient_pts, frame_border_colors, nullptr, 2, SkTileMode::kClamp);
  frame_border.setShader(frame_border_shader);
  frame_border.setStyle(SkPaint::kStroke_Style);
  frame_border.setStrokeWidth(0.00025);
  canvas.drawRoundRect(bounds, kFrameCornerRadius, kFrameCornerRadius,
                       frame_border);

  auto DrawInset = [&](SkPath shape) {
    const SkRect &bounds = shape.getBounds();
    SkPaint paint;
    SkColor colors[2] = {color::AdjustLightness(frame_bg_colors[0], 5),
                         color::AdjustLightness(frame_bg_colors[1], -5)};
    SkPoint points[2] = {{0, bounds.top()}, {0, bounds.bottom()}};
    sk_sp<SkShader> shader = SkGradientShader::MakeLinear(
        points, colors, nullptr, 2, SkTileMode::kClamp);
    paint.setShader(shader);
    paint.setStyle(SkPaint::kStroke_Style);
    paint.setStrokeWidth(0.0005);
    canvas.drawPath(shape, paint);
  };

  // TODO: draw inset around every child
  if (object) {
    // Draw inset around object
    DrawInset(object->Shape());
  }

  DrawChildren(canvas, animation_state);

  // Draw debug text log below the Location
  float n_lines = 1;
  float offset_y = bounds.top();
  float offset_x = bounds.left();
  float line_height = gui::kLetterSize * 1.5;
  auto &font = gui::GetFont();

  if (error) {
    constexpr float b = 0.00025;
    SkPaint error_paint;
    error_paint.setColor(SK_ColorRED);
    error_paint.setStyle(SkPaint::kStroke_Style);
    error_paint.setStrokeWidth(2 * b);
    error_paint.setAntiAlias(true);
    canvas.drawPath(my_shape, error_paint);
    offset_x -= b;
    offset_y -= 3 * b;
    error_paint.setStyle(SkPaint::kFill_Style);
    canvas.translate(offset_x, offset_y - n_lines * line_height);
    font.DrawText(canvas, error->text, error_paint);
    canvas.translate(-offset_x, -offset_y + n_lines * line_height);
    n_lines += 1;
  }
}

std::unique_ptr<Action> Location::ButtonDownAction(gui::Pointer &p,
                                                   gui::PointerButton btn) {
  if (btn == gui::PointerButton::kMouseLeft) {
    auto a = std::make_unique<DragLocationAction>(this);
    a->contact_point = p.PositionWithin(*this);
    return a;
  }
  return nullptr;
}

void Location::SetNumber(double number) { SetText(f("%lf", number)); }

std::string Location::LoggableString() const {
  std::string_view object_name = object->Name();
  if (name.empty()) {
    if (object_name.empty()) {
      auto &o = *object;
      return typeid(o).name();
    } else {
      return std::string(object_name);
    }
  } else {
    return f("%*s \"%s\"", object_name.size(), object_name.data(),
             name.c_str());
  }
}

void Location::ReportMissing(std::string_view property) {
  auto error_message =
      f("Couldn't find \"%*s\". You can create a connection or rename "
        "one of the nearby objects to fix this.",
        property.size(), property.data());
  ReportError(error_message);
}

} // namespace automaton
