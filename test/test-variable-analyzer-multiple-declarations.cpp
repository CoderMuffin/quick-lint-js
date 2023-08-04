// Copyright (C) 2020  Matthew "strager" Glazar
// See end of file for extended copyright information.

#include <cstring>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <quick-lint-js/diag-collector.h>
#include <quick-lint-js/diag-matcher.h>
#include <quick-lint-js/fe/language.h>
#include <quick-lint-js/fe/variable-analyzer.h>
#include <quick-lint-js/identifier-support.h>
#include <quick-lint-js/port/char8.h>
#include <quick-lint-js/variable-analyzer-support.h>

using ::testing::ElementsAreArray;
using ::testing::IsEmpty;

// This file contains tests for multiple declarations with the same name.
namespace quick_lint_js {
namespace {
TEST(Test_Variable_Analyzer_Multiple_Declarations,
     enum_and_namespace_do_not_conflict) {
  test_parse_and_analyze(
      u8"namespace A {} "_sv
      u8"enum A {} "_sv,
      no_diags, typescript_analyze_options, default_globals);

  test_parse_and_analyze(
      u8"enum A {} "_sv
      u8"namespace A {} "_sv,
      no_diags, typescript_analyze_options, default_globals);
}

TEST(Test_Variable_Analyzer_Multiple_Declarations,
     variable_and_namespace_do_not_conflict) {
  const Char8 namespace_declaration[] = u8"n";
  const Char8 var_declaration[] = u8"n";

  for (Variable_Kind var_kind :
       {Variable_Kind::_const, Variable_Kind::_let, Variable_Kind::_var}) {
    SCOPED_TRACE(var_kind);

    {
      // namespace n {}
      // var n;
      Diag_Collector v;
      Variable_Analyzer l(&v, &default_globals, typescript_var_options);
      l.visit_enter_namespace_scope();
      l.visit_exit_namespace_scope();
      l.visit_variable_declaration(identifier_of(namespace_declaration),
                                   Variable_Kind::_namespace,
                                   Variable_Declaration_Flags::none);
      l.visit_variable_declaration(
          identifier_of(var_declaration), var_kind,
          Variable_Declaration_Flags::initialized_with_equals);
      l.visit_end_of_module();

      EXPECT_THAT(v.errors, IsEmpty());
    }

    {
      // var n;
      // namespace n {}
      Diag_Collector v;
      Variable_Analyzer l(&v, &default_globals, typescript_var_options);
      l.visit_variable_declaration(
          identifier_of(var_declaration), var_kind,
          Variable_Declaration_Flags::initialized_with_equals);
      l.visit_enter_namespace_scope();
      l.visit_exit_namespace_scope();
      l.visit_variable_declaration(identifier_of(namespace_declaration),
                                   Variable_Kind::_namespace,
                                   Variable_Declaration_Flags::none);
      l.visit_end_of_module();

      EXPECT_THAT(v.errors, IsEmpty());
    }
  }
}

TEST(Test_Variable_Analyzer_Multiple_Declarations,
     namespace_can_be_declared_multiple_times) {
  test_parse_and_analyze(
      u8"namespace ns {} "_sv
      u8"namespace ns {} "_sv
      u8"namespace ns {} "_sv,
      no_diags, typescript_analyze_options, default_globals);
}

TEST(Test_Variable_Analyzer_Multiple_Declarations,
     type_alias_and_local_variable_do_not_conflict) {
  const Char8 type_declaration[] = u8"x";
  const Char8 var_declaration[] = u8"x";

  for (Variable_Kind var_kind :
       {Variable_Kind::_const, Variable_Kind::_let, Variable_Kind::_var}) {
    SCOPED_TRACE(var_kind);

    {
      // type x = null;
      // var x;
      Diag_Collector v;
      Variable_Analyzer l(&v, &default_globals, typescript_var_options);
      l.visit_variable_declaration(identifier_of(type_declaration),
                                   Variable_Kind::_type_alias,
                                   Variable_Declaration_Flags::none);
      l.visit_variable_declaration(
          identifier_of(var_declaration), var_kind,
          Variable_Declaration_Flags::initialized_with_equals);
      l.visit_end_of_module();

      EXPECT_THAT(v.errors, IsEmpty());
    }

    {
      // var x;
      // type x = null;
      Diag_Collector v;
      Variable_Analyzer l(&v, &default_globals, typescript_var_options);
      l.visit_variable_declaration(
          identifier_of(var_declaration), var_kind,
          Variable_Declaration_Flags::initialized_with_equals);
      l.visit_variable_declaration(identifier_of(type_declaration),
                                   Variable_Kind::_type_alias,
                                   Variable_Declaration_Flags::none);
      l.visit_end_of_module();

      EXPECT_THAT(v.errors, IsEmpty());
    }
  }
}

TEST(Test_Variable_Analyzer_Multiple_Declarations,
     namespace_can_appear_after_function_or_class_with_same_name) {
  test_parse_and_analyze(
      u8"function x() {} "_sv
      u8"namespace x {} "_sv,
      no_diags, typescript_analyze_options, default_globals);

  test_parse_and_analyze(
      u8"class x {} "_sv
      u8"namespace x {} "_sv,
      no_diags, typescript_analyze_options, default_globals);
}

TEST(Test_Variable_Analyzer_Multiple_Declarations,
     function_or_class_cannot_appear_after_non_empty_namespace_with_same_name) {
  const Char8 class_declaration[] = u8"x";
  const Char8 namespace_declaration[] = u8"x";

  test_parse_and_analyze(
      u8"namespace x { ; }  function x() {}"_sv,
      u8"                            ^ Diag_Redeclaration_Of_Variable.redeclaration\n"_diag
      u8"          ^ .original_declaration"_diag,
      typescript_analyze_options, default_globals);

  {
    // namespace x { ; }
    // class x {}      // ERROR
    Diag_Collector v;
    Variable_Analyzer l(&v, &default_globals, typescript_var_options);
    l.visit_enter_namespace_scope();
    l.visit_exit_namespace_scope();
    l.visit_variable_declaration(
        identifier_of(namespace_declaration), Variable_Kind::_namespace,
        Variable_Declaration_Flags::non_empty_namespace);

    l.visit_enter_class_scope();
    l.visit_enter_class_scope_body(identifier_of(class_declaration));
    l.visit_exit_class_scope();
    l.visit_variable_declaration(identifier_of(class_declaration),
                                 Variable_Kind::_class,
                                 Variable_Declaration_Flags::none);
    l.visit_end_of_module();

    EXPECT_THAT(v.errors,
                ElementsAreArray({
                    DIAG_TYPE_2_SPANS(
                        Diag_Redeclaration_Of_Variable,             //
                        redeclaration, span_of(class_declaration),  //
                        original_declaration, span_of(namespace_declaration)),
                }));
  }
}

TEST(Test_Variable_Analyzer_Multiple_Declarations,
     function_or_class_can_appear_after_empty_namespace_with_same_name) {
  test_parse_and_analyze(
      u8"namespace x {} "_sv
      u8"function x() {} "_sv,
      no_diags, typescript_analyze_options, default_globals);

  test_parse_and_analyze(
      u8"namespace x {} "_sv
      u8"class x {} "_sv,
      no_diags, typescript_analyze_options, default_globals);
}

TEST(Test_Variable_Analyzer_Multiple_Declarations,
     function_parameter_can_have_same_name_as_generic_parameter) {
  test_parse_and_analyze(u8"(function <T>(T) {});"_sv, no_diags,
                         typescript_analyze_options, default_globals);
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
