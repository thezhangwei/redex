/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <boost/functional/hash.hpp>

#include "FiniteAbstractDomain.h"

namespace nullness {

enum Nullness {
  BOTTOM,
  IS_NULL,
  NOT_NULL,
  TOP // Nullable
};

using NullnessLattice =
    sparta::BitVectorLattice<Nullness, 4, boost::hash<Nullness>>;

/*
 *         TOP (Nullable)
 *        /      \
 *      NULL    NOT_NULL
 *        \      /
 *         BOTTOM
 */
extern NullnessLattice lattice;

/*
 * Nullness domain
 *
 * We can use the nullness domain to track the nullness of a given reference
 * type value.
 */
using NullnessDomain = sparta::FiniteAbstractDomain<Nullness,
                                                    NullnessLattice,
                                                    NullnessLattice::Encoding,
                                                    &lattice>;

std::ostream& operator<<(std::ostream& output, const Nullness& nullness);

} // namespace nullness
