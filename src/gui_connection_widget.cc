#include "gui_connection_widget.h"

#include <cassert>

#include <include/core/SkColor.h>
#include <include/core/SkRRect.h>

#include "base.h"
#include "font.h"
#include "gui_constants.h"
#include "log.h"

namespace automaton::gui {

struct ConnectionLabelWidget : Widget {
  ConnectionWidget *parent;
  std::string label;
  ConnectionLabelWidget(ConnectionWidget *parent, std::string_view label)
      : parent(parent), label(label) {
    this->label = "👆" + this->label + " ";
  }
  float Width() const { return GetFont().MeasureText(label); }
  float Height() const { return kLetterSize; }
  SkPath Shape() const override {
    float w = Width();
    float h = Height();
    return SkPath::Rect(SkRect::MakeWH(w, h));
  }
  void Draw(SkCanvas &canvas, animation::State &state) const override {
    SkPaint paint;
    DrawColored(canvas, state, paint);
  }

  void DrawColored(SkCanvas &canvas, animation::State &state,
                   const SkPaint &paint) const override {
    auto &font = GetFont();
    canvas.translate(-Width() / 2, -Height() / 2);
    font.DrawText(canvas, label, paint);
    canvas.translate(Width() / 2, Height() / 2);
  }
};

ConnectionWidget::ConnectionWidget(Location *from, std::string_view label)
    : Button(from->ParentWidget(),
             std::make_unique<ConnectionLabelWidget>(this, label)),
      from(from), label(label) {}

Widget *ConnectionWidget::ParentWidget() const { return from; }

void DrawConnection(SkCanvas &canvas, const SkPath &from_path,
                    const SkPath &to_path) {
  SkPaint paint;
  paint.setAntiAlias(true);
  paint.setStyle(SkPaint::kStroke_Style);
  paint.setStrokeWidth(0.0005);
  paint.setColor(SK_ColorBLACK);
  SkRRect from_rrect, to_rrect;
  bool from_is_rrect = from_path.isRRect(&from_rrect);
  bool to_is_rrect = to_path.isRRect(&to_rrect);

  // Find an area where the start of a connection can freely move.
  SkRect from_inner;
  if (from_is_rrect) {
    SkVector radii = from_rrect.getSimpleRadii();
    from_inner = from_rrect.rect().makeInset(radii.x(), radii.y());
  } else {
    SkPoint from_center = from_path.getBounds().center();
    from_inner = SkRect::MakeXYWH(from_center.x(), from_center.y(), 0, 0);
  }
  // Find an area where the end of a connection can freely move.
  SkRect to_inner;
  if (to_is_rrect) {
    SkVector radii = to_rrect.getSimpleRadii();
    to_inner = to_rrect.rect().makeInset(radii.x(), radii.y());
  } else {
    SkPoint to_center = to_path.getBounds().center();
    to_inner = SkRect::MakeXYWH(to_center.x(), to_center.y(), 0, 0);
  }

  SkPoint from, to;
  // Set the vertical positions of the connection endpoints.
  float left = std::max(from_inner.left(), to_inner.left());
  float right = std::min(from_inner.right(), to_inner.right());
  if (left <= right) {
    from.fX = to.fX = (left + right) / 2;
  } else if (from_inner.right() < to_inner.left()) {
    from.fX = from_inner.right();
    to.fX = to_inner.left();
  } else {
    from.fX = from_inner.left();
    to.fX = to_inner.right();
  }
  // Set the horizontal positions of the connection endpoints.
  float top = std::max(from_inner.top(), to_inner.top());
  float bottom = std::min(from_inner.bottom(), to_inner.bottom());
  if (top <= bottom) {
    from.fY = to.fY = (top + bottom) / 2;
  } else if (from_inner.bottom() < to_inner.top()) {
    from.fY = from_inner.bottom();
    to.fY = to_inner.top();
  } else {
    from.fY = from_inner.top();
    to.fY = to_inner.bottom();
  }
  // Draw the connection.
  canvas.drawLine(from.x(), from.y(), to.x(), to.y(), paint);
}

void ConnectionWidget::Draw(SkCanvas &canvas, animation::State &state) const {
  SkPath my_shape = Shape();

  auto [a, b] = from->outgoing.equal_range(label);
  for (auto it = a; it != b; ++it) {
    Connection &c = *it->second;
    Location &to = c.to;
    SkPath to_shape = to.Shape();
    SkMatrix m = to.ParentWidget()->TransformFromChild(&to, &state);
    m.postConcat(from->ParentWidget()->TransformToChild(this, &state));
    to_shape.transform(m);
    DrawConnection(canvas, my_shape, to_shape);
  }
  Button::Draw(canvas, state);
}

std::unique_ptr<Action> ConnectionWidget::ButtonDownAction(Pointer &,
                                                           PointerButton) {
  if (drag_action != nullptr) {
    return nullptr;
  }
  return std::make_unique<DragConnectionAction>(this);
}

vec2 ConnectionWidget::Center() const {
  SkRect b = Shape().getBounds();
  return Vec2(b.centerX(), b.centerY());
}

DragConnectionAction::DragConnectionAction(ConnectionWidget *widget)
    : widget(widget) {
  assert(widget->drag_action == nullptr);
  widget->drag_action = this;
}

DragConnectionAction::~DragConnectionAction() {
  assert(widget->drag_action == this);
  widget->drag_action = nullptr;
}

void DragConnectionAction::Begin(gui::Pointer &pointer) {
  current_position = pointer.PositionWithin(*widget->from->ParentWidget());
}

void DragConnectionAction::Update(gui::Pointer &pointer) {
  current_position = pointer.PositionWithin(*widget->from->ParentWidget());
}

void DragConnectionAction::End() {
  Location *from = widget->from;
  Machine *m = from->ParentAs<Machine>();
  Location *to = m->LocationAtPoint(current_position);
  if (to != nullptr) {
    from->ConnectTo(*to, widget->label);
  }
}

void DragConnectionAction::Draw(SkCanvas &canvas,
                                animation::State &animation_state) {
  SkPath from_path = widget->Shape();
  SkMatrix from_transform = widget->from->ParentWidget()->TransformFromChild(
      widget, &animation_state);
  from_path.transform(from_transform);
  SkPath to_path = SkPath();
  to_path.moveTo(current_position.X, current_position.Y);
  DrawConnection(canvas, from_path, to_path);
}

} // namespace automaton::gui
