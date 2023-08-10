// Copyright (C) 2020  Matthew "strager" Glazar
// See end of file for extended copyright information.

#include <ostream>
#include <quick-lint-js/port/char8.h>
#include <quick-lint-js/spy-visitor.h>

namespace quick_lint_js {
void PrintTo(const Visited_Variable_Declaration &x, std::ostream *out) {
  *out << x.kind << ' ' << out_string8(x.name);
  if (x.flags & Variable_Declaration_Flags::initialized_with_equals) {
    *out << " (initialized with '=')";
  }
  if (x.flags & Variable_Declaration_Flags::non_empty_namespace) {
    *out << " (non-empty)";
  }
}
}

// quick-lint-js finds bugs in JavaScript programs.
// Copyright (C) 2020  Matthew "strager" Glazar
//
// This file is part of quick-lint-js.
//
// quick-lint-js is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// quick-lint-js is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with quick-lint-js.  If not, see <https://www.gnu.org/licenses/>.
