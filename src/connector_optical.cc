#include "connector_optical.hh"

#include <include/core/SkBlurTypes.h>
#include <include/core/SkMaskFilter.h>
#include <include/effects/SkGradientShader.h>
#include <include/effects/SkRuntimeEffect.h>

#include <cmath>

#include "arcline.hh"
#include "font.hh"
#include "log.hh"
#include "math.hh"
#include "svg.hh"

using namespace maf;

namespace automat::gui {

constexpr float kCasingWidth = 0.008;
constexpr float kCasingHeight = 0.008;
constexpr bool kDebugCable = false;

ArcLine RouteCable(Vec2 start, Vec2 cable_end) {
  ArcLine cable = ArcLine(start, M_PI * 1.5);
  Vec2 cable_middle = (start + cable_end) / 2;
  Vec2 delta = cable_middle - start;
  float distance = Length(delta);
  float turn_radius = std::max<float>(distance / 4, 0.01);

  auto horizontal_shift = ArcLine::TurnShift(delta.x * 2, turn_radius);
  float move_down = -delta.y - horizontal_shift.distance_forward / 2;
  if (move_down < 0) {
    auto vertical_shift =
        ArcLine::TurnShift(cable_end.x < start.x ? move_down * 2 : -move_down * 2, turn_radius);

    float move_side = (horizontal_shift.move_between_turns - vertical_shift.distance_forward) / 2;
    if (move_side < 0) {
      // If there is not enough space to route the cable in the middle, we will route it around the
      // objects.
      float x = start.x;
      float y = start.y;
      float dir;
      if (start.x > cable_end.x) {
        dir = 1;
      } else {
        dir = -1;
      }
      cable.TurnBy(dir * M_PI / 2, turn_radius);
      x += turn_radius * dir;
      y += turn_radius;
      cable.TurnBy(dir * M_PI / 2, turn_radius);
      x += turn_radius * dir;
      y -= turn_radius;
      float move_up = cable_end.y - y;
      float move_down = -move_up;
      if (move_up > 0) {
        cable.MoveBy(move_up);
      }
      y = cable_end.y;
      cable.TurnBy(dir * M_PI / 2, turn_radius);
      x -= turn_radius * dir;
      y -= turn_radius;
      cable.MoveBy(dir * (x - cable_end.x) - turn_radius);
      cable.TurnBy(dir * M_PI / 2, turn_radius);
      if (move_down > 0) {
        cable.MoveBy(move_down);
      }
    } else {
      cable.TurnBy(horizontal_shift.first_turn_angle, turn_radius);
      if (move_side > 0) {
        cable.MoveBy(move_side);
      }
      vertical_shift.Apply(cable);
      if (move_side > 0) {
        cable.MoveBy(move_side);
      }
      cable.TurnBy(-horizontal_shift.first_turn_angle, turn_radius);
    }
  } else {
    if (move_down > 0) {
      cable.MoveBy(move_down);
    }
    horizontal_shift.Apply(cable);
    if (move_down > 0) {
      cable.MoveBy(move_down);
    }
  }
  return cable;
}

void DrawOpticalConnector(DrawContext& ctx, OpticalConnectorState& state, Vec2 start, Vec2 end) {
  auto& canvas = ctx.canvas;
  auto& actx = ctx.animation_context;

  float casing_left = end.x - kCasingWidth / 2;
  float casing_right = end.x + kCasingWidth / 2;
  float casing_top = end.y + kCasingHeight;

  Vec2 cable_end = Vec2(end.x, casing_top);
  ArcLine cable = RouteCable(start, cable_end);

  auto cable_path = cable.ToPath(false);

  {  // Black metal casing
    SkPaint black_metal_paint;
    SkPoint pts[2] = {end + Vec2(-0.004, 0), end + Vec2(0.004, 0)};
    SkColor colors[5] = {0xff626262, 0xff000000, 0xff181818, 0xff0d0d0d, 0xff5e5e5e};
    float pos[5] = {0, 0.1, 0.5, 0.9, 1};
    sk_sp<SkShader> gradient =
        SkGradientShader::MakeLinear(pts, colors, pos, 5, SkTileMode::kClamp);
    black_metal_paint.setShader(gradient);
    SkRect black_metal_rect = SkRect::MakeLTRB(end.x - 0.004, end.y, end.x + 0.004, end.y + 0.008);
    canvas.drawRect(black_metal_rect, black_metal_paint);
  }

  {  // Steel insert
    SkRect steel_rect = SkRect::MakeLTRB(end.x - 0.003, end.y - 0.001, end.x + 0.003, end.y);

    // Fill with black - this will only stay around borders
    SkPaint black;
    black.setColor(0xff000000);
    canvas.drawRect(steel_rect, black);

    // Fill with steel-like gradient
    SkPaint steel_paint;
    SkPoint pts[2] = {end + Vec2(-0.003, 0), end + Vec2(0.003, 0)};
    SkColor colors[2] = {0xffe6e6e6, 0xff949494};
    sk_sp<SkShader> gradient =
        SkGradientShader::MakeLinear(pts, colors, nullptr, 2, SkTileMode::kClamp);
    steel_paint.setShader(gradient);
    steel_paint.setMaskFilter(
        SkMaskFilter::MakeBlur(SkBlurStyle::kInner_SkBlurStyle, 0.0001, true));
    steel_paint.setColor(0xff000000);
    canvas.drawRect(steel_rect, steel_paint);
  }

  {  // Rubber cable holder
    constexpr float kRubberWidth = 0.002;
    constexpr float kRubberHeight = 0.016;
    constexpr float kLowerCpOffset = kRubberHeight * 0.3;
    constexpr float kUpperCpOffset = kRubberHeight * 0.7;
    constexpr float kTopCpOffset = kRubberWidth * 0.2;

    float sleeve_left = end.x - kRubberWidth / 2;
    float sleeve_right = end.x + kRubberWidth / 2;
    float sleeve_top = casing_top + kRubberHeight;
    SkPath rubber_path;
    rubber_path.moveTo(casing_left, casing_top);
    rubber_path.cubicTo(casing_left, casing_top + kLowerCpOffset, sleeve_left,
                        sleeve_top - kUpperCpOffset, sleeve_left, sleeve_top);
    rubber_path.cubicTo(sleeve_left, sleeve_top + kTopCpOffset, sleeve_right,
                        sleeve_top + +kTopCpOffset, sleeve_right, sleeve_top);
    rubber_path.cubicTo(sleeve_right, sleeve_top - kUpperCpOffset, casing_right,
                        casing_top + kLowerCpOffset, casing_right, casing_top);
    rubber_path.close();

    SkPaint dark_flat;
    dark_flat.setAntiAlias(true);
    dark_flat.setColor(0xff151515);
    canvas.drawPath(rubber_path, dark_flat);

    SkPaint lighter_inside;
    lighter_inside.setAntiAlias(false);
    lighter_inside.setMaskFilter(
        SkMaskFilter::MakeBlur(SkBlurStyle::kInner_SkBlurStyle, 0.0010, true));
    lighter_inside.setColor(0xff2a2a2a);
    canvas.drawPath(rubber_path, lighter_inside);
  }

  {  // Icon on the metal casing
    SkPath path = PathFromSVG(kNextShape);
    path.offset(end.x, end.y + 0.004);
    SkPaint icon_paint;
    icon_paint.setColor(0xff808080);
    icon_paint.setAntiAlias(true);
    canvas.drawPath(path, icon_paint);
  }

  if constexpr (kDebugCable) {  // Draw the arcline
    SkPaint arcline_paint;
    arcline_paint.setColor(SK_ColorBLACK);
    arcline_paint.setAlphaf(.5);
    arcline_paint.setStrokeWidth(0.0005);
    arcline_paint.setStyle(SkPaint::kStroke_Style);
    arcline_paint.setAntiAlias(true);
    canvas.drawPath(cable_path, arcline_paint);
  }

  auto it = ArcLine::Iterator(cable);
  float cable_length = it.AdvanceToEnd();

  SkPaint cross_paint;
  cross_paint.setColor(0xffff8800);
  cross_paint.setAntiAlias(true);
  cross_paint.setStrokeWidth(0.0005);
  cross_paint.setStyle(SkPaint::kStroke_Style);

  auto& dispenser = start;
  float dt = ctx.animation_context.timer.d;
  dt = std::clamp<float>(dt, 0.001, 0.1);
  auto& chain = state.sections;
  using ChainLink = OpticalConnectorState::CableSegment;
  constexpr float kStep = 0.005;
  constexpr float kCrossSize = 0.0005;
  if (chain.empty()) {
    chain.emplace_back();
    chain.emplace_back();
  }
  chain.front().pos = cable_end;
  chain.back().pos = start;

  for (auto& link : chain) {
    link.acc = Vec2(0, 0);
  }

  Vec<Vec2> anchors;
  {  // Fill anchors
    anchors.push_back(cable_end);
    for (float cable_pos = kStep; cable_pos + kStep < cable_length; cable_pos += kStep) {
      it.Advance(-kStep);
      Vec2 anchor = it.Position();
      anchors.push_back(anchor);
    }
    anchors.push_back(start);
  }

  if constexpr (kDebugCable) {  // Orange crosses
    for (auto& anchor : anchors) {
      SkPath cross;
      cross.moveTo(anchor.x - kCrossSize, anchor.y - kCrossSize);
      cross.lineTo(anchor.x + kCrossSize, anchor.y + kCrossSize);
      cross.moveTo(anchor.x + kCrossSize, anchor.y - kCrossSize);
      cross.lineTo(anchor.x - kCrossSize, anchor.y + kCrossSize);
      canvas.drawPath(cross, cross_paint);
    }
    canvas.drawCircle(start.x, start.y, kCrossSize, cross_paint);
  }

  for (int i = 1; i < anchors.size() && i < chain.size(); i++) {  // Fill distances
    float distance_target = Length(anchors[i] - anchors[i - 1]);
    chain[i - 1].distance = distance_target;
    // Smooth variant (leaving this for later)
    // chain[i-1].distance += (distance_target - chain[i-1].distance) * expf(-dt * 10);
  }
  // Dispenser pulling the chain in
  if (chain.size() > anchors.size()) {
    // chain[chain.size() - 2].distance = Length(dispenser - chain[chain.size() - 2].pos);
    state.dispenser_v += 1e0 * dt;
    state.dispenser_v *= expf(-1 * dt);  // Limit the maximum speed
    if constexpr (kDebugCable) {
      canvas.drawLine(dispenser, dispenser + Vec2(0, state.dispenser_v / 10), cross_paint);
    }
    float retract = state.dispenser_v * dt;
    // Shorten the final link by pulling it towards the dispenser
    Vec2 prev = dispenser;
    float total_dist = 0;
    int i = chain.size() - 2;
    for (; i >= 0; --i) {
      total_dist += chain[i].distance;
      if (total_dist > retract) {
        break;
      }
    }
    if (retract > total_dist) {
      retract = total_dist;
    }
    for (int j = chain.size() - 2; j > i; --j) {
      chain.EraseIndex(j);
    }
    float remaining = total_dist - retract;
    // Move chain[i] to |remaining| distance from dispenser
    chain[i].distance = remaining;
    if (i - 1 >= 0) {
      chain[i - 1].distance = Length(chain[i - 1].pos - chain[i].pos);
    }
  } else {
    state.dispenser_v = 0;
  }

  {  // Add a new link if the last one is too far from the dispenser
    auto delta = chain[chain.size() - 2].pos - dispenser;
    auto current_dist = Length(delta);
    auto desired_dist = chain[chain.size() - 2].distance;
    float min_distance = chain.size() < anchors.size() ? kStep : 2 * kStep;
    if (current_dist > min_distance &&
        current_dist > desired_dist * 1.1 + state.dispenser_v * dt * 2) {
      chain[chain.size() - 2].distance = kStep;
      auto new_it =
          chain.insert(chain.begin() + chain.size() - 1,
                       ChainLink{
                           .pos = chain[chain.size() - 2].pos - delta / current_dist * kStep,
                           .vel = Vec2(0, 0),
                           .acc = Vec2(0, 0),
                           .distance = desired_dist - kStep,
                       });
    }
  }

  if (true) {  // Move chain links towards anchors (more at the end of the cable)
    for (int i = 1; i < anchors.size() - 1 && i < chain.size() - 1; i++) {
      Vec2 new_pos =
          chain[i].pos + (anchors[i] - chain[i].pos) * expf(-dt * 6000.0f * i / anchors.size());
      chain[i].vel += (new_pos - chain[i].pos) / dt;
      chain[i].pos = new_pos;
    }
  }

  if (true) {  // Apply forces towards anchors
    for (int i = 1; i < anchors.size() - 1 && i < chain.size() - 1; i++) {
      chain[i].acc += (anchors[i] - chain[i].pos) * 3e2;
    }
  }

  Vec<float> dir;
  dir.resize(chain.size());
  dir[0] = M_PI / 2;
  for (int i = 1; i < chain.size() - 1; i++) {
    auto& prev = chain[i - 1];
    auto& next = chain[i + 1];
    dir[i] = atan(next.pos - prev.pos);
  }
  dir[chain.size() - 1] = M_PI / 2;

  SkPaint stiffness_paint;
  stiffness_paint.setColor(0x80ff0000);
  stiffness_paint.setAntiAlias(true);
  stiffness_paint.setStyle(SkPaint::kFill_Style);
  if (true) {  // Apply stiffness forces
    const float angle_limit = M_PI / 8;
    const float stiffness = 1e3;
    for (int i = 1; i < chain.size(); i++) {
      auto& c = chain[i];
      auto& prev = chain[i - 1];
      auto alpha = atan(c.pos - prev.pos);
      auto diff = alpha - dir[i - 1];
      if (diff > M_PI) diff -= M_PI * 2;
      if (diff < -M_PI) diff += M_PI * 2;
      if (fabsf(diff) > angle_limit) {
        auto limit = dir[i - 1] + (diff >= 0 ? angle_limit : -angle_limit);
        auto f = (prev.pos + Vec2::Polar(limit, prev.distance) - c.pos) * stiffness;
        c.acc += f;
        prev.acc -= f;

        if constexpr (kDebugCable) {
          Rect rect;
          rect.left = prev.pos.x - prev.distance;
          rect.right = prev.pos.x + prev.distance;
          rect.top = prev.pos.y + prev.distance;
          rect.bottom = prev.pos.y - prev.distance;
          float startAngle = limit / M_PI * 180;
          float sweepAngle = diff > 0 ? diff - angle_limit : diff + angle_limit;
          sweepAngle *= 180 / M_PI;

          canvas.drawArc(rect.sk, startAngle, sweepAngle, true, stiffness_paint);
        }
      }
    }
    for (int i = 1; i < chain.size() - 1; i++) {
      auto& c = chain[i];
      auto& next = chain[i + 1];
      auto alpha = atan(next.pos - c.pos);
      auto diff = alpha - dir[i + 1];
      if (diff > M_PI) diff -= M_PI * 2;
      if (diff < -M_PI) diff += M_PI * 2;
      if (fabsf(diff) > angle_limit) {
        auto limit = dir[i + 1] + (diff >= 0 ? angle_limit : -angle_limit);
        auto f = (next.pos - Vec2::Polar(limit, c.distance) - c.pos) * stiffness;
        c.acc += f;
        next.acc -= f;
        if constexpr (kDebugCable) {
          Rect rect;
          rect.left = next.pos.x - c.distance;
          rect.right = next.pos.x + c.distance;
          rect.top = next.pos.y + c.distance;
          rect.bottom = next.pos.y - c.distance;
          float startAngle = 180 + limit / M_PI * 180;
          float sweepAngle = diff > 0 ? diff - angle_limit : diff + angle_limit;
          sweepAngle *= 180 / M_PI;
          canvas.drawArc(rect.sk, startAngle, sweepAngle, true, stiffness_paint);
        }
      }
    }
  }

  for (int i = 1; i < chain.size() - 1; ++i) {
    chain[i].vel += chain[i].acc * dt;
  }

  {                      // Friction
    int friction_i = 1;  // Skip segment 0 (always attached to mouse)
    // Segments that have anchors have higher friction
    auto n_high_friction = std::min(chain.size() - 1, anchors.size());
    for (; friction_i < n_high_friction; ++friction_i) {
      chain[friction_i].vel *= expf(-20 * dt);
    }
    // Segments without anchors are more free to move
    for (; friction_i < chain.size() - 1; ++friction_i) {
      chain[friction_i].vel *= expf(-2 * dt);
    }
  }

  for (int i = 1; i < chain.size() - 1; ++i) {
    chain[i].pos += chain[i].vel * dt;
  }

  {  // Cable length constraint
    chain.front().pos = cable_end;
    chain.back().pos = dispenser;
    for (int i = 1; i < chain.size(); i++) {  // Cable length constraint
      if (i == chain.size() - 1 && chain.size() <= anchors.size()) {
        // This check prevents dispenser apparently shooting out more
        // cable than it should.
        continue;
      }

      auto& curr = chain[i];
      auto& prev = chain[i - 1];
      auto c = (curr.pos + prev.pos) / 2;
      auto d = curr.pos - prev.pos;
      auto dist = Length(d);
      auto target_dist = chain[i - 1].distance;
      auto new_curr = c + d / dist * target_dist / 2;
      auto new_prev = c - d / dist * target_dist / 2;
      curr.vel += (new_curr - curr.pos) / dt;
      prev.vel += (new_prev - prev.pos) / dt;
      curr.pos = new_curr;
      prev.pos = new_prev;
    }
    chain.front().pos = cable_end;
    chain.back().pos = dispenser;
    for (int i = chain.size() - 1; i >= 1; --i) {
      if (i == chain.size() - 1 && chain.size() <= anchors.size()) {
        // This check prevents dispenser apparently shooting out more
        // cable than it should.
        continue;
      }

      auto& curr = chain[i];
      auto& prev = chain[i - 1];
      auto c = (curr.pos + prev.pos) / 2;
      auto d = curr.pos - prev.pos;
      auto dist = Length(d);
      auto target_dist = chain[i - 1].distance;
      auto new_curr = c + d / dist * target_dist / 2;
      auto new_prev = c - d / dist * target_dist / 2;
      curr.vel += (new_curr - curr.pos) / dt;
      prev.vel += (new_prev - prev.pos) / dt;
      curr.pos = new_curr;
      prev.pos = new_prev;
    }
    chain.front().pos = cable_end;
    chain.back().pos = dispenser;
  }

  auto& font = GetFont();

  if constexpr (kDebugCable) {  // Draw the chain as a series of straight lines
    SkPaint chain_paint;
    chain_paint.setColor(0xff0088ff);
    chain_paint.setAntiAlias(true);
    chain_paint.setStrokeWidth(0.00025);
    chain_paint.setStyle(SkPaint::kStroke_Style);
    for (int i = 0; i < chain.size(); ++i) {
      Vec2 line_offset = Vec2::Polar(dir[i], kStep / 4);
      canvas.drawLine(chain[i].pos - line_offset, chain[i].pos + line_offset, chain_paint);
      canvas.save();
      Str i_str = ::ToStr(i);
      canvas.translate(chain[i].pos.x, chain[i].pos.y);
      font.DrawText(canvas, i_str, SkPaint());
      canvas.restore();
    }
  }

  if (true) {  // Draw the chain as a bezier curve

    SkPaint cable_paint;
    cable_paint.setStyle(SkPaint::kStroke_Style);
    cable_paint.setStrokeWidth(0.002);
    cable_paint.setAntiAlias(true);
    cable_paint.setColor(0xff111111);

    SkPath p;
    p.moveTo(chain[0].pos);
    for (int i = 1; i < chain.size(); i++) {
      Vec2 p1 = chain[i - 1].pos + Vec2::Polar(dir[i - 1], kStep / 3);
      Vec2 p2 = chain[i].pos - Vec2::Polar(dir[i], kStep / 3);
      p.cubicTo(p1, p2, chain[i].pos);
    }
    p.setIsVolatile(true);
    canvas.drawPath(p, cable_paint);
    SkPaint cable_paint2;
    cable_paint2.setStyle(SkPaint::kStroke_Style);
    cable_paint2.setStrokeWidth(0.002);
    cable_paint2.setAntiAlias(true);
    cable_paint2.setColor(0xff444444);
    cable_paint2.setMaskFilter(
        SkMaskFilter::MakeBlur(SkBlurStyle::kInner_SkBlurStyle, 0.0005, true));
    canvas.drawPath(p, cable_paint2);
  }

  // TODO: Draw the cable plug below the object (it should be the main starting point for dragging
  // the cable)
  // TODO: Keep the cable on screen after its released
  // TODO: Once the cable stabilizes, draw it as a simple ArcLine
  // TODO: Add some noise to the cable texture
  // TODO: Draw the cable texture (shader?)
  // TODO: Blinking fiber at the end of the cable
}

/*
// This function has some nice code for drawing connections between rounded rectangles.
// Keeping this for potential usage in the future
void DrawConnection(SkCanvas& canvas, const SkPath& from_shape, const SkPath& to_shape) {
  SkColor color = 0xff6e4521;
  SkPaint line_paint;
  line_paint.setAntiAlias(true);
  line_paint.setStyle(SkPaint::kStroke_Style);
  line_paint.setStrokeWidth(0.0005);
  line_paint.setColor(color);
  SkPaint arrow_paint;
  arrow_paint.setAntiAlias(true);
  arrow_paint.setStyle(SkPaint::kFill_Style);
  arrow_paint.setColor(color);
  SkRRect from_rrect, to_rrect;
  bool from_is_rrect = from_shape.isRRect(&from_rrect);
  bool to_is_rrect = to_shape.isRRect(&to_rrect);

  // Find an area where the start of a connection can freely move.
  SkRect from_inner;
  if (from_is_rrect) {
    SkVector radii = from_rrect.getSimpleRadii();
    from_inner = from_rrect.rect().makeInset(radii.x(), radii.y());
  } else {
    Vec2 from_center = from_shape.getBounds().center();
    from_inner = SkRect::MakeXYWH(from_center.x, from_center.y, 0, 0);
  }
  // Find an area where the end of a connection can freely move.
  SkRect to_inner;
  if (to_is_rrect) {
    SkVector radii = to_rrect.getSimpleRadii();
    to_inner = to_rrect.rect().makeInset(radii.x(), radii.y());
  } else {
    Vec2 to_center = to_shape.getBounds().center();
    to_inner = SkRect::MakeXYWH(to_center.x, to_center.y, 0, 0);
  }
  to_inner.sort();
  from_inner.sort();

  Vec2 from, to;
  // Set the vertical positions of the connection endpoints.
  float left = std::max(from_inner.left(), to_inner.left());
  float right = std::min(from_inner.right(), to_inner.right());
  if (left <= right) {
    from.x = to.x = (left + right) / 2;
  } else if (from_inner.right() < to_inner.left()) {
    from.x = from_inner.right();
    to.x = to_inner.left();
  } else {
    from.x = from_inner.left();
    to.x = to_inner.right();
  }
  // Set the horizontal positions of the connection endpoints.
  float top = std::max(from_inner.top(), to_inner.top());
  float bottom = std::min(from_inner.bottom(), to_inner.bottom());
  if (bottom >= top) {
    from.y = to.y = (top + bottom) / 2;
  } else if (from_inner.bottom() < to_inner.top()) {
    from.y = from_inner.bottom();
    to.y = to_inner.top();
  } else {
    from.y = from_inner.top();
    to.y = to_inner.bottom();
  }
  // Find polar coordinates of the connection.
  SkVector delta = to - from;
  float degrees = 180 * std::atan2(delta.y(), delta.x()) / std::numbers::pi;
  float end = delta.length();
  float start = 0;
  if (from_is_rrect) {
    start = std::min(start + from_rrect.getSimpleRadii().fX, end);
  }
  if (to_is_rrect) {
    end = std::max(start, end - to_rrect.getSimpleRadii().fX);
  }
  float line_end = std::max(start, end + kConnectionArrowShape.getBounds().centerX());
  // Draw the connection.
  canvas.save();
  canvas.translate(from.x, from.y);
  canvas.rotate(degrees);
  if (start < line_end) {
    canvas.drawLine(start, 0, line_end, 0, line_paint);
  }
  canvas.translate(end, 0);
  canvas.drawPath(kConnectionArrowShape, arrow_paint);
  canvas.restore();
}
*/

}  // namespace automat::gui