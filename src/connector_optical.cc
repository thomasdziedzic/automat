#include "connector_optical.hh"

#include <include/core/SkBlurTypes.h>
#include <include/core/SkCanvas.h>
#include <include/core/SkMaskFilter.h>
#include <include/core/SkMesh.h>
#include <include/effects/SkGradientShader.h>
#include <include/effects/SkRuntimeEffect.h>

#include <cmath>

#include "arcline.hh"
#include "font.hh"
#include "math.hh"
#include "svg.hh"

using namespace maf;

namespace automat::gui {

constexpr float kCasingWidth = 0.008;
constexpr float kCasingHeight = 0.008;
constexpr bool kDebugCable = false;
constexpr float kStep = 0.005;
constexpr float kCrossSize = 0.001;
constexpr float kCableWidth = 0.002;

ArcLine RouteCable(Vec2 start, Vec2 cable_end) {
  ArcLine cable = ArcLine(start, M_PI * 1.5);
  Vec2 delta = cable_end - start;
  float distance = Length(delta);
  float turn_radius = std::max<float>(distance / 8, 0.01);

  auto horizontal_shift = ArcLine::TurnShift(delta.x, turn_radius);
  float move_down = (-delta.y - horizontal_shift.distance_forward) / 2;
  if (move_down < 0) {
    // Increase the turn radius of the vertical move to allow ∞-type routing
    float vertical_turn_radius = std::max(turn_radius, horizontal_shift.move_between_turns * 0.5f);
    auto vertical_shift = ArcLine::TurnShift(cable_end.x < start.x ? move_down * 2 : -move_down * 2,
                                             vertical_turn_radius);

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

// This function walks along the given arcline (from the end to its start) and adds
// an anchor every kStep distance. It populates the `anchors` and `anchor_tangents` vectors.
static void PopulateAnchors(Vec<Vec2>& anchors, Vec<float>& anchor_dir, const ArcLine& arcline) {
  auto it = ArcLine::Iterator(arcline);
  Vec2 dispenser = it.Position();
  float cable_length = it.AdvanceToEnd();
  Vec2 tail = it.Position();

  anchors.push_back(tail);
  anchor_dir.push_back(M_PI / 2);
  for (float cable_pos = kStep; cable_pos < cable_length - kCableWidth / 2; cable_pos += kStep) {
    it.Advance(-kStep);
    anchors.push_back(it.Position());
    float dir = NormalizeAngle(it.Angle() + M_PI);
    anchor_dir.push_back(dir);
  }
  anchors.push_back(dispenser);
  anchor_dir.push_back(M_PI / 2);
}

// Simulate the dispenser pulling in the cable. This function may remove some of the cable segments
// but will always leave at least two - starting & ending points.
//
// Returns true if the dispenser is active and pulling the cable in.
//
// WARNING: this function will adjust the length of the final cable segment (the one closest to the
// dispenser). Don't change it or visual glitches will occur.
static bool SimulateDispenser(OpticalConnectorState& state, float dt, Size anchor_count) {
  bool pulling = anchor_count < state.sections.size();
  if (pulling) {
    state.dispenser_v += 5e-1 * dt;
    state.dispenser_v *= expf(-1 * dt);  // Limit the maximum speed
    float retract = state.dispenser_v * dt;
    // Shorten the final link by pulling it towards the dispenser
    float total_dist = 0;
    int i = state.sections.size() - 2;
    for (; i >= 0; --i) {
      total_dist += state.sections[i].distance;
      if (total_dist > retract) {
        break;
      }
    }
    if ((i < 0) || (retract > total_dist)) {
      i = 0;
      retract = total_dist;
    }
    for (int j = state.sections.size() - 2; j > i; --j) {
      state.sections.EraseIndex(j);
    }
    float remaining = total_dist - retract;
    // Move chain[i] to |remaining| distance from dispenser
    state.sections[i].distance = remaining;
  } else {
    state.dispenser_v = 0;
    do {  // Add a new link if the last one is too far from the dispenser
      auto delta = state.sections[state.sections.size() - 2].pos - state.sections.back().pos;
      auto current_dist = Length(delta);
      constexpr float kExtendThreshold = kStep + kCableWidth / 2;
      if (current_dist > kExtendThreshold) {
        state.sections[state.sections.size() - 2].distance = kStep;
        auto new_it = state.sections.insert(
            state.sections.begin() + state.sections.size() - 1,
            OpticalConnectorState::CableSection{
                .pos = state.sections[state.sections.size() - 2].pos - Vec2(0, kCableWidth / 2) -
                       delta / current_dist * kStep,
                .vel = Vec2(0, 0),
                .acc = Vec2(0, 0),
                .distance = current_dist - kStep,
            });
      } else if (state.sections.size() < anchor_count) {
        auto new_it =
            state.sections.insert(state.sections.begin() + state.sections.size() - 1,
                                  OpticalConnectorState::CableSection{
                                      .pos = state.sections.back().pos - Vec2(0, kCableWidth / 2),
                                      .vel = Vec2(0, 0),
                                      .acc = Vec2(0, 0),
                                      .distance = kCableWidth / 2,
                                  });
        break;
      } else {
        break;
      }
    } while (state.sections.size() < anchor_count);
  }

  return pulling;
}

void SimulateCablePhysics(float dt, OpticalConnectorState& state, Vec2 start, Optional<Vec2> end) {
  Optional<Vec2> cable_end;
  if (end) {
    cable_end = Vec2(end->x, end->y + kCasingHeight);
  }
  if (state.stabilized && Length(start - state.stabilized_start) < 0.0001) {
    if (cable_end.has_value() == state.stabilized_end.has_value() &&
        (!cable_end.has_value() || Length(*cable_end - *state.stabilized_end) < 0.0001)) {
      return;
    }
  }

  auto& dispenser = start;
  auto& chain = state.sections;
  if (cable_end) {
    chain.front().pos = *cable_end;
  }
  chain.back().pos = start;

  if (cable_end) {  // Create the arcline & pull the cable towards it
    state.arcline = RouteCable(start, *cable_end);
  } else {
    state.arcline.reset();
  }

  Vec<Vec2> anchors;
  Vec<float> true_anchor_dir;
  if (state.arcline) {
    PopulateAnchors(anchors, true_anchor_dir, *state.arcline);
  }

  for (auto& link : chain) {
    link.acc = Vec2(0, 0);
  }

  // Dispenser pulling the chain in. The chain is pulled in when there are fewer anchors than cable
  // segments.
  bool dispenser_active = SimulateDispenser(state, dt, anchors.size());

  float numerical_anchor_dir[anchors.size()];

  int anchor_i[chain.size()];  // Index of the anchor that the chain link is attached to

  // Match cable sections to anchors.
  // Sometimes there is more sections than anchors and sometimes there are more anchors than
  // sections. A cable section that doesn't have a matching anchor will be set to -1.
  for (int i = 0; i < chain.size(); ++i) {
    if (i == chain.size() - 1) {
      anchor_i[i] = anchors.size() - 1;  // This also handles the case when there are no anchors
    } else if (i >= ((int)anchors.size()) - 1) {
      anchor_i[i] = -1;
    } else {
      anchor_i[i] = i;
    }
  }

  // Move chain links towards anchors (more at the end of the cable)
  for (int i = 0; i < chain.size(); i++) {
    int ai = anchor_i[i];
    if (ai == -1) {
      continue;
    }
    // LERP the cable section towards its anchor point. More at the end of the cable.
    float time_factor = 1 - expf(-dt * 60.0f);                 // approach 1 as dt -> infinity
    float offset_factor = std::max<float>(0, 1 - ai / 10.0f);  // 1 near the plug and falling to 0
    Vec2 new_pos = chain[i].pos + (anchors[ai] - chain[i].pos) * time_factor * offset_factor;
    chain[i].vel += (new_pos - chain[i].pos) / dt;
    chain[i].pos = new_pos;

    // Also apply a force towards the anchor. This is unrelated to LERP-ing above.
    chain[i].acc += (anchors[ai] - chain[i].pos) * 3e2;
  }

  constexpr float kDistanceEpsilon = 1e-6;
  if (Length(chain[chain.size() - 1].pos - chain[chain.size() - 2].pos) > kDistanceEpsilon &&
      chain[chain.size() - 2].distance > kDistanceEpsilon) {
    chain[chain.size() - 1].dir = atan(chain[chain.size() - 1].pos - chain[chain.size() - 2].pos);
  } else {
    chain[chain.size() - 1].dir = M_PI / 2;
  }
  if (Length(chain[1].pos - chain[0].pos) > kDistanceEpsilon &&
      chain[0].distance > kDistanceEpsilon) {
    chain[0].dir = atan(chain[1].pos - chain[0].pos);
  } else {
    chain[0].dir = M_PI / 2;
  }
  for (int i = 1; i < chain.size() - 1; i++) {
    chain[i].dir = atan(chain[i + 1].pos - chain[i - 1].pos);
  }

  // Copy over the alignment of the anchors to the chain links.
  float total_anchor_distance = 0;
  for (int i = 0; i < chain.size(); ++i) {
    int ai = anchor_i[i];
    int prev_ai = i > 0 ? anchor_i[i - 1] : -1;
    int next_ai = i < chain.size() - 1 ? anchor_i[i + 1] : -1;

    if (ai != -1 && prev_ai != -1 && next_ai != -1) {
      numerical_anchor_dir[ai] = atan(anchors[next_ai] - anchors[prev_ai]);
    } else if (ai != -1 && prev_ai != -1) {
      numerical_anchor_dir[ai] = atan(anchors[ai] - anchors[prev_ai]);
    } else if (ai != -1 && next_ai != -1) {
      numerical_anchor_dir[ai] = atan(anchors[next_ai] - anchors[ai]);
    } else if (ai != -1) {
      numerical_anchor_dir[ai] = M_PI / 2;
    }
    float true_dir_offset;
    if (ai != -1) {
      float distance_mm = Length(anchors[ai] - chain[i].pos) * 1000;
      total_anchor_distance += distance_mm;
      true_dir_offset = NormalizeAngle(true_anchor_dir[ai] - chain[i].dir);
      true_dir_offset = std::lerp(true_dir_offset, 0, std::min<float>(distance_mm, 1));
      chain[i].true_dir_offset = true_dir_offset;
    } else {
      chain[i].true_dir_offset *= expf(-dt * 10);
    }
    if (ai != -1 && prev_ai != -1) {
      chain[i].prev_dir_delta = atan(anchors[prev_ai] - anchors[ai]) - numerical_anchor_dir[ai];
    } else {
      chain[i].prev_dir_delta = M_PI;
    }
    if (ai != -1 && next_ai != -1) {
      chain[i].next_dir_delta = atan(anchors[next_ai] - anchors[ai]) - numerical_anchor_dir[ai];
    } else {
      chain[i].next_dir_delta = 0;
    }
    if (dispenser_active && i == chain.size() - 2) {
      // pass
    } else {
      if (ai != -1 && next_ai != -1) {
        chain[i].distance = Length(anchors[next_ai] - anchors[ai]);
      } else {
        // Smoothly fade the distance to kStep
        float alpha = expf(-dt * 1);
        chain[i].distance = chain[i].distance * alpha + kStep * (1 - alpha);
      }
    }
  }
  if (cable_end) {
    chain.front().true_dir_offset = NormalizeAngle(M_PI / 2 - chain.front().dir);
  }
  chain.back().true_dir_offset = NormalizeAngle(M_PI / 2 - chain.back().dir);

  if (anchors.empty()) {
    state.stabilized = chain.size() == 2 && Length(chain[0].pos - chain[1].pos) < 0.0001;
  } else {
    float average_anchor_distance = total_anchor_distance / anchors.size();
    state.stabilized = average_anchor_distance < 0.1 && chain.size() == anchors.size();
  }
  if (state.stabilized) {
    state.stabilized_start = start;
    if (cable_end) {
      state.stabilized_end = *cable_end;
    } else {
      state.stabilized_end.reset();
      chain.front().true_dir_offset = 0;
    }
  }

  for (int i = 0; i < chain.size() - 1; ++i) {
    chain[i].vel += chain[i].acc * dt;
  }

  {                      // Friction
    int friction_i = 0;  // Skip segment 0 (always attached to mouse)
    // Segments that have anchors have higher friction
    auto n_high_friction = std::min<int>(chain.size() - 1, anchors.size());
    for (; friction_i < n_high_friction; ++friction_i) {
      chain[friction_i].vel *= expf(-20 * dt);
    }
    // Segments without anchors are more free to move
    for (; friction_i < chain.size(); ++friction_i) {
      chain[friction_i].vel *= expf(-2 * dt);
    }
    if (cable_end.has_value()) {
      chain.front().vel = Vec2(0, 0);
    }
  }

  for (int i = 0; i < chain.size() - 1; ++i) {
    chain[i].pos += chain[i].vel * dt;
  }

  if (true) {  // Inverse kinematics solver
    bool distance_only = anchors.empty();
    for (int iter = 0; iter < 6; ++iter) {
      if (cable_end) {
        chain.front().pos = *cable_end;
      }
      chain.back().pos = dispenser;
      chain.back().distance = kStep;

      OpticalConnectorState::CableSection cN;
      cN.pos = chain.back().pos + Vec2::Polar(chain.back().dir, kStep);

      int start = 1;
      int end = chain.size();
      int inc = 1;
      if (iter % 2) {
        start = chain.size() - 1;
        end = 0;
        inc = -1;
      }

      for (int i = start; i != end; i += inc) {
        auto& a = chain[i - 1];
        auto& b = chain[i];
        auto& c = i == chain.size() - 1 ? cN : chain[i + 1];

        Vec2 middle_pre_fix = (a.pos + b.pos + c.pos) / 3;

        float a_dir_offset = b.prev_dir_delta;
        float c_dir_offset = b.next_dir_delta;
        Vec2 a_target = b.pos + Vec2::Polar(chain[i].dir + a_dir_offset, a.distance);
        Vec2 c_target = b.pos + Vec2::Polar(chain[i].dir + c_dir_offset, b.distance);

        if (distance_only) {
          Vec2 ab = a.pos - b.pos;
          float l_ab = std::max<float>(1e-9, Length(ab));
          a_target = b.pos + ab / l_ab * a.distance;
          Vec2 bc = c.pos - b.pos;
          float l_bc = std::max<float>(1e-9, Length(bc));
          c_target = b.pos + bc / l_bc * b.distance;
        }

        float alpha = 0.4;
        Vec2 a_new = a.pos + (a_target - a.pos) * alpha;
        Vec2 c_new = c.pos + (c_target - c.pos) * alpha;

        Vec2 middle_post_fix = (a_new + b.pos + c_new) / 3;

        Vec2 correction = middle_pre_fix - middle_post_fix;

        a_new += correction;
        Vec2 b_new = b.pos + correction;
        c_new += correction;

        a.vel += (a_new - a.pos) / dt;
        b.vel += (b_new - b.pos) / dt;
        c.vel += (c_new - c.pos) / dt;
        a.pos = a_new;
        b.pos = b_new;
        c.pos = c_new;
      }
      if (cable_end) {
        chain.front().pos = *cable_end;
      }
      chain.back().pos = dispenser;
    }
  }
}

Vec2 OpticalConnectorState::PlugTopCenter() const { return sections.front().pos; }

Vec2 OpticalConnectorState::PlugBottomCenter() const {
  return sections.front().pos - Vec2::Polar(sections.front().dir, kCasingHeight);
}

void DrawOpticalConnector(DrawContext& ctx, OpticalConnectorState& state) {
  auto& canvas = ctx.canvas;
  auto& actx = ctx.animation_context;

  // Find the index of the last section that is part of the rubber sleeve
  int rubber_sleeve_tail_i = std::min<int>(3, (int)state.sections.size() - 1);
  bool rubber_touching_dispenser = rubber_sleeve_tail_i == state.sections.size() - 1;

  // Draw the cable as a bezier curve
  if (!rubber_touching_dispenser) {
    SkPaint cable_paint;
    cable_paint.setStyle(SkPaint::kStroke_Style);
    cable_paint.setStrokeWidth(kCableWidth);
    cable_paint.setAntiAlias(true);
    cable_paint.setColor(0xff111111);

    SkPath p;
    if (state.stabilized) {
      if (state.arcline) {
        p = state.arcline->ToPath(false);
      }
    } else {
      p.moveTo(state.sections[0].pos);
      for (int i = 1; i < state.sections.size(); i++) {
        Vec2 p1 = state.sections[i - 1].pos +
                  Vec2::Polar(state.sections[i - 1].dir + state.sections[i - 1].true_dir_offset,
                              state.sections[i - 1].distance / 3);
        Vec2 p2 = state.sections[i].pos -
                  Vec2::Polar(state.sections[i].dir + state.sections[i].true_dir_offset,
                              state.sections[i].distance / 3);
        p.cubicTo(p1, p2, state.sections[i].pos);
      }
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

  canvas.save();
  Vec2 cable_end = state.PlugTopCenter();
  SkMatrix transform = SkMatrix::Translate(cable_end);
  float connector_dir = state.arcline
                            ? M_PI / 2
                            : state.sections.front().dir + state.sections.front().true_dir_offset;
  transform.preRotate(connector_dir * 180 / M_PI - 90);
  transform.preTranslate(0, -kCasingHeight);
  canvas.concat(transform);

  float casing_left = -kCasingWidth / 2;
  float casing_right = kCasingWidth / 2;
  float casing_top = kCasingHeight;

  {  // Black metal casing
    // TODO: cache mesh!
    SkMeshSpecification::Attribute attributes[2] = {
        {
            .type = SkMeshSpecification::Attribute::Type::kFloat2,
            .offset = 0,
            .name = SkString("position"),
        },
        {
            .type = SkMeshSpecification::Attribute::Type::kFloat2,
            .offset = 8,
            .name = SkString("uv"),
        }};
    SkMeshSpecification::Varying varyings[3] = {
        {
            .type = SkMeshSpecification::Varying::Type::kFloat2,
            .name = SkString("position"),
        },
        {
            .type = SkMeshSpecification::Varying::Type::kFloat2,
            .name = SkString("uv"),
        },
        {
            .type = SkMeshSpecification::Varying::Type::kFloat,
            .name = SkString("light"),
        }};
    auto vs = SkString(R"(
      Varyings main(const Attributes attrs) {
        Varyings v;
        v.position = attrs.position;
        v.uv = attrs.uv;
        v.light = attrs.uv.y;
        return v;
      }
    )");
    auto fs = SkString(R"(
      const float kCaseSideRadius = 0.12;
      // NOTE: fix this once Skia supports array initializers here
      const vec3 kCaseBorderDarkColor = vec3(5) / 255; // subtle dark contour
      const vec3 kCaseBorderReflectionColor = vec3(0x36, 0x39, 0x3c) / 255; // canvas reflection
      const vec3 kCaseSideDarkColor = vec3(0x14, 0x15, 0x16) / 255; // darker metal between reflections
      const vec3 kCaseSideLightColor = vec3(0x2a, 0x2c, 0x2f) / 255; // side-light reflection
      const vec3 kCaseFrontColor = vec3(0x15, 0x16, 0x1a) / 255; // front color
      const float kBorderDarkWidth = 0.2;
      const float kCaseSideDarkH = 0.4;
      const float kCaseSideLightH = 0.8;
      const float kCaseFrontH = 1;
      const vec3 kTopLightColor = vec3(0x32, 0x34, 0x39) / 255 - kCaseFrontColor;
      const float kBevelRadius = kBorderDarkWidth * kCaseSideRadius;

      uniform float plug_width_pixels;

      float2 main(const Varyings v, out float4 color) {
        float2 h = sin(min((0.5 - abs(0.5 - v.uv)) / kCaseSideRadius, 1) * 3.14159265358979323846 / 2);
        float bevel = 1 - length(1 - sin(min((0.5 - abs(0.5 - v.uv)) / kBevelRadius, 1) * 3.14159265358979323846 / 2));
        if (h.x < kCaseSideDarkH) {
          color.rgb = mix(kCaseBorderReflectionColor, kCaseSideDarkColor, (h.x - kBorderDarkWidth) / (kCaseSideDarkH - kBorderDarkWidth));
        } else if (h.x < kCaseSideLightH) {
          color.rgb = mix(kCaseSideDarkColor, kCaseSideLightColor, (h.x - kCaseSideDarkH) / (kCaseSideLightH - kCaseSideDarkH));
        } else {
          color.rgb = mix(kCaseSideLightColor, kCaseFrontColor, (h.x - kCaseSideLightH) / (kCaseFrontH - kCaseSideLightH));
        }
        if (bevel < 1) {
          vec3 edge_color = kCaseBorderDarkColor;
          if (v.uv.y > 0.5) {
            edge_color = mix(edge_color, vec3(0.4), clamp((h.x - kCaseSideDarkH) / (kCaseFrontH - kCaseSideDarkH), 0, 1));
          }
          color.rgb = mix(edge_color, color.rgb, bevel);
        }
        color.rgb += kTopLightColor * v.light;
        color.a = 1;
        float radius_pixels = kBevelRadius * plug_width_pixels;
        // Make the corners transparent
        color.rgba *= clamp(bevel * max(radius_pixels / 2, 1), 0, 1);
        return v.position;
      }
    )");

    auto spec_result = SkMeshSpecification::Make(attributes, 16, varyings, vs, fs);
    if (!spec_result.error.isEmpty()) {
      ERROR << "Error creating mesh specification: " << spec_result.error.c_str();
    } else {
      SkRect bounds = SkRect::MakeLTRB(casing_left, casing_top, casing_right, 0);
      Vec2 vertex_data[8] = {
          Vec2(casing_left, 0),          Vec2(0, 0), Vec2(casing_right, 0),          Vec2(1, 0),
          Vec2(casing_left, casing_top), Vec2(0, 1), Vec2(casing_right, casing_top), Vec2(1, 1),
      };
      float plug_width_pixels = canvas.getTotalMatrix().mapRadius(kCasingWidth);
      auto uniforms = SkData::MakeWithCopy(&plug_width_pixels, sizeof(plug_width_pixels));
      auto vertex_buffer = SkMeshes::MakeVertexBuffer(vertex_data, sizeof(vertex_data));
      auto mesh_result =
          SkMesh::Make(spec_result.specification, SkMesh::Mode::kTriangleStrip, vertex_buffer, 4, 0,
                       uniforms, SkSpan<SkMesh::ChildPtr>(), bounds);
      if (!mesh_result.error.isEmpty()) {
        ERROR << "Error creating mesh: " << mesh_result.error.c_str();
      } else {
        SkPaint default_paint;
        default_paint.setColor(0xffffffff);
        canvas.drawMesh(mesh_result.mesh, nullptr, default_paint);
      }
    }
  }

  {  // Steel insert
    SkRect steel_rect = SkRect::MakeLTRB(-0.003, -0.001, 0.003, 0);

    // Fill with black - this will only stay around borders
    SkPaint black;
    black.setColor(0xff000000);
    canvas.drawRect(steel_rect, black);

    // Fill with steel-like gradient
    SkPaint steel_paint;
    SkPoint pts[2] = {Vec2(-0.003, 0), Vec2(0.003, 0)};
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
    constexpr float kRubberWidth = 0.003;
    constexpr float kRubberHeight = 0.015;
    constexpr float kUpperCpOffset = kRubberHeight * 0.5;
    constexpr float kTopCpOffset = kRubberWidth * 0.2;
    float lower_cp_offset = kRubberHeight * 0.3;

    Vec2 pts[6];
    Vec2& left = pts[0];
    Vec2& left_cp1 = pts[1];
    Vec2& left_cp2 = pts[2];
    Vec2& right = pts[3];
    Vec2& right_cp1 = pts[4];
    Vec2& right_cp2 = pts[5];
    SkMatrix inverse;
    if (rubber_sleeve_tail_i >= 0 && transform.invert(&inverse)) {
      auto& p = state.sections[rubber_sleeve_tail_i];
      Vec2 local_sleeve_top = inverse.mapPoint(p.pos);
      float sleeve_top_dist = Length(Vec2(0, casing_top) - local_sleeve_top);
      // 1 when cable is fully retracted, 0 when the rubber part of the connector is fully exposed
      float flatten_factor = std::clamp<float>(1 - 2 * sleeve_top_dist / kRubberHeight, 0, 1);
      float flatten_factor_sin = sin(flatten_factor * M_PI / 2);

      lower_cp_offset *= (1 - flatten_factor_sin);

      float rubber_width = std::lerp(kRubberWidth, kCasingWidth, flatten_factor_sin);

      Vec2 side_offset = Vec2::Polar(p.dir + M_PI / 2, rubber_width / 2);
      Vec2 upper_cp_offset =
          Vec2::Polar(p.dir + M_PI, kUpperCpOffset * powf(1 - flatten_factor_sin, 2));
      Vec2 top_cp_offset = Vec2::Polar(p.dir, kTopCpOffset);
      left = p.pos + side_offset;
      left_cp1 = left + upper_cp_offset;
      left_cp2 = left + top_cp_offset;
      right = p.pos - side_offset;
      right_cp1 = right + top_cp_offset;
      right_cp2 = right + upper_cp_offset;
      inverse.mapPoints(&pts[0].sk, 6);
    } else {
      float sleeve_left = -kRubberWidth / 2;
      float sleeve_right = kRubberWidth / 2;
      float sleeve_top = kCasingHeight + kRubberHeight;
      left = Vec2(sleeve_left, sleeve_top);
      left_cp1 = Vec2(sleeve_left, sleeve_top - kUpperCpOffset);
      left_cp2 = Vec2(sleeve_left, sleeve_top + kTopCpOffset);
      right = Vec2(sleeve_right, sleeve_top);
      right_cp1 = Vec2(sleeve_right, sleeve_top + kTopCpOffset);
      right_cp2 = Vec2(sleeve_right, sleeve_top - kUpperCpOffset);
    }
    Vec2 bottom_left = Vec2(casing_left, casing_top);
    Vec2 bottom_left_cp = bottom_left + Vec2(0, lower_cp_offset);
    Vec2 bottom_right = Vec2(casing_right, casing_top);
    Vec2 bottom_right_cp = bottom_right + Vec2(0, lower_cp_offset);
    SkPath rubber_path;
    rubber_path.moveTo(bottom_left);                      // bottom left
    rubber_path.cubicTo(bottom_left_cp, left_cp1, left);  // upper left
    rubber_path.cubicTo(left_cp2, right_cp1, right);      // upper right
    rubber_path.cubicTo(right_cp2, bottom_right_cp,
                        bottom_right);  // bottom right
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
    path.offset(0, 0.004);
    SkPaint icon_paint;
    icon_paint.setColor(0xff808080);
    icon_paint.setAntiAlias(true);
    canvas.drawPath(path, icon_paint);
  }

  canvas.restore();

  if constexpr (kDebugCable) {  // Draw the arcline
    if (state.arcline) {
      SkPath cable_path = state.arcline->ToPath(false);
      SkPaint arcline_paint;
      arcline_paint.setColor(SK_ColorBLACK);
      arcline_paint.setAlphaf(.5);
      arcline_paint.setStrokeWidth(0.0005);
      arcline_paint.setStyle(SkPaint::kStroke_Style);
      arcline_paint.setAntiAlias(true);
      canvas.drawPath(cable_path, arcline_paint);
    }
  }

  if constexpr (kDebugCable) {  // Draw the cable sections as a series of straight lines
    SkPaint cross_paint;
    cross_paint.setColor(0xffff8800);
    cross_paint.setAntiAlias(true);
    cross_paint.setStrokeWidth(0.0005);
    cross_paint.setStyle(SkPaint::kStroke_Style);

    auto& font = GetFont();
    auto& chain = state.sections;
    SkPaint chain_paint;
    chain_paint.setColor(0xff0088ff);
    chain_paint.setAntiAlias(true);
    chain_paint.setStrokeWidth(0.00025);
    chain_paint.setStyle(SkPaint::kStroke_Style);
    for (int i = 0; i < chain.size(); ++i) {
      Vec2 line_offset = Vec2::Polar(chain[i].dir, kStep / 4);
      canvas.drawLine(chain[i].pos - line_offset, chain[i].pos + line_offset, chain_paint);
      canvas.save();
      Str i_str = ::ToStr(i);
      canvas.translate(chain[i].pos.x, chain[i].pos.y);
      font.DrawText(canvas, i_str, SkPaint());
      canvas.restore();
    }
  }
}

// This function has some nice code for drawing connections between rounded rectangles.
// Keeping this for potential usage in the future
void DrawArrow(SkCanvas& canvas, const SkPath& from_shape, const SkPath& to_shape) {
  static const SkPath kConnectionArrowShape = PathFromSVG(kConnectionArrowShapeSVG);
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
  Vec2 delta = to - from;
  float degrees = atan(delta) * 180 / M_PI;
  float end = Length(delta);
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

OpticalConnectorState::OpticalConnectorState(Vec2 start) : dispenser_v(0) {
  sections.emplace_back(CableSection{
      .pos = start,
      .vel = Vec2(0, 0),
      .acc = Vec2(0, 0),
      .dir = M_PI / 2,
      .true_dir_offset = 0,
      .distance = 0,
      .next_dir_delta = 0,
  });  // plug
  sections.emplace_back(CableSection{
      .pos = start,
      .vel = Vec2(0, 0),
      .acc = Vec2(0, 0),
      .dir = M_PI / 2,
      .true_dir_offset = 0,
      .distance = 0,
      .next_dir_delta = 0,
  });  // dispenser
}
}  // namespace automat::gui