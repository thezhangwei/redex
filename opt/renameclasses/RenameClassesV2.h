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

#include "ClassHierarchy.h"

enum class DontRenameReasonCode {
  Annotated,
  Annotations,
  Specific,
  Packages,
  Hierarchy,
  Resources,
  ClassNameLiterals,
  Canaries,
  NativeBindings,
  SerdeRelationships,
  ClassForTypesWithReflection,
  ProguardCantRename,
};

struct DontRenameReason {
  DontRenameReasonCode code;
  std::string rule;
};

class RenameClassesPassV2 : public Pass {
 public:
  RenameClassesPassV2() : Pass("RenameClassesPassV2") {}

  virtual void configure_pass(const PassConfig& pc) override {
    pc.get("rename_annotations", false, m_rename_annotations);
    pc.get("force_rename_hierarchies", {}, m_force_rename_hierarchies);
    pc.get("dont_rename_hierarchies", {}, m_dont_rename_hierarchies);
    pc.get("dont_rename_annotated", {}, m_dont_rename_annotated);
    std::vector<std::string> dont_rename_specific;
    pc.get("dont_rename_specific", {}, dont_rename_specific);
    pc.get("dont_rename_packages", {}, m_dont_rename_packages);
    pc.get("dont_rename_types_with_reflection", {},
        m_dont_rename_types_with_reflection);
    m_dont_rename_specific.insert(dont_rename_specific.begin(),
        dont_rename_specific.end());
  }

  virtual void eval_pass(DexStoresVector& stores,
      ConfigFiles& cfg, PassManager& mgr) override;
  virtual void run_pass(DexStoresVector& stores,
      ConfigFiles& cfg, PassManager& mgr) override;

 private:

  void build_force_rename_hierarchies(
      PassManager& mgr,
      Scope& scope,
      const ClassHierarchy& class_hierarchy,
      std::unordered_map<const DexType*,
      std::string>& force_rename_hierarchies);

  void build_dont_rename_resources(PassManager& mgr,
      std::set<std::string>& dont_rename_resources);
  void build_dont_rename_class_name_literals(Scope& scope,
      std::set<std::string>& dont_rename_class_name_literals);
  void build_dont_rename_for_types_with_reflection(
      Scope& scope,
      const ProguardMap& pg_map,
      std::set<std::string>& build_dont_rename_for_specific_methods);
  void build_dont_rename_canaries(Scope& scope,
      std::set<std::string>& dont_rename_canaries);
  void build_dont_rename_hierarchies(
      PassManager& mgr,
      Scope& scope,
      const ClassHierarchy& class_hierarchy,
      std::unordered_map<const DexType*, std::string>& dont_rename_hierarchies);
  void build_dont_rename_native_bindings(Scope& scope,
      std::set<DexType*>& dont_rename_native_bindings);
  void build_dont_rename_serde_relationships(Scope& scope,
      std::set<DexType*>& dont_rename_serde_relationships);
  void build_dont_rename_annotated(
      std::set<DexType*, dextypes_comparator>& dont_rename_annotated);

  void eval_classes(Scope& scope,
                    const ClassHierarchy& class_hierarchy,
                    ConfigFiles& cfg,
                    bool rename_annotations,
                    PassManager& mgr);
  void rename_classes(Scope& scope,
                      ConfigFiles& cfg,
                      bool rename_annotations,
                      PassManager& mgr);

  // Config and rules
  bool m_rename_annotations;
  std::vector<std::string> m_force_rename_hierarchies;
  std::vector<std::string> m_dont_rename_hierarchies;
  std::vector<std::string> m_dont_rename_annotated;
  std::vector<std::string> m_dont_rename_types_with_reflection;
  std::vector<std::string> m_dont_rename_packages;
  std::unordered_set<std::string> m_dont_rename_specific;

  // Decisions we made in the eval_classes pass
  std::unordered_set<const DexClass*> m_force_rename_classes;
  std::unordered_map<const DexClass*, DontRenameReason> m_dont_rename_reasons;
};
