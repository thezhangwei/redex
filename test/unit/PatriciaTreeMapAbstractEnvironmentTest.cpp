/**
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#include <cstdint>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <limits>
#include <random>

#include "HashedAbstractEnvironment.h"
#include "HashedSetAbstractDomain.h"
#include "PatriciaTreeMapAbstractEnvironment.h"

using Domain = HashedSetAbstractDomain<std::string>;
using Environment = PatriciaTreeMapAbstractEnvironment<uint32_t, Domain>;

template <typename T1, typename T2>
bool operator==(const HashedAbstractEnvironment<T1, T2>& e1,
                const HashedAbstractEnvironment<T1, T2>& e2) {
  return e1.equals(e2);
}

template <typename T1, typename T2>
bool operator!=(const HashedAbstractEnvironment<T1, T2>& e1,
                const HashedAbstractEnvironment<T1, T2>& e2) {
  return !(e1 == e2);
}

bool operator==(const Domain& d1, const Domain& d2) {
  return d1.equals(d2);
}

bool operator!=(const Domain& d1, const Domain& d2) {
  return !(d1 == d2);
}

class PatriciaTreeMapAbstractEnvironmentTest : public ::testing::Test {
 protected:
  PatriciaTreeMapAbstractEnvironmentTest()
      : m_rd_device(),
        m_generator(m_rd_device()),
        m_size_dist(0, 50),
        m_elem_dist(0, std::numeric_limits<uint32_t>::max()) {}

  Environment generate_random_environment() {
    Environment env;
    size_t size = m_size_dist(m_generator);
    for (size_t i = 0; i < size; ++i) {
      auto rnd = m_elem_dist(m_generator);
      auto rnd_string = std::to_string(m_elem_dist(m_generator));
      env.set(rnd, Domain({rnd_string}));
    }
    return env;
  }

  std::random_device m_rd_device;
  std::mt19937 m_generator;
  std::uniform_int_distribution<uint32_t> m_size_dist;
  std::uniform_int_distribution<uint32_t> m_elem_dist;
};

HashedAbstractEnvironment<uint32_t, Domain> hae_from_ptae(
    const Environment& env) {
  HashedAbstractEnvironment<uint32_t, Domain> hae;
  if (env.is_value()) {
    for (const auto& pair : env.bindings()) {
      hae.set(pair.first, pair.second);
    }
  } else if (env.is_top()) {
    hae.set_to_top();
  } else {
    hae.set_to_bottom();
  }
  return hae;
}

TEST_F(PatriciaTreeMapAbstractEnvironmentTest, latticeOperations) {
  Environment e1({{1, Domain({"a", "b"})},
                  {2, Domain("c")},
                  {3, Domain({"d", "e", "f"})},
                  {4, Domain({"a", "f"})}});
  Environment e2({{0, Domain({"c", "f"})},
                  {2, Domain({"c", "d"})},
                  {3, Domain({"d", "e", "g", "h"})}});

  EXPECT_EQ(4, e1.size());
  EXPECT_EQ(3, e2.size());

  EXPECT_TRUE(Environment::bottom().leq(e1));
  EXPECT_FALSE(e1.leq(Environment::bottom()));
  EXPECT_FALSE(Environment::top().leq(e1));
  EXPECT_TRUE(e1.leq(Environment::top()));
  EXPECT_FALSE(e1.leq(e2));
  EXPECT_FALSE(e2.leq(e1));

  EXPECT_TRUE(e1.equals(e1));
  EXPECT_FALSE(e1.equals(e2));
  EXPECT_TRUE(Environment::bottom().equals(Environment::bottom()));
  EXPECT_TRUE(Environment::top().equals(Environment::top()));
  EXPECT_FALSE(Environment::bottom().equals(Environment::top()));

  Environment join = e1.join(e2);
  EXPECT_TRUE(e1.leq(join));
  EXPECT_TRUE(e2.leq(join));
  EXPECT_EQ(2, join.size());
  EXPECT_THAT(join.get(2).elements(),
              ::testing::UnorderedElementsAre("c", "d"));
  EXPECT_THAT(join.get(3).elements(),
              ::testing::UnorderedElementsAre("d", "e", "f", "g", "h"));
  EXPECT_TRUE(join.equals(e1.widening(e2)));

  EXPECT_TRUE(e1.join(Environment::top()).is_top());
  EXPECT_TRUE(e1.join(Environment::bottom()).equals(e1));

  Environment meet = e1.meet(e2);
  EXPECT_TRUE(meet.leq(e1));
  EXPECT_TRUE(meet.leq(e2));
  EXPECT_EQ(5, meet.size());
  EXPECT_THAT(meet.get(0).elements(),
              ::testing::UnorderedElementsAre("c", "f"));
  EXPECT_THAT(meet.get(1).elements(),
              ::testing::UnorderedElementsAre("a", "b"));
  EXPECT_THAT(meet.get(2).elements(), ::testing::ElementsAre("c"));
  EXPECT_THAT(meet.get(3).elements(),
              ::testing::UnorderedElementsAre("d", "e"));
  EXPECT_THAT(meet.get(4).elements(),
              ::testing::UnorderedElementsAre("a", "f"));
  EXPECT_TRUE(meet.equals(e1.narrowing(e2)));

  EXPECT_TRUE(e1.meet(Environment::bottom()).is_bottom());
  EXPECT_TRUE(e1.meet(Environment::top()).equals(e1));
}

TEST_F(PatriciaTreeMapAbstractEnvironmentTest, destructiveOperations) {
  Environment e1({{1, Domain({"a", "b"})}});
  Environment e2({{2, Domain({"c", "d"})}, {3, Domain({"g", "h"})}});

  e1.set(2, Domain({"c", "f"})).set(4, Domain({"e", "f", "g"}));
  EXPECT_EQ(3, e1.size());
  EXPECT_THAT(e1.get(1).elements(),
              ::testing::UnorderedElementsAre("a", "b"));
  EXPECT_THAT(e1.get(2).elements(),
              ::testing::UnorderedElementsAre("c", "f"));
  EXPECT_THAT(e1.get(4).elements(),
              ::testing::UnorderedElementsAre("e", "f", "g"));

  Environment join = e1;
  join.join_with(e2);
  EXPECT_EQ(1, join.size()) << join;
  EXPECT_THAT(join.get(2).elements(),
              ::testing::UnorderedElementsAre("c", "d", "f"));

  Environment widening = e1;
  widening.widen_with(e2);
  EXPECT_TRUE(widening.equals(join));

  Environment meet = e1;
  meet.meet_with(e2);
  EXPECT_EQ(4, meet.size());
  EXPECT_THAT(meet.get(1).elements(),
              ::testing::UnorderedElementsAre("a", "b"));
  EXPECT_THAT(meet.get(2).elements(), ::testing::ElementsAre("c"));
  EXPECT_THAT(meet.get(3).elements(),
              ::testing::UnorderedElementsAre("g", "h"));
  EXPECT_THAT(meet.get(4).elements(),
              ::testing::UnorderedElementsAre("e", "f", "g"));

  Environment narrowing = e1;
  narrowing.narrow_with(e2);
  EXPECT_TRUE(narrowing.equals(meet));

  auto add_e = [](const Domain& s) {
    auto copy = s;
    copy.add("e");
    return copy;
  };
  e1.update(1, add_e).update(2, add_e);
  EXPECT_EQ(3, e1.size());
  EXPECT_THAT(e1.get(1).elements(),
              ::testing::UnorderedElementsAre("a", "b", "e"));
  EXPECT_THAT(e1.get(2).elements(),
              ::testing::UnorderedElementsAre("c", "e", "f"));
  EXPECT_THAT(e1.get(4).elements(),
              ::testing::UnorderedElementsAre("e", "f", "g"));

  Environment e3 = e2;
  EXPECT_EQ(2, e3.size());
  e3.update(1, add_e).update(2, add_e);
  EXPECT_EQ(2, e3.size());
  EXPECT_THAT(e3.get(2).elements(),
              ::testing::UnorderedElementsAre("c", "d", "e"));
  EXPECT_THAT(e3.get(3).elements(),
              ::testing::UnorderedElementsAre("g", "h"));

  auto refine_de = [](const Domain& s) {
    auto copy = s;
    copy.meet_with(Domain({"d", "e"}));
    return copy;
  };
  EXPECT_EQ(2, e2.size());
  e2.update(1, refine_de).update(2, refine_de);
  EXPECT_EQ(3, e2.size());
  EXPECT_THAT(e2.get(1).elements(),
              ::testing::UnorderedElementsAre("d", "e"));
  EXPECT_THAT(e2.get(2).elements(), ::testing::ElementsAre("d"));
  EXPECT_THAT(e2.get(3).elements(),
              ::testing::UnorderedElementsAre("g", "h"));
}

TEST_F(PatriciaTreeMapAbstractEnvironmentTest, robustness) {
  for (size_t k = 0; k < 10; ++k) {
    Environment e1 = this->generate_random_environment();
    Environment e2 = this->generate_random_environment();

    auto ref_meet = hae_from_ptae(e1);
    ref_meet.meet_with(hae_from_ptae(e2));
    auto meet = e1;
    meet.meet_with(e2);
    EXPECT_EQ(hae_from_ptae(meet), ref_meet);
    EXPECT_TRUE(meet.leq(e1));
    EXPECT_TRUE(meet.leq(e2));

    auto ref_join = hae_from_ptae(e1);
    ref_join.join_with(hae_from_ptae(e2));
    auto join = e1;
    join.join_with(e2);
    EXPECT_EQ(hae_from_ptae(join), ref_join);
    EXPECT_TRUE(e1.leq(join));
    EXPECT_TRUE(e2.leq(join));
  }
}

TEST_F(PatriciaTreeMapAbstractEnvironmentTest, whiteBox) {
  // The algorithms are designed in such a way that Patricia trees that are left
  // unchanged by an operation are not reconstructed (i.e., the result of an
  // operation shares structure with the operands whenever possible). This is
  // what we check here.
  Environment e({{1, Domain({"a"})}});
  auto tree_before = e.bindings().get_patricia_tree();
  e.update(1, [](const Domain& x) { return Domain({"a"}); });
  EXPECT_EQ(e.bindings().get_patricia_tree(), tree_before);
  e.meet_with(e);
  EXPECT_EQ(e.bindings().get_patricia_tree(), tree_before);
  e.join_with(e);
  EXPECT_EQ(e.bindings().get_patricia_tree(), tree_before);
}
