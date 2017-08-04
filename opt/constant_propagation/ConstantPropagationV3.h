/**
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#pragma once

#include "GlobalConstProp.h"
#include "LocalConstProp.h"
#include "Pass.h"

#include "Transform.h"

class ConstantPropagationPassV3 : public Pass {
 public:
  ConstantPropagationPassV3()
      : Pass("ConstantPropagationPassV3"), m_branches_removed(0) {}

  virtual void configure_pass(const PassConfig& pc) override;
  virtual void run_pass(DexStoresVector& stores,
                        ConfigFiles& cfg,
                        PassManager& mgr) override;

 private:
  std::unordered_set<DexType*> m_blacklist;
  size_t m_branches_removed;
};
