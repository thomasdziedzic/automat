#pragma once

#include "virtual_fs.hh"

namespace automat::audio {

using Sound = maf::fs::VFile;

#ifdef __linux__
void Init(int* argc, char*** argv);
#else
void Init();
#endif

void Play(Sound&);

struct Effect {
  virtual ~Effect() {}
};

std::unique_ptr<Effect> MakeBeginLoopEndEffect(Sound& begin, Sound& loop, Sound& end);

}  // namespace automat::audio