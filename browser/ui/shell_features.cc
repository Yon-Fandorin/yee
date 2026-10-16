// Copyright 2026 The Yee Authors. BSD-style license in LICENSE.
#include "chrome/browser/ui/views/yee/shell_features.h"

#include "base/command_line.h"

namespace yee {
bool IsShellEnabled() {
  return !base::CommandLine::ForCurrentProcess()->HasSwitch(
      "disable-yee-shell-scaffold");
}
}  // namespace yee
