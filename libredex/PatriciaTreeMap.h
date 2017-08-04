/**
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#pragma once

#include <cstdint>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <memory>
#include <stack>
#include <type_traits>
#include <utility>

#include "Debug.h"
#include "PatriciaTreeUtil.h"
#include "Util.h"

// Forward declarations.
namespace ptmap_impl {

template <typename IntegerType, typename Value>
class PatriciaTree;

template <typename IntegerType, typename Value>
class PatriciaTreeLeaf;

template <typename IntegerType, typename Value>
class PatriciaTreeIterator;

template <typename T>
using CombiningFunction = std::function<T(const T&, const T&)>;

template <typename IntegerType, typename Value>
inline const Value* find_value(
    IntegerType key, std::shared_ptr<PatriciaTree<IntegerType, Value>> tree);

template <typename IntegerType, typename Value>
inline bool leq(
    std::shared_ptr<PatriciaTree<IntegerType, Value>> tree1,
    std::shared_ptr<PatriciaTree<IntegerType, Value>> tree2);

template <typename IntegerType, typename Value>
inline bool equals(std::shared_ptr<PatriciaTree<IntegerType, Value>> tree1,
                   std::shared_ptr<PatriciaTree<IntegerType, Value>> tree2);

template <typename IntegerType, typename Value>
inline std::shared_ptr<PatriciaTree<IntegerType, Value>> combine_new_leaf(
    const CombiningFunction<Value>& combine,
    IntegerType key,
    const Value& value);

template <typename IntegerType, typename Value>
inline std::shared_ptr<PatriciaTree<IntegerType, Value>> update(
    const CombiningFunction<Value>& combine,
    IntegerType key,
    const Value& value,
    std::shared_ptr<PatriciaTree<IntegerType, Value>> tree);

template <typename IntegerType, typename Value>
inline std::shared_ptr<PatriciaTree<IntegerType, Value>> merge(
    const CombiningFunction<Value>& combine,
    std::shared_ptr<PatriciaTree<IntegerType, Value>> s,
    std::shared_ptr<PatriciaTree<IntegerType, Value>> t);

template <typename IntegerType, typename Value>
inline std::shared_ptr<PatriciaTree<IntegerType, Value>> intersect(
    const ptmap_impl::CombiningFunction<Value>& combine,
    std::shared_ptr<PatriciaTree<IntegerType, Value>> s,
    std::shared_ptr<PatriciaTree<IntegerType, Value>> t);

template <typename T>
T snd(const T&, const T& second) {
  return second;
}

} // namespace ptmap_impl

/*
 * This implements a map of integer keys and AbstractDomain values. It's based
 * on the following paper:
 *
 *   C. Okasaki, A. Gill. Fast Mergeable Integer Maps. In Workshop on ML (1998).
 *
 * See PatriciaTreeSet.h for more details about Patricia trees.
 *
 * Specializing this implementation for AbstractDomain values (instead of
 * arbitrary values) allows us to better optimize operations like meet, join,
 * and leq. It also makes it easy for us to save space by implicitly mapping
 * all unbound keys to Top.
 */
template <typename Key, typename Value>
class PatriciaTreeMap final {
 public:
  using IntegerType =
      typename std::conditional_t<std::is_pointer<Key>::value, uintptr_t, Key>;

  using key_type = Key;
  using mapped_type = Value;
  using value_type = std::pair<const Key, Value>;
  using iterator = ptmap_impl::PatriciaTreeIterator<Key, Value>;
  using combining_function = ptmap_impl::CombiningFunction<Value>;

  PatriciaTreeMap() = default;

  ~PatriciaTreeMap() {
    // The destructor is the only method that is guaranteed to be created when a
    // class template is instantiated. This is a good place to perform all the
    // sanity checks on the template parameters.
    static_assert(std::is_base_of<AbstractDomain<Value>, Value>::value,
                  "Value doesn't inherit from AbstractDomain");
  }

  bool is_empty() const { return m_tree == nullptr; }

  size_t size() const {
    size_t s = 0;
    for (auto UNUSED pair : *this) {
      ++s;
    }
    return s;
  }

  iterator begin() const { return iterator(m_tree); }

  iterator end() const { return iterator(); }

  const Value at(Key key) const {
    const Value* value = ptmap_impl::find_value(encode(key), m_tree);
    if (value == nullptr) {
      return Value::top();
    }
    return *value;
  }

  bool leq(const PatriciaTreeMap& other) const {
    return ptmap_impl::leq<IntegerType>(m_tree, other.m_tree);
  }

  bool equals(const PatriciaTreeMap& other) const {
    return ptmap_impl::equals<IntegerType>(m_tree, other.m_tree);
  }

  PatriciaTreeMap& update(const std::function<Value(const Value&)>& operation,
                          Key key) {
    m_tree = ptmap_impl::update<IntegerType, Value>(
        [&operation](const Value& x, const Value&) { return operation(x); },
        encode(key),
        Value::top(),
        m_tree);
    return *this;
  }

  PatriciaTreeMap& insert_or_assign(Key key, const Value& value) {
    m_tree = ptmap_impl::update<IntegerType, Value>(
        ptmap_impl::snd<Value>, encode(key), value, m_tree);
    return *this;
  }

  PatriciaTreeMap& union_with(const combining_function& combine,
                              const PatriciaTreeMap& other) {
    m_tree = ptmap_impl::merge(combine, m_tree, other.m_tree);
    return *this;
  }

  PatriciaTreeMap& intersection_with(const combining_function& combine,
                                     const PatriciaTreeMap& other) {
    m_tree = ptmap_impl::intersect<IntegerType>(combine, m_tree, other.m_tree);
    return *this;
  }

  PatriciaTreeMap get_union_with(const combining_function& combine,
                                 const PatriciaTreeMap& other) const {
    auto result = *this;
    result.union_with(combine, other);
    return result;
  }

  PatriciaTreeMap get_intersection_with(const combining_function& combine,
                                        const PatriciaTreeMap& other) const {
    auto result = *this;
    result.intersection_with(combine, other);
    return result;
  }

  void clear() { m_tree.reset(); }

  std::shared_ptr<ptmap_impl::PatriciaTree<IntegerType, Value>>
  get_patricia_tree() const {
    return m_tree;
  }

 private:
  // These functions are used to handle the type conversions required when
  // manipulating sets of pointers. The first parameter is necessary to make
  // template deduction work.
  template <typename T = Key,
            typename = typename std::enable_if_t<std::is_pointer<T>::value>>
  static uintptr_t encode(Key x) {
    return reinterpret_cast<uintptr_t>(x);
  }

  template <typename T = Key,
            typename = typename std::enable_if_t<!std::is_pointer<T>::value>>
  static Key encode(Key x) {
    return x;
  }

  template <typename T = Key,
            typename = typename std::enable_if_t<std::is_pointer<T>::value>>
  static Key decode(uintptr_t x) {
    return reinterpret_cast<Key>(x);
  }

  template <typename T = Key,
            typename = typename std::enable_if_t<!std::is_pointer<T>::value>>
  static Key decode(Key x) {
    return x;
  }

  std::shared_ptr<ptmap_impl::PatriciaTree<IntegerType, Value>> m_tree;

  template <typename T, typename V>
  friend std::ostream& operator<<(std::ostream&, const PatriciaTreeMap<T, V>&);

  template <typename T, typename V>
  friend class ptmap_impl::PatriciaTreeIterator;
};

template <typename Key, typename Value>
inline std::ostream& operator<<(std::ostream& o,
                                const PatriciaTreeMap<Key, Value>& s) {
  o << "{";
  for (auto it = s.begin(); it != s.end(); ++it) {
    o << PatriciaTreeMap<Key, Value>::decode(it->first) << " -> "
      << it->second;
    if (std::next(it) != s.end()) {
      o << ", ";
    }
  }
  o << "}";
  return o;
}

namespace ptmap_impl {

using namespace pt_util;

template <typename IntegerType, typename Value>
class PatriciaTree {
 public:
  // A Patricia tree is an immutable structure.
  PatriciaTree& operator=(const PatriciaTree& other) = delete;

  virtual ~PatriciaTree() {
    // The destructor is the only method that is guaranteed to be created when
    // a class template is instantiated. This is a good place to perform all
    // the sanity checks on the template parameters.
    static_assert(std::is_unsigned<IntegerType>::value,
                  "IntegerType is not an unsigned arihmetic type");
  }

  virtual bool is_leaf() const = 0;

  bool is_branch() const { return !is_leaf(); }
};

template <typename IntegerType, typename Value>
class PatriciaTreeBranch final : public PatriciaTree<IntegerType, Value> {
 public:
  PatriciaTreeBranch(
      IntegerType prefix,
      IntegerType branching_bit,
      std::shared_ptr<PatriciaTree<IntegerType, Value>> left_tree,
      std::shared_ptr<PatriciaTree<IntegerType, Value>> right_tree)
      : m_prefix(prefix),
        m_stacking_bit(branching_bit),
        m_left_tree(left_tree),
        m_right_tree(right_tree) {}

  bool is_leaf() const override { return false; }

  IntegerType prefix() const { return m_prefix; }

  IntegerType branching_bit() const { return m_stacking_bit; }

  std::shared_ptr<PatriciaTree<IntegerType, Value>> left_tree() const {
    return m_left_tree;
  }

  std::shared_ptr<PatriciaTree<IntegerType, Value>> right_tree() const {
    return m_right_tree;
  }

 private:
  IntegerType m_prefix;
  IntegerType m_stacking_bit;
  std::shared_ptr<PatriciaTree<IntegerType, Value>> m_left_tree;
  std::shared_ptr<PatriciaTree<IntegerType, Value>> m_right_tree;
};

template <typename IntegerType, typename Value>
class PatriciaTreeLeaf final : public PatriciaTree<IntegerType, Value> {
 public:
  explicit PatriciaTreeLeaf(IntegerType key, const Value& value)
      : m_pair(key, value) {}

  bool is_leaf() const override { return true; }

  const IntegerType& key() const { return m_pair.first; }

  const Value& value() const { return m_pair.second; }

 private:
  std::pair<IntegerType, Value> m_pair;

  template <typename T, typename V>
  friend class ptmap_impl::PatriciaTreeIterator;
};

template <typename IntegerType, typename Value>
std::shared_ptr<PatriciaTreeBranch<IntegerType, Value>> join(
    IntegerType prefix0,
    std::shared_ptr<PatriciaTree<IntegerType, Value>> tree0,
    IntegerType prefix1,
    std::shared_ptr<PatriciaTree<IntegerType, Value>> tree1) {
  IntegerType m = get_branching_bit(prefix0, prefix1);
  if (is_zero_bit(prefix0, m)) {
    return std::make_shared<PatriciaTreeBranch<IntegerType, Value>>(
        mask(prefix0, m), m, tree0, tree1);
  } else {
    return std::make_shared<PatriciaTreeBranch<IntegerType, Value>>(
        mask(prefix0, m), m, tree1, tree0);
  }
}

// This function is used to prevent the creation of branch nodes with only one
// child.
template <typename IntegerType, typename Value>
std::shared_ptr<PatriciaTree<IntegerType, Value>> make_branch(
    IntegerType prefix,
    IntegerType branching_bit,
    std::shared_ptr<PatriciaTree<IntegerType, Value>> left_tree,
    std::shared_ptr<PatriciaTree<IntegerType, Value>> right_tree) {
  if (left_tree == nullptr) {
    return right_tree;
  }
  if (right_tree == nullptr) {
    return left_tree;
  }
  return std::make_shared<PatriciaTreeBranch<IntegerType, Value>>(
      prefix, branching_bit, left_tree, right_tree);
}

// Tries to find the value corresponding to :key. Returns null if the key is
// not present in :tree.
template <typename IntegerType, typename Value>
inline const Value* find_value(
    IntegerType key,
    std::shared_ptr<PatriciaTree<IntegerType, Value>> tree) {
  if (tree == nullptr) {
    return nullptr;
  }
  if (tree->is_leaf()) {
    auto leaf =
        std::static_pointer_cast<PatriciaTreeLeaf<IntegerType, Value>>(tree);
    if (key == leaf->key()) {
      return &leaf->value();
    }
    return nullptr;
  }
  auto branch =
      std::static_pointer_cast<PatriciaTreeBranch<IntegerType, Value>>(tree);
  if (is_zero_bit(key, branch->branching_bit())) {
    return find_value(key, branch->left_tree());
  } else {
    return find_value(key, branch->right_tree());
  }
}

template <typename IntegerType, typename Value>
inline bool leq(
    std::shared_ptr<PatriciaTree<IntegerType, Value>> s,
    std::shared_ptr<PatriciaTree<IntegerType, Value>> t) {
  if (s == t) {
    // This conditions allows the leq to run in sublinear time when comparing
    // Patricia trees that share some structure.
    return true;
  }
  if (s == nullptr) {
    return false;
  }
  if (t == nullptr) {
    return true;
  }
  if (s->is_leaf()) {
    if (t->is_branch()) {
      return false;
    }
    auto s_leaf =
        std::static_pointer_cast<PatriciaTreeLeaf<IntegerType, Value>>(s);
    auto t_leaf =
        std::static_pointer_cast<PatriciaTreeLeaf<IntegerType, Value>>(t);
    return s_leaf->value().leq(t_leaf->value());
  }
  if (t->is_leaf()) {
    auto leaf =
        std::static_pointer_cast<PatriciaTreeLeaf<IntegerType, Value>>(t);
    auto* s_value = find_value(leaf->key(), s);
    if (s_value == nullptr) {
      return false;
    }
    return s_value->leq(leaf->value());
  }
  auto s_branch =
      std::static_pointer_cast<PatriciaTreeBranch<IntegerType, Value>>(s);
  auto t_branch =
      std::static_pointer_cast<PatriciaTreeBranch<IntegerType, Value>>(t);
  IntegerType m = s_branch->branching_bit();
  IntegerType n = t_branch->branching_bit();
  IntegerType p = s_branch->prefix();
  IntegerType q = t_branch->prefix();
  auto s0 = s_branch->left_tree();
  auto s1 = s_branch->right_tree();
  auto t0 = t_branch->left_tree();
  auto t1 = t_branch->right_tree();
  if (m == n && p == q) {
    return leq(s_branch->left_tree(), t_branch->left_tree()) &&
           leq(s_branch->right_tree(), t_branch->right_tree());
  }
  if (m < n && match_prefix(q, p, m)) {
    return leq(is_zero_bit(q, m) ? s0 : s1, t);
  }
  // Otherwise, tree t contains bindings to (non-Top) values that are not bound
  // in s (and therefore implicitly bound to Top).
  return false;
}

// A Patricia tree is a canonical representation of the set of keys it contains.
// Hence, set equality is equivalent to structural equality of Patricia trees.
template <typename IntegerType, typename Value>
inline bool equals(std::shared_ptr<PatriciaTree<IntegerType, Value>> tree1,
                   std::shared_ptr<PatriciaTree<IntegerType, Value>> tree2) {
  if (tree1 == tree2) {
    // This conditions allows the equality test to run in sublinear time when
    // comparing Patricia trees that share some structure.
    return true;
  }
  if (tree1 == nullptr) {
    return tree2 == nullptr;
  }
  if (tree2 == nullptr) {
    return false;
  }
  if (tree1->is_leaf()) {
    if (tree2->is_branch()) {
      return false;
    }
    auto leaf1 =
        std::static_pointer_cast<PatriciaTreeLeaf<IntegerType, Value>>(tree1);
    auto leaf2 =
        std::static_pointer_cast<PatriciaTreeLeaf<IntegerType, Value>>(tree2);
    return leaf1->key() == leaf2->key() &&
           leaf1->value().equals(leaf2->value());
  }
  if (tree2->is_leaf()) {
    return false;
  }
  auto branch1 =
      std::static_pointer_cast<PatriciaTreeBranch<IntegerType, Value>>(tree1);
  auto branch2 =
      std::static_pointer_cast<PatriciaTreeBranch<IntegerType, Value>>(tree2);
  return branch1->prefix() == branch2->prefix() &&
         branch1->branching_bit() == branch2->branching_bit() &&
         equals(branch1->left_tree(), branch2->left_tree()) &&
         equals(branch1->right_tree(), branch2->right_tree());
}

// Finds the value corresponding to :key in the tree and replaces its bound
// value with combine(bound_value, :value). Note that the existing value is
// always the first parameter to :combine and the new value is the second.
template <typename IntegerType, typename Value>
inline std::shared_ptr<PatriciaTree<IntegerType, Value>> update(
    const ptmap_impl::CombiningFunction<Value>& combine,
    IntegerType key,
    const Value& value,
    std::shared_ptr<PatriciaTree<IntegerType, Value>> tree) {
  if (tree == nullptr) {
    return combine_new_leaf(combine, key, value);
  }
  if (tree->is_leaf()) {
    auto leaf =
        std::static_pointer_cast<PatriciaTreeLeaf<IntegerType, Value>>(tree);
    if (key == leaf->key()) {
      return combine_leaf(combine, value, leaf);
    }
    auto new_leaf = combine_new_leaf(combine, key, value);
    if (new_leaf == nullptr) {
      return leaf;
    }
    return join<IntegerType, Value>(key, new_leaf, leaf->key(), leaf);
  }
  auto branch =
      std::static_pointer_cast<PatriciaTreeBranch<IntegerType, Value>>(tree);
  if (match_prefix(key, branch->prefix(), branch->branching_bit())) {
    if (is_zero_bit(key, branch->branching_bit())) {
      auto new_left_tree = update(combine, key, value, branch->left_tree());
      if (new_left_tree == branch->left_tree()) {
        return branch;
      }
      return make_branch(branch->prefix(),
                         branch->branching_bit(),
                         new_left_tree,
                         branch->right_tree());
    } else {
      auto new_right_tree = update(combine, key, value, branch->right_tree());
      if (new_right_tree == branch->right_tree()) {
        return branch;
      }
      return make_branch(branch->prefix(),
                         branch->branching_bit(),
                         branch->left_tree(),
                         new_right_tree);
    }
  }
  auto new_leaf = combine_new_leaf(combine, key, value);
  if (new_leaf == nullptr) {
    return branch;
  }
  return join<IntegerType, Value>(key, new_leaf, branch->prefix(), branch);
}

// We keep the notations of the paper so as to make the implementation easier
// to follow.
template <typename IntegerType, typename Value>
inline std::shared_ptr<PatriciaTree<IntegerType, Value>> merge(
    const ptmap_impl::CombiningFunction<Value>& combine,
    std::shared_ptr<PatriciaTree<IntegerType, Value>> s,
    std::shared_ptr<PatriciaTree<IntegerType, Value>> t) {
  if (s == t) {
    // This conditional is what allows the union operation to complete in
    // sublinear time when the operands share some structure.
    return s;
  }
  if (s == nullptr) {
    return t;
  }
  if (t == nullptr) {
    return s;
  }
  if (s->is_leaf()) {
    auto leaf =
        std::static_pointer_cast<PatriciaTreeLeaf<IntegerType, Value>>(s);
    return update(combine, leaf->key(), leaf->value(), t);
  }
  if (t->is_leaf()) {
    auto leaf =
        std::static_pointer_cast<PatriciaTreeLeaf<IntegerType, Value>>(t);
    return update(combine, leaf->key(), leaf->value(), s);
  }
  auto s_branch =
      std::static_pointer_cast<PatriciaTreeBranch<IntegerType, Value>>(s);
  auto t_branch =
      std::static_pointer_cast<PatriciaTreeBranch<IntegerType, Value>>(t);
  IntegerType m = s_branch->branching_bit();
  IntegerType n = t_branch->branching_bit();
  IntegerType p = s_branch->prefix();
  IntegerType q = t_branch->prefix();
  auto s0 = s_branch->left_tree();
  auto s1 = s_branch->right_tree();
  auto t0 = t_branch->left_tree();
  auto t1 = t_branch->right_tree();
  if (m == n && p == q) {
    // The two trees have the same prefix. We just merge the subtrees.
    auto new_left = merge(combine, s0, t0);
    auto new_right = merge(combine, s1, t1);
    if (new_left == s0 && new_right == s1) {
      return s;
    }
    if (new_left == t0 && new_right == t1) {
      return t;
    }
    return std::make_shared<PatriciaTreeBranch<IntegerType, Value>>(
        p, m, new_left, new_right);
  }
  if (m < n && match_prefix(q, p, m)) {
    // q contains p. Merge t with a subtree of s.
    if (is_zero_bit(q, m)) {
      auto new_left = merge(combine, s0, t);
      if (s0 == new_left) {
        return s;
      }
      return std::make_shared<PatriciaTreeBranch<IntegerType, Value>>(
          p, m, new_left, s1);
    } else {
      auto new_right = merge(combine, s1, t);
      if (s1 == new_right) {
        return s;
      }
      return std::make_shared<PatriciaTreeBranch<IntegerType, Value>>(
          p, m, s0, new_right);
    }
  }
  if (m > n && match_prefix(p, q, n)) {
    // p contains q. Merge s with a subtree of t.
    if (is_zero_bit(p, n)) {
      auto new_left = merge(combine, s, t0);
      if (t0 == new_left) {
        return t;
      }
      return std::make_shared<PatriciaTreeBranch<IntegerType, Value>>(
          q, n, new_left, t1);
    } else {
      auto new_right = merge(combine, s, t1);
      if (t1 == new_right) {
        return t;
      }
      return std::make_shared<PatriciaTreeBranch<IntegerType, Value>>(
          q, n, t0, new_right);
    }
  }
  // The prefixes disagree.
  return join(p, s, q, t);
}

// Combine :value with the value in :leaf.
template <typename IntegerType, typename Value>
inline std::shared_ptr<PatriciaTree<IntegerType, Value>> combine_leaf(
    const ptmap_impl::CombiningFunction<Value>& combine,
    const Value& value,
    std::shared_ptr<PatriciaTreeLeaf<IntegerType, Value>> leaf) {
  auto combined_value = combine(leaf->value(), value);
  if (combined_value.is_top()) {
    return nullptr;
  }
  if (!combined_value.equals(leaf->value())) {
    return std::make_shared<PatriciaTreeLeaf<IntegerType, Value>>(
        leaf->key(), combined_value);
  }
  return leaf;
}

// Create a new leaf with a Top value and combine :value into it.
template <typename IntegerType, typename Value>
inline std::shared_ptr<PatriciaTree<IntegerType, Value>> combine_new_leaf(
    const ptmap_impl::CombiningFunction<Value>& combine,
    IntegerType key,
    const Value& value) {
  auto new_leaf = std::make_shared<PatriciaTreeLeaf<IntegerType, Value>>(
      key, Value::top());
  return combine_leaf(combine, value, new_leaf);
}

template <typename IntegerType, typename Value>
inline std::shared_ptr<PatriciaTree<IntegerType, Value>> intersect(
    const ptmap_impl::CombiningFunction<Value>& combine,
    std::shared_ptr<PatriciaTree<IntegerType, Value>> s,
    std::shared_ptr<PatriciaTree<IntegerType, Value>> t) {
  if (s == t) {
    // This conditional is what allows the intersection operation to complete in
    // sublinear time when the operands share some structure.
    return s;
  }
  if (s == nullptr || t == nullptr) {
    return nullptr;
  }
  if (s->is_leaf()) {
    auto leaf =
        std::static_pointer_cast<PatriciaTreeLeaf<IntegerType, Value>>(s);
    auto* value = find_value(leaf->key(), t);
    if (value == nullptr) {
      return nullptr;
    }
    return combine_leaf(combine, *value, leaf);
  }
  if (t->is_leaf()) {
    auto leaf =
        std::static_pointer_cast<PatriciaTreeLeaf<IntegerType, Value>>(t);
    auto* value = find_value(leaf->key(), s);
    if (value == nullptr) {
      return nullptr;
    }
    return combine_leaf(combine, *value, leaf);
  }
  auto s_branch =
      std::static_pointer_cast<PatriciaTreeBranch<IntegerType, Value>>(s);
  auto t_branch =
      std::static_pointer_cast<PatriciaTreeBranch<IntegerType, Value>>(t);
  IntegerType m = s_branch->branching_bit();
  IntegerType n = t_branch->branching_bit();
  IntegerType p = s_branch->prefix();
  IntegerType q = t_branch->prefix();
  auto s0 = s_branch->left_tree();
  auto s1 = s_branch->right_tree();
  auto t0 = t_branch->left_tree();
  auto t1 = t_branch->right_tree();
  if (m == n && p == q) {
    // The two trees have the same prefix. We merge the intersection of the
    // corresponding subtrees.
    //
    // The subtrees don't have overlapping explicit values, but the combining
    // function will still be called to merge the elements in one tree with the
    // implicit Top values in the other.
    return merge<IntegerType, Value>(
        [](const Value& x, const Value& y) -> Value {
          return x.meet(y);
        },
        intersect(combine, s0, t0),
        intersect(combine, s1, t1));
  }
  if (m < n && match_prefix(q, p, m)) {
    // q contains p. Intersect t with a subtree of s.
    return intersect(combine, is_zero_bit(q, m) ? s0 : s1, t);
  }
  if (m > n && match_prefix(p, q, n)) {
    // p contains q. Intersect s with a subtree of t.
    return intersect(combine, s, is_zero_bit(p, n) ? t0 : t1);
  }
  // The prefixes disagree.
  return nullptr;
}

// The iterator basically performs a post-order traversal of the tree, pausing
// at each leaf.
template <typename Key, typename Value>
class PatriciaTreeIterator final
    : public std::iterator<std::forward_iterator_tag, Key> {
 public:
  using IntegerType = typename PatriciaTreeMap<Key, Value>::IntegerType;

  PatriciaTreeIterator() {}

  explicit PatriciaTreeIterator(
      const std::shared_ptr<PatriciaTree<IntegerType, Value>>& tree) {
    if (tree == nullptr) {
      return;
    }
    go_to_next_leaf(tree);
  }

  PatriciaTreeIterator& operator++() {
    // We disallow incrementing the end iterator.
    always_assert(m_leaf != nullptr);
    if (m_stack.empty()) {
      // This means that we were on the rightmost leaf. We've reached the end of
      // the iteration.
      m_leaf = nullptr;
      return *this;
    }
    // Otherwise, we pop out a branch from the stack and move to the leftmost
    // leaf in its right-hand subtree.
    auto branch = m_stack.top();
    m_stack.pop();
    go_to_next_leaf(branch->right_tree());
    return *this;
  }

  PatriciaTreeIterator operator++(int) {
    PatriciaTreeIterator retval = *this;
    ++(*this);
    return retval;
  }

  bool operator==(const PatriciaTreeIterator& other) const {
    // Note that there's no need to check the stack (it's just used to traverse
    // the tree).
    return m_leaf == other.m_leaf;
  }

  bool operator!=(const PatriciaTreeIterator& other) const {
    return !(*this == other);
  }

  const std::pair<Key, Value>& operator*() {
    return *reinterpret_cast<std::pair<Key, Value>*>(&m_leaf->m_pair);
  }

  const std::pair<Key, Value>* operator->() {
    return reinterpret_cast<std::pair<Key, Value>*>(&m_leaf->m_pair);
  }

 private:
  // The argument is never null.
  void go_to_next_leaf(
      const std::shared_ptr<PatriciaTree<IntegerType, Value>>& tree) {
    auto t = tree;
    // We go to the leftmost leaf, storing the branches that we're traversing
    // on the stack. By definition of a Patricia tree, a branch node always
    // has two children, hence the leftmost leaf always exists.
    while (t->is_branch()) {
      auto branch =
          std::static_pointer_cast<PatriciaTreeBranch<IntegerType, Value>>(t);
      m_stack.push(branch);
      t = branch->left_tree();
      // A branch node always has two children.
      assert(t != nullptr);
    }
    m_leaf = std::static_pointer_cast<PatriciaTreeLeaf<IntegerType, Value>>(t);
  }

  std::stack<std::shared_ptr<PatriciaTreeBranch<IntegerType, Value>>> m_stack;
  std::shared_ptr<PatriciaTreeLeaf<IntegerType, Value>> m_leaf;
};

} // namespace ptmap_impl
