/**
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#include "PluginRegistry.h"
#include "Debug.h"
#include "Util.h"

PluginRegistry& PluginRegistry::get() {
  // TODO t19478845
  static PluginRegistry registry;
  return registry;
}

void PluginRegistry::register_pass(const std::string& pass, std::unique_ptr<Plugin> plugin_entry) {
  always_assert_log(
      m_registered_passes.count(pass) == 0,
      "Bailing, plugin registration for pass has already happened :: %s\n",
      pass.c_str());
  m_registered_passes[pass] = std::move(plugin_entry);
}

Plugin* PluginRegistry::pass_registry(const std::string& pass) {
  return m_registered_passes[pass].get();
}
