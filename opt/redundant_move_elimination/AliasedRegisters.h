/**
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#pragma once

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#include <boost/graph/adjacency_list.hpp>
#pragma GCC diagnostic pop
#include <boost/optional.hpp>
#include <boost/range/iterator_range.hpp>

#include "AbstractDomain.h"
#include "DexClass.h"

typedef uint16_t Register;

struct RegisterValue {

  enum class Kind {
    REGISTER,
    CONST_LITERAL,
    CONST_STRING,
    CONST_TYPE,
    NONE,
  } kind;

  union {
    Register reg;
    int64_t literal;
    DexString* str;
    DexType* type;
    std::nullptr_t dummy;
  };

  explicit RegisterValue(Register r) : kind(Kind::REGISTER), reg(r) {}
  explicit RegisterValue(int64_t l) : kind(Kind::CONST_LITERAL), literal(l) {}
  explicit RegisterValue(DexString* s) : kind(Kind::CONST_STRING), str(s) {}
  explicit RegisterValue(DexType* t) : kind(Kind::CONST_TYPE), type(t) {}
  explicit RegisterValue() : kind(Kind::NONE), dummy() {}

  bool operator==(const RegisterValue& other) const {
    if (kind != other.kind) {
      return false;
    }

    switch (kind) {
    case Kind::REGISTER:
      return reg == other.reg;
    case Kind::CONST_LITERAL:
      return literal == other.literal;
    case Kind::CONST_STRING:
      return str == other.str;
    case Kind::CONST_TYPE:
      return type == other.type;
    case Kind::NONE:
      return true;
    default:
      always_assert_log(false, "unknown RegisterValue kind");
    }
  }

  bool operator!=(const RegisterValue& other) const {
    return !(*this == other);
  }

  static const RegisterValue& none() {
    static const RegisterValue s_none;
    return s_none;
  }
};

class AliasedRegisters final : public AbstractValue<AliasedRegisters> {
 public:
  AliasedRegisters() {}

  /**
   * declare that r1 and r2 are aliases of each other.
   * This also means r1 is aliased to all of r2's aliases and vice versa.
   */
  void make_aliased(const RegisterValue& r1, const RegisterValue& r2);

  /**
   * break every alias that any register has to `r`
   */
  void break_alias(const RegisterValue& r);

  /**
   * Including transitive aliases
   */
  bool are_aliases(const RegisterValue& r1, const RegisterValue& r2);

  boost::optional<Register> get_representative(const RegisterValue& r);

  // ---- extends AbstractValue ----

  void clear() override;

  Kind kind() const override;

  bool leq(const AliasedRegisters& other) const override;

  bool equals(const AliasedRegisters& other) const override;

  Kind join_with(const AliasedRegisters& other) override;

  Kind widen_with(const AliasedRegisters& other) override;

  Kind meet_with(const AliasedRegisters& other) override;

  Kind narrow_with(const AliasedRegisters& other) override;

 private:
  // An undirected graph where register values are vertices
  // and an edge means they are aliased.
  // Using a set for the edge container makes sure we can't have parallel edges
  using Graph = boost::adjacency_list<boost::setS, // out edge container
                                      boost::vecS, // vertex container
                                      boost::undirectedS, // undirected graph
                                      RegisterValue>; // node property
  typedef boost::graph_traits<Graph>::vertex_descriptor vertex_t;
  Graph m_graph;

  // Cache the connected component map here for speed.
  // Computed by get_representative and cleared by any change to the graph
  std::vector<int> m_conn_components;

  const boost::range_detail::integer_iterator<vertex_t> find(
      const RegisterValue& r) const;

  vertex_t find_or_create(const RegisterValue& r);

  bool path_exists(vertex_t v1, vertex_t v2) const;

  bool has_edge_between(const RegisterValue& r1, const RegisterValue& r2) const;

  // call when the graph is changed and things we computed on the old graph
  // are no longer true.
  void invalidate_cache();
};

class AliasDomain
    : public AbstractDomainScaffolding<AliasedRegisters, AliasDomain> {
 public:

  explicit AliasDomain(AbstractValueKind kind = AbstractValueKind::Top)
      : AbstractDomainScaffolding<AliasedRegisters, AliasDomain>(kind) {}

  static AliasDomain bottom() {
    return AliasDomain(AliasDomain::AbstractValueKind::Bottom);
  }

  static AliasDomain top() {
    return AliasDomain(AbstractValueKind::Top);
  }

  void update(std::function<void(AliasedRegisters&)> operation) {
    if (is_bottom()) {
      return;
    }
    operation(*this->get_value());
    normalize();
  }
};
