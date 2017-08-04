/**
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#pragma once

#include "Pass.h"

#include <map>
#include <string>
#include <unordered_set>
#include <vector>

class AnnoKill {
 public:
  using AnnoSet = std::unordered_set<DexType*>;
  using AnnoNames = std::vector<std::string>;

  struct AnnoKillStats {
    size_t annotations;
    size_t annotations_killed;
    size_t class_asets;
    size_t class_asets_cleared;
    size_t method_asets;
    size_t method_asets_cleared;
    size_t method_param_asets;
    size_t method_param_asets_cleared;
    size_t field_asets;
    size_t field_asets_cleared;
    size_t visibility_build_count;
    size_t visibility_runtime_count;
    size_t visibility_system_count;

    AnnoKillStats() { memset(this, 0, sizeof(AnnoKillStats)); }
  };

  AnnoKill(Scope& scope, const AnnoNames& keep, const AnnoNames& kill);

  bool kill_annotations();

  AnnoKillStats get_stats() const { return m_stats; }

 private:
  // Gets the set of all annotations referenced in code
  // either by the use of SomeClass.class, as a parameter of a method
  // call or if the annotation is a field of a class.
  AnnoSet get_referenced_annos();

  // Retrieves the list of annotation instances that match the given set
  // of annotation types to be removed.
  AnnoSet get_removable_annotation_instances();

  void cleanup_aset(DexAnnotationSet* aset, const AnnoSet& referenced_annos);
  void count_annotation(const DexAnnotation* da);

  Scope& m_scope;
  AnnoSet m_kill;
  AnnoSet m_keep;
  AnnoKillStats m_stats;

  std::map<std::string, size_t> m_build_anno_map;
  std::map<std::string, size_t> m_runtime_anno_map;
  std::map<std::string, size_t> m_system_anno_map;
};

class AnnoKillPass : public Pass {
 public:
  AnnoKillPass() : Pass("AnnoKillPass") {}

  virtual void configure_pass(const PassConfig& pc) override {
    pc.get("keep_annos", {}, m_keep_annos);
    pc.get("kill_annos", {}, m_kill_annos);
  }

  virtual void run_pass(DexStoresVector&, ConfigFiles&, PassManager&) override;

 private:
  std::vector<std::string> m_keep_annos;
  std::vector<std::string> m_kill_annos;
};
