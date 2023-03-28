#pragma once

#include "math.h"
#include "dual_ptr.h"

#include <include/core/SkCanvas.h>

namespace automaton {

struct Action {
  virtual ~Action() = default;
  virtual void Begin(vec2 position) = 0;
  virtual void Update(vec2 position) = 0;
  virtual void End() = 0;
  virtual void Draw(SkCanvas& canvas, dual_ptr_holder& animation_state) = 0;
};

} // namespace automaton
