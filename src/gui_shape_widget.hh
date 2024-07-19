#pragma once

#include <include/core/SkPaint.h>
#include <include/core/SkPath.h>

#include "widget.hh"

namespace automat::gui {

struct ShapeWidget : Widget, PaintMixin {
  SkPath path;

  ShapeWidget(SkPath path);
  SkPath Shape(animation::Display*) const override;
  void Draw(DrawContext&) const override;
};

std::unique_ptr<Widget> MakeShapeWidget(const char* svg_path, SkColor fill_color,
                                        const SkMatrix* transform = nullptr);

}  // namespace automat::gui