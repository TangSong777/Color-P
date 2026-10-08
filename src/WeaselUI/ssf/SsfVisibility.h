#pragma once

namespace weasel { namespace ssf {
// Composition flags may briefly remain true after a literal keypad commit.
// Only actual content or an explicit mode-switch event may show the skin.
inline bool SuppressEmptyWindow(bool enabled, bool empty, bool mode_tip) {
  return enabled && empty && !mode_tip;
}
}}  // namespace weasel::ssf
