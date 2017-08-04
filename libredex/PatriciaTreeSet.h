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
namespace pt_impl {

template <typename IntegerType>
class PatriciaTree;

template <typename IntegerType>
class PatriciaTreeLeaf;

template <typename IntegerType>
class PatriciaTreeIterator;

template <typename IntegerType>
inline bool contains(IntegerType key,
                     std::shared_ptr<PatriciaTree<IntegerType>> tree);

template <typename IntegerType>
inline bool is_subset_of(std::shared_ptr<PatriciaTree<IntegerType>> tree1,
                         std::shared_ptr<PatriciaTree<IntegerType>> tree2);

template <typename IntegerType>
inline bool equals(std::shared_ptr<PatriciaTree<IntegerType>> tree1,
                   std::shared_ptr<PatriciaTree<IntegerType>> tree2);

template <typename IntegerType>
inline std::shared_ptr<PatriciaTree<IntegerType>> insert(
    IntegerType key, std::shared_ptr<PatriciaTree<IntegerType>> tree);

template <typename IntegerType>
inline std::shared_ptr<PatriciaTree<IntegerType>> remove(
    IntegerType key, std::shared_ptr<PatriciaTree<IntegerType>> tree);

template <typename IntegerType>
inline std::shared_ptr<PatriciaTree<IntegerType>> merge(
    std::shared_ptr<PatriciaTree<IntegerType>> s,
    std::shared_ptr<PatriciaTree<IntegerType>> t);

template <typename IntegerType>
inline std::shared_ptr<PatriciaTree<IntegerType>> intersect(
    std::shared_ptr<PatriciaTree<IntegerType>> s,
    std::shared_ptr<PatriciaTree<IntegerType>> t);

} // namespace pt_impl

/*
 * This implementation of sets of integers using Patricia trees is based on the
 * following paper:
 *
 *   C. Okasaki, A. Gill. Fast Mergeable Integer Maps. In Workshop on ML (1998).
 *
 * Patricia trees are a highly efficient representation of compressed binary
 * tries. They are well suited for the situation where one has to manipulate
 * many large sets that are identical or nearly identical. In the paper,
 * Patricia trees are entirely reconstructed for each operation. We have
 * modified the original algorithms, so that subtrees that are not affected by
 * an operation remain unchanged. Since this is a functional data structure,
 * identical subtrees are therefore shared among all Patricia tries manipulated
 * by the program. This effectively achieves a form of incremental hash-consing.
 * Note that it's not perfect, since identical trees that are independently
 * constructed are not equated, but it's a lot more efficient than regular
 * hash-consing. This data structure doesn't just reduce the memory footprint of
 * sets, it also significantly speeds up certain operations. Whenever two sets
 * represented as Patricia trees share some structure, their union and
 * intersection can often be computed in sublinear time.
 *
 * Patricia trees can only handle unsigned integers. Arbitrary objects can be
 * accommodated as long as they are represented as pointers. Our implementation
 * of Patricia-tree sets can transparently operate on either unsigned integers
 * or pointers to objects.
 */
template <typename Element>
class PatriciaTreeSet final {
 public:
  using IntegerType = typename std::
      conditional_t<std::is_pointer<Element>::value, uintptr_t, Element>;

  using iterator = pt_impl::PatriciaTreeIterator<Element>;

  PatriciaTreeSet() = default;

  explicit PatriciaTreeSet(std::initializer_list<Element> l) {
    for (Element x : l) {
      insert(encode(x));
    }
  }

  template <typename InputIterator>
  PatriciaTreeSet(InputIterator first, InputIterator last) {
    for (auto it = first; it != last; ++it) {
      insert(*it);
    }
  }

  bool is_empty() const { return m_tree == nullptr; }

  size_t size() const {
    size_t s = 0;
    for (Element UNUSED x : *this) {
      ++s;
    }
    return s;
  }

  iterator begin() const { return iterator(m_tree); }

  iterator end() const { return iterator(); }

  bool contains(Element key) const {
    if (m_tree == nullptr) {
      return false;
    }
    return pt_impl::contains<IntegerType>(encode(key), m_tree);
  }

  bool is_subset_of(const PatriciaTreeSet& other) const {
    return pt_impl::is_subset_of<IntegerType>(m_tree, other.m_tree);
  }

  bool equals(const PatriciaTreeSet& other) const {
    return pt_impl::equals<IntegerType>(m_tree, other.m_tree);
  }

  PatriciaTreeSet& insert(Element key) {
    m_tree = pt_impl::insert<IntegerType>(encode(key), m_tree);
    return *this;
  }

  PatriciaTreeSet& remove(Element key) {
    m_tree = pt_impl::remove<IntegerType>(encode(key), m_tree);
    return *this;
  }

  PatriciaTreeSet& union_with(const PatriciaTreeSet& other) {
    m_tree = pt_impl::merge<IntegerType>(m_tree, other.m_tree);
    return *this;
  }

  PatriciaTreeSet& intersection_with(const PatriciaTreeSet& other) {
    m_tree = pt_impl::intersect<IntegerType>(m_tree, other.m_tree);
    return *this;
  }

  PatriciaTreeSet get_union_with(const PatriciaTreeSet& other) const {
    auto result = *this;
    result.union_with(other);
    return result;
  }

  PatriciaTreeSet get_intersection_with(const PatriciaTreeSet& other) const {
    auto result = *this;
    result.intersection_with(other);
    return result;
  }

  void clear() { m_tree.reset(); }

  std::shared_ptr<pt_impl::PatriciaTree<IntegerType>> get_patricia_tree()
      const {
    return m_tree;
  }

 private:
  // These functions are used to handle the type conversions required when
  // manipulating sets of pointers. The first parameter is necessary to make
  // template deduction work.
  template <typename T = Element,
            typename = typename std::enable_if_t<std::is_pointer<T>::value>>
  static uintptr_t encode(Element x) {
    return reinterpret_cast<uintptr_t>(x);
  }

  template <typename T = Element,
            typename = typename std::enable_if_t<!std::is_pointer<T>::value>>
  static Element encode(Element x) {
    return x;
  }

  template <typename T = Element,
            typename = typename std::enable_if_t<std::is_pointer<T>::value>>
  static Element decode(uintptr_t x) {
    return reinterpret_cast<Element>(x);
  }

  template <typename T = Element,
            typename = typename std::enable_if_t<!std::is_pointer<T>::value>>
  static Element decode(Element x) {
    return x;
  }

  std::shared_ptr<pt_impl::PatriciaTree<IntegerType>> m_tree;

  template <typename T>
  friend std::ostream& operator<<(std::ostream&, const PatriciaTreeSet<T>&);

  template <typename T>
  friend class pt_impl::PatriciaTreeIterator;
};

template <typename Element>
inline std::ostream& operator<<(std::ostream& o,
                                const PatriciaTreeSet<Element>& s) {
  o << "{";
  for (auto it = s.begin(); it != s.end(); ++it) {
    o << PatriciaTreeSet<Element>::decode(*it);
    if (std::next(it) != s.end()) {
      o << ", ";
    }
  }
  o << "}";
  return o;
}

namespace pt_impl {

using namespace pt_util;

template <typename IntegerType>
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

// This defines an internal node of a Patricia tree. Patricia trees are
// compressed binary tries, where a path in the tree represents a sequence of
// branchings based on the value of some bits at certain positions in the binary
// decomposition of the key. The position of the bit in the key which determines
// the branching at a given node is represented in m_branching_bit as a bit mask
// (i.e., all bits are 0 except for the branching bit). All keys in the subtree
// originating from a given node share the same bit prefix (in the little endian
// ordering), which is stored in m_prefix.
template <typename IntegerType>
class PatriciaTreeBranch final : public PatriciaTree<IntegerType> {
 public:
  PatriciaTreeBranch(IntegerType prefix,
                     IntegerType branching_bit,
                     std::shared_ptr<PatriciaTree<IntegerType>> left_tree,
                     std::shared_ptr<PatriciaTree<IntegerType>> right_tree)
      : m_prefix(prefix),
        m_stacking_bit(branching_bit),
        m_left_tree(left_tree),
        m_right_tree(right_tree) {}

  bool is_leaf() const override { return false; }

  IntegerType prefix() const { return m_prefix; }

  IntegerType branching_bit() const { return m_stacking_bit; }

  std::shared_ptr<PatriciaTree<IntegerType>> left_tree() const {
    return m_left_tree;
  }

  std::shared_ptr<PatriciaTree<IntegerType>> right_tree() const {
    return m_right_tree;
  }

 private:
  IntegerType m_prefix;
  IntegerType m_stacking_bit;
  std::shared_ptr<PatriciaTree<IntegerType>> m_left_tree;
  std::shared_ptr<PatriciaTree<IntegerType>> m_right_tree;
};

template <typename IntegerType>
class PatriciaTreeLeaf final : public PatriciaTree<IntegerType> {
 public:
  explicit PatriciaTreeLeaf(IntegerType key) : m_key(key) {}

  bool is_leaf() const override { return true; }

  const IntegerType& key() const { return m_key; }

 private:
  IntegerType m_key;
};

template <typename IntegerType>
std::shared_ptr<PatriciaTreeBranch<IntegerType>> join(
    IntegerType prefix0,
    std::shared_ptr<PatriciaTree<IntegerType>> tree0,
    IntegerType prefix1,
    std::shared_ptr<PatriciaTree<IntegerType>> tree1) {
  IntegerType m = get_branching_bit(prefix0, prefix1);
  if (is_zero_bit(prefix0, m)) {
    return std::make_shared<PatriciaTreeBranch<IntegerType>>(
        mask(prefix0, m), m, tree0, tree1);
  } else {
    return std::make_shared<PatriciaTreeBranch<IntegerType>>(
        mask(prefix0, m), m, tree1, tree0);
  }
}

// This function is used by remove() to prevent the creation of branch nodes
// with only one child.
template <typename IntegerType>
std::shared_ptr<PatriciaTree<IntegerType>> make_branch(
    IntegerType prefix,
    IntegerType branching_bit,
    std::shared_ptr<PatriciaTree<IntegerType>> left_tree,
    std::shared_ptr<PatriciaTree<IntegerType>> right_tree) {
  if (left_tree == nullptr) {
    return right_tree;
  }
  if (right_tree == nullptr) {
    return left_tree;
  }
  return std::make_shared<PatriciaTreeBranch<IntegerType>>(
      prefix, branching_bit, left_tree, right_tree);
}

template <typename IntegerType>
inline bool contains(IntegerType key,
                     std::shared_ptr<PatriciaTree<IntegerType>> tree) {
  if (tree == nullptr) {
    return false;
  }
  if (tree->is_leaf()) {
    auto leaf = std::static_pointer_cast<PatriciaTreeLeaf<IntegerType>>(tree);
    return key == leaf->key();
  }
  auto branch = std::static_pointer_cast<PatriciaTreeBranch<IntegerType>>(tree);
  if (is_zero_bit(key, branch->branching_bit())) {
    return contains(key, branch->left_tree());
  } else {
    return contains(key, branch->right_tree());
  }
}

template <typename IntegerType>
inline bool is_subset_of(std::shared_ptr<PatriciaTree<IntegerType>> tree1,
                         std::shared_ptr<PatriciaTree<IntegerType>> tree2) {
  if (tree1 == tree2) {
    // This conditions allows the inclusion test to run in sublinear time
    // when comparing Patricia trees that share some structure.
    return true;
  }
  if (tree1 == nullptr) {
    return true;
  }
  if (tree2 == nullptr) {
    return false;
  }
  if (tree1->is_leaf()) {
    auto leaf = std::static_pointer_cast<PatriciaTreeLeaf<IntegerType>>(tree1);
    return contains(leaf->key(), tree2);
  }
  if (tree2->is_leaf()) {
    return false;
  }
  auto branch1 =
      std::static_pointer_cast<PatriciaTreeBranch<IntegerType>>(tree1);
  auto branch2 =
      std::static_pointer_cast<PatriciaTreeBranch<IntegerType>>(tree2);
  if (branch1->prefix() == branch2->prefix() &&
      branch1->branching_bit() == branch2->branching_bit()) {
    return is_subset_of(branch1->left_tree(), branch2->left_tree()) &&
           is_subset_of(branch1->right_tree(), branch2->right_tree());
  }
  if (branch1->branching_bit() > branch2->branching_bit() &&
      match_prefix(
          branch1->prefix(), branch2->prefix(), branch2->branching_bit())) {
    if (is_zero_bit(branch1->prefix(), branch2->branching_bit())) {
      return is_subset_of(branch1->left_tree(), branch2->left_tree()) &&
             is_subset_of(branch1->right_tree(), branch2->left_tree());
    } else {
      return is_subset_of(branch1->left_tree(), branch2->right_tree()) &&
             is_subset_of(branch1->right_tree(), branch2->right_tree());
    }
  }
  return false;
}

// A Patricia tree is a canonical representation of the set of keys it contains.
// Hence, set equality is equivalent to structural equality of Patricia trees.
template <typename IntegerType>
inline bool equals(std::shared_ptr<PatriciaTree<IntegerType>> tree1,
                   std::shared_ptr<PatriciaTree<IntegerType>> tree2) {
  if (tree1 == tree2) {
    // This conditions allows the equality test to run in sublinear time
    // when comparing Patricia trees that share some structure.
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
    auto leaf1 = std::static_pointer_cast<PatriciaTreeLeaf<IntegerType>>(tree1);
    auto leaf2 = std::static_pointer_cast<PatriciaTreeLeaf<IntegerType>>(tree2);
    return leaf1->key() == leaf2->key();
  }
  if (tree2->is_leaf()) {
    return false;
  }
  auto branch1 =
      std::static_pointer_cast<PatriciaTreeBranch<IntegerType>>(tree1);
  auto branch2 =
      std::static_pointer_cast<PatriciaTreeBranch<IntegerType>>(tree2);
  return branch1->prefix() == branch2->prefix() &&
         branch1->branching_bit() == branch2->branching_bit() &&
         equals(branch1->left_tree(), branch2->left_tree()) &&
         equals(branch1->right_tree(), branch2->right_tree());
}

template <typename IntegerType>
inline std::shared_ptr<PatriciaTree<IntegerType>> insert(
    IntegerType key, std::shared_ptr<PatriciaTree<IntegerType>> tree) {
  if (tree == nullptr) {
    return std::make_shared<PatriciaTreeLeaf<IntegerType>>(key);
  }
  if (tree->is_leaf()) {
    auto leaf = std::static_pointer_cast<PatriciaTreeLeaf<IntegerType>>(tree);
    if (key == leaf->key()) {
      return leaf;
    }
    return join<IntegerType>(
        key,
        std::make_shared<PatriciaTreeLeaf<IntegerType>>(key),
        leaf->key(),
        leaf);
  }
  auto branch = std::static_pointer_cast<PatriciaTreeBranch<IntegerType>>(tree);
  if (match_prefix(key, branch->prefix(), branch->branching_bit())) {
    if (is_zero_bit(key, branch->branching_bit())) {
      auto new_left_tree = insert(key, branch->left_tree());
      if (new_left_tree == branch->left_tree()) {
        return branch;
      }
      return std::make_shared<PatriciaTreeBranch<IntegerType>>(
          branch->prefix(),
          branch->branching_bit(),
          new_left_tree,
          branch->right_tree());
    } else {
      auto new_right_tree = insert(key, branch->right_tree());
      if (new_right_tree == branch->right_tree()) {
        return branch;
      }
      return std::make_shared<PatriciaTreeBranch<IntegerType>>(
          branch->prefix(),
          branch->branching_bit(),
          branch->left_tree(),
          new_right_tree);
    }
  }
  return join<IntegerType>(key,
                           std::make_shared<PatriciaTreeLeaf<IntegerType>>(key),
                           branch->prefix(),
                           branch);
}

template <typename IntegerType>
inline std::shared_ptr<PatriciaTree<IntegerType>> remove(
    IntegerType key, std::shared_ptr<PatriciaTree<IntegerType>> tree) {
  if (tree == nullptr) {
    return nullptr;
  }
  if (tree->is_leaf()) {
    auto leaf = std::static_pointer_cast<PatriciaTreeLeaf<IntegerType>>(tree);
    if (key == leaf->key()) {
      return nullptr;
    }
    return leaf;
  }
  auto branch = std::static_pointer_cast<PatriciaTreeBranch<IntegerType>>(tree);
  if (match_prefix(key, branch->prefix(), branch->branching_bit())) {
    if (is_zero_bit(key, branch->branching_bit())) {
      auto new_left_tree = remove(key, branch->left_tree());
      if (new_left_tree == branch->left_tree()) {
        return branch;
      }
      return make_branch<IntegerType>(branch->prefix(),
                                      branch->branching_bit(),
                                      new_left_tree,
                                      branch->right_tree());
    } else {
      auto new_right_tree = remove(key, branch->right_tree());
      if (new_right_tree == branch->right_tree()) {
        return branch;
      }
      return make_branch<IntegerType>(branch->prefix(),
                                      branch->branching_bit(),
                                      branch->left_tree(),
                                      new_right_tree);
    }
  }
  return branch;
}

// We keep the notations of the paper so as to make the implementation easier
// to follow.
template <typename IntegerType>
inline std::shared_ptr<PatriciaTree<IntegerType>> merge(
    std::shared_ptr<PatriciaTree<IntegerType>> s,
    std::shared_ptr<PatriciaTree<IntegerType>> t) {
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
    auto leaf = std::static_pointer_cast<PatriciaTreeLeaf<IntegerType>>(s);
    return insert(leaf->key(), t);
  }
  if (t->is_leaf()) {
    auto leaf = std::static_pointer_cast<PatriciaTreeLeaf<IntegerType>>(t);
    return insert(leaf->key(), s);
  }
  auto s_branch = std::static_pointer_cast<PatriciaTreeBranch<IntegerType>>(s);
  auto t_branch = std::static_pointer_cast<PatriciaTreeBranch<IntegerType>>(t);
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
    auto new_left = merge(s0, t0);
    auto new_right = merge(s1, t1);
    if (new_left == s0 && new_right == s1) {
      return s;
    }
    if (new_left == t0 && new_right == t1) {
      return t;
    }
    return std::make_shared<PatriciaTreeBranch<IntegerType>>(
        p, m, new_left, new_right);
  }
  if (m < n && match_prefix(q, p, m)) {
    // q contains p. Merge t with a subtree of s.
    if (is_zero_bit(q, m)) {
      auto new_left = merge(s0, t);
      if (s0 == new_left) {
        return s;
      }
      return std::make_shared<PatriciaTreeBranch<IntegerType>>(
          p, m, new_left, s1);
    } else {
      auto new_right = merge(s1, t);
      if (s1 == new_right) {
        return s;
      }
      return std::make_shared<PatriciaTreeBranch<IntegerType>>(
          p, m, s0, new_right);
    }
  }
  if (m > n && match_prefix(p, q, n)) {
    // p contains q. Merge s with a subtree of t.
    if (is_zero_bit(p, n)) {
      auto new_left = merge(s, t0);
      if (t0 == new_left) {
        return t;
      }
      return std::make_shared<PatriciaTreeBranch<IntegerType>>(
          q, n, new_left, t1);
    } else {
      auto new_right = merge(s, t1);
      if (t1 == new_right) {
        return t;
      }
      return std::make_shared<PatriciaTreeBranch<IntegerType>>(
          q, n, t0, new_right);
    }
  }
  // The prefixes disagree.
  return join(p, s, q, t);
}

template <typename IntegerType>
inline std::shared_ptr<PatriciaTree<IntegerType>> intersect(
    std::shared_ptr<PatriciaTree<IntegerType>> s,
    std::shared_ptr<PatriciaTree<IntegerType>> t) {
  if (s == t) {
    // This conditional is what allows the intersection operation to complete in
    // sublinear time when the operands share some structure.
    return s;
  }
  if (s == nullptr || t == nullptr) {
    return nullptr;
  }
  if (s->is_leaf()) {
    auto leaf = std::static_pointer_cast<PatriciaTreeLeaf<IntegerType>>(s);
    return contains(leaf->key(), t) ? leaf : nullptr;
  }
  if (t->is_leaf()) {
    auto leaf = std::static_pointer_cast<PatriciaTreeLeaf<IntegerType>>(t);
    return contains(leaf->key(), s) ? leaf : nullptr;
  }
  auto s_branch = std::static_pointer_cast<PatriciaTreeBranch<IntegerType>>(s);
  auto t_branch = std::static_pointer_cast<PatriciaTreeBranch<IntegerType>>(t);
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
    return merge(intersect(s0, t0), intersect(s1, t1));
  }
  if (m < n && match_prefix(q, p, m)) {
    // q contains p. Intersect t with a subtree of s.
    return intersect(is_zero_bit(q, m) ? s0 : s1, t);
  }
  if (m > n && match_prefix(p, q, n)) {
    // p contains q. Intersect s with a subtree of t.
    return intersect(s, is_zero_bit(p, n) ? t0 : t1);
  }
  // The prefixes disagree.
  return nullptr;
}

// The iterator basically performs a post-order traversal of the tree, pausing
// at each leaf.
template <typename Element>
class PatriciaTreeIterator final
    : public std::iterator<std::forward_iterator_tag, Element> {
 public:
  using IntegerType = typename PatriciaTreeSet<Element>::IntegerType;

  PatriciaTreeIterator() {}

  explicit PatriciaTreeIterator(
      const std::shared_ptr<PatriciaTree<IntegerType>>& tree) {
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

  Element operator*() {
    return PatriciaTreeSet<Element>::decode(m_leaf->key());
  }

 private:
  // The argument is never null.
  void go_to_next_leaf(const std::shared_ptr<PatriciaTree<IntegerType>>& tree) {
    auto t = tree;
    // We go to the leftmost leaf, storing the branches that we're traversing
    // on the stack. By definition of a Patricia tree, a branch node always
    // has two children, hence the leftmost leaf always exists.
    while (t->is_branch()) {
      auto branch =
          std::static_pointer_cast<PatriciaTreeBranch<IntegerType>>(t);
      m_stack.push(branch);
      t = branch->left_tree();
      // A branch node always has two children.
      assert(t != nullptr);
    }
    m_leaf = std::static_pointer_cast<PatriciaTreeLeaf<IntegerType>>(t);
  }

  std::stack<std::shared_ptr<PatriciaTreeBranch<IntegerType>>> m_stack;
  std::shared_ptr<PatriciaTreeLeaf<IntegerType>> m_leaf;
};

} // namespace pt_impl
