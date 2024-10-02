// SPDX-FileCopyrightText: Copyright 2024 Automat Authors
// SPDX-License-Identifier: MIT
#pragma once

#include <gtest/gtest.h>

#include "backtrace.hh"
#include "base.hh"

namespace automat {

struct TestBase : ::testing::Test {
  Location root = Location(nullptr);
  Machine& machine = *root.Create<Machine>();

  TestBase() {
    EnableBacktraceOnSIGSEGV();
    machine.name = "Root Machine";
  }
};

}  // namespace automat
