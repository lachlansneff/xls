// Copyright 2021 The XLS Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "xls/passes/comparison_simplification_pass.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/statusor.h"
#include "xls/common/status/matchers.h"
#include "xls/ir/function.h"
#include "xls/ir/function_builder.h"
#include "xls/ir/ir_matcher.h"
#include "xls/ir/ir_test_base.h"
#include "xls/ir/package.h"

namespace m = ::xls::op_matchers;

namespace xls {
namespace {

using status_testing::IsOkAndHolds;

class ComparisonSimplificationPassTest : public IrTestBase {
 protected:
  ComparisonSimplificationPassTest() = default;

  absl::StatusOr<bool> Run(Package* p) {
    PassResults results;
    return ComparisonSimplificationPass().Run(p, PassOptions(), &results);
  }
};

TEST_F(ComparisonSimplificationPassTest, OrOfEqAndNe) {
  auto p = CreatePackage();
  FunctionBuilder fb(TestName(), p.get());
  BValue x = fb.Param("x", p->GetBitsType(32));
  BValue x_ne_42 = fb.Ne(x, fb.Literal(UBits(42, 32)));
  BValue x_eq_37 = fb.Eq(x, fb.Literal(UBits(37, 32)));
  fb.And(x_ne_42, x_eq_37);
  XLS_ASSERT_OK_AND_ASSIGN(Function * f, fb.Build());
  ASSERT_THAT(Run(p.get()), IsOkAndHolds(true));

  EXPECT_THAT(f->return_value(), m::Eq(x.node(), m::Literal(37)));
}

TEST_F(ComparisonSimplificationPassTest, EqWithNonliterals) {
  auto p = CreatePackage();
  FunctionBuilder fb(TestName(), p.get());
  BValue x = fb.Param("x", p->GetBitsType(32));
  BValue y = fb.Param("y", p->GetBitsType(32));
  BValue x_ne_42 = fb.Ne(x, fb.Literal(UBits(42, 32)));
  BValue x_eq_y = fb.Eq(x, y);
  fb.And(x_ne_42, x_eq_y);
  XLS_ASSERT_OK_AND_ASSIGN(Function * f, fb.Build());
  ASSERT_THAT(Run(p.get()), IsOkAndHolds(false));

  EXPECT_THAT(f->return_value(), m::And(m::Ne(), m::Eq()));
}

TEST_F(ComparisonSimplificationPassTest, ComparisonsWithDifferentVariables) {
  auto p = CreatePackage();
  FunctionBuilder fb(TestName(), p.get());
  BValue x = fb.Param("x", p->GetBitsType(32));
  BValue y = fb.Param("y", p->GetBitsType(32));
  BValue x_ne_42 = fb.Ne(fb.Literal(UBits(42, 32)), x);
  BValue y_eq_37 = fb.Eq(y, fb.Literal(UBits(37, 32)));
  fb.And(x_ne_42, y_eq_37);
  XLS_ASSERT_OK_AND_ASSIGN(Function * f, fb.Build());
  // The comparisons are between different variables `x` and `y` so should not
  // be transformed.
  ASSERT_THAT(Run(p.get()), IsOkAndHolds(false));

  EXPECT_THAT(f->return_value(), m::And());
}

TEST_F(ComparisonSimplificationPassTest, EmptyRange) {
  auto p = CreatePackage();
  FunctionBuilder fb(TestName(), p.get());
  BValue x = fb.Param("x", p->GetBitsType(32));
  BValue x_eq_42 = fb.Eq(x, fb.Literal(UBits(42, 32)));
  BValue x_eq_37 = fb.Eq(fb.Literal(UBits(37, 32)), x);
  fb.And(x_eq_42, x_eq_37);
  XLS_ASSERT_OK_AND_ASSIGN(Function * f, fb.Build());
  ASSERT_THAT(Run(p.get()), IsOkAndHolds(true));

  EXPECT_THAT(f->return_value(), m::Literal(0));
}

TEST_F(ComparisonSimplificationPassTest, MaximalRange) {
  auto p = CreatePackage();
  FunctionBuilder fb(TestName(), p.get());
  BValue x = fb.Param("x", p->GetBitsType(32));
  BValue x_ne_42 = fb.Ne(x, fb.Literal(UBits(42, 32)));
  BValue x_ne_37 = fb.Ne(fb.Literal(UBits(37, 32)), x);
  fb.Or(x_ne_42, x_ne_37);
  XLS_ASSERT_OK_AND_ASSIGN(Function * f, fb.Build());
  ASSERT_THAT(Run(p.get()), IsOkAndHolds(true));

  EXPECT_THAT(f->return_value(), m::Literal(1));
}

TEST_F(ComparisonSimplificationPassTest, NotNeLiteral) {
  auto p = CreatePackage();
  FunctionBuilder fb(TestName(), p.get());
  BValue x = fb.Param("x", p->GetBitsType(32));
  fb.Not(fb.Ne(x, fb.Literal(UBits(42, 32))));
  XLS_ASSERT_OK_AND_ASSIGN(Function * f, fb.Build());
  ASSERT_THAT(Run(p.get()), IsOkAndHolds(true));

  EXPECT_THAT(f->return_value(), m::Eq(x.node(), m::Literal(42)));
}

TEST_F(ComparisonSimplificationPassTest, EqsWithPreciseGap) {
  auto p = CreatePackage();
  FunctionBuilder fb(TestName(), p.get());
  BValue x = fb.Param("x", p->GetBitsType(2));
  BValue x_eq_0 = fb.Eq(x, fb.Literal(UBits(0, 2)));
  BValue x_eq_1 = fb.Eq(x, fb.Literal(UBits(1, 2)));
  BValue x_eq_3 = fb.Eq(x, fb.Literal(UBits(3, 2)));
  fb.Not(fb.Or({x_eq_0, x_eq_1, x_eq_3}));
  XLS_ASSERT_OK_AND_ASSIGN(Function * f, fb.Build());
  ASSERT_THAT(Run(p.get()), IsOkAndHolds(true));

  EXPECT_THAT(f->return_value(), m::Eq(x.node(), m::Literal(2)));
}

TEST_F(ComparisonSimplificationPassTest, NotAndOfNeqsPrecise) {
  auto p = CreatePackage();
  FunctionBuilder fb(TestName(), p.get());
  BValue x = fb.Param("x", p->GetBitsType(2));
  BValue x_ne_0 = fb.Ne(x, fb.Literal(UBits(0, 2)));
  BValue x_ne_1 = fb.Ne(x, fb.Literal(UBits(1, 2)));
  BValue x_ne_3 = fb.Ne(x, fb.Literal(UBits(3, 2)));
  fb.Not(fb.And({x_ne_0, x_ne_1, x_ne_3}));
  XLS_ASSERT_OK_AND_ASSIGN(Function * f, fb.Build());
  ASSERT_THAT(Run(p.get()), IsOkAndHolds(true));

  EXPECT_THAT(f->return_value(), m::Ne(x.node(), m::Literal(2)));
}

TEST_F(ComparisonSimplificationPassTest, NotAndOfNeqsButNot) {
  auto p = CreatePackage();
  FunctionBuilder fb(TestName(), p.get());
  BValue x = fb.Param("x", p->GetBitsType(3));
  BValue x_ne_0 = fb.Ne(x, fb.Literal(UBits(0, 3)));
  BValue x_ne_1 = fb.Ne(x, fb.Literal(UBits(1, 3)));
  BValue x_ne_3 = fb.Ne(x, fb.Literal(UBits(3, 3)));
  fb.Not(fb.And({x_ne_0, x_ne_1, x_ne_3}));
  XLS_ASSERT_OK_AND_ASSIGN(Function * f, fb.Build());
  ASSERT_THAT(Run(p.get()), IsOkAndHolds(false));

  EXPECT_THAT(f->return_value(), m::Not(m::And()));
}

}  // namespace
}  // namespace xls
