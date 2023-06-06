#include "gui_shape_widget.h"

#include "svg.h"

namespace automat::gui {

ShapeWidget::ShapeWidget(SkPath path) : path(path) {}

SkPath ShapeWidget::Shape() const { return path; }

void ShapeWidget::Draw(DrawContext& ctx) const { ctx.canvas.drawPath(path, paint); }

std::unique_ptr<Widget> MakeShapeWidget(const char* svg_path, SkColor fill_color) {
  SkPath path = PathFromSVG(svg_path);
  auto ret = std::make_unique<ShapeWidget>(path);
  ret->paint.setAntiAlias(true);
  ret->paint.setColor(fill_color);
  return ret;
}

}  // namespace automat::gui