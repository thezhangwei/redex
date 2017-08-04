/**
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#include "AccessMarking.h"

#include <unordered_map>

#include "ClassHierarchy.h"
#include "VirtualScope.h"
#include "DexUtil.h"
#include "Mutators.h"
#include "ReachableClasses.h"
#include "Resolver.h"
#include "Transform.h"
#include "Walkers.h"

namespace {
size_t mark_classes_final(const Scope& scope, const ClassHierarchy& ch) {
  size_t n_classes_finalized = 0;
  for (auto const& cls : scope) {
    if (keep(cls) || is_abstract(cls) || is_final(cls)) continue;
    auto const& children = get_children(ch, cls->get_type());
    if (children.empty()) {
      TRACE(ACCESS, 2, "Finalizing class: %s\n", SHOW(cls));
      set_final(cls);
      ++n_classes_finalized;
    }
  }
  return n_classes_finalized;
}

const DexMethod* find_override(
    const DexMethod* method, const DexClass* cls, const ClassHierarchy& ch) {
  TypeSet children;
  get_all_children(ch, cls->get_type(), children);
  for (auto const& childtype : children) {
    auto const& child = type_class(childtype);
    assert(child);
    for (auto const& child_method : child->get_vmethods()) {
      if (signatures_match(method, child_method)) {
        return child_method;
      }
    }
  }
  return nullptr;
}

size_t mark_methods_final(const Scope& scope, const ClassHierarchy& ch) {
  size_t n_methods_finalized = 0;
  for (auto const& cls : scope) {
    for (auto const& method : cls->get_vmethods()) {
      if (keep(method) || is_abstract(method) || is_final(method)) {
        continue;
      }
      if (!find_override(method, cls, ch)) {
        TRACE(ACCESS, 2, "Finalizing method: %s\n", SHOW(method));
        set_final(method);
        ++n_methods_finalized;
      }
    }
  }
  return n_methods_finalized;
}

std::vector<DexMethod*> direct_methods(const std::vector<DexClass*>& scope) {
  std::vector<DexMethod*> ret;
  for (auto cls : scope) {
    for (auto m : cls->get_dmethods()) {
      ret.push_back(m);
    }
  }
  return ret;
}

std::unordered_set<DexMethod*> find_private_methods(
  const std::vector<DexClass*>& scope,
  const std::vector<DexMethod*>& cv
) {
  std::unordered_set<DexMethod*> candidates;
  for (auto m : cv) {
    TRACE(ACCESS, 3, "Considering for privatization: %s\n", SHOW(m));
    if (!is_clinit(m) && !keep(m) && !is_abstract(m) && !is_private(m)) {
      candidates.emplace(m);
    }
  }
  walk_opcodes(
    scope,
    [](DexMethod*) { return true; },
    [&](DexMethod* caller, IRInstruction* inst) {
      if (!inst->has_method()) return;
      auto callee = inst->get_method();
      if (!callee->is_concrete()) {
        callee = resolve_method(callee, MethodSearch::Any);
        if (!callee) return;
      }
      if (callee->get_class() == caller->get_class()) {
        return;
      }
      candidates.erase(callee);
    }
  );
  return candidates;
}

void fix_call_sites_private(
  const std::vector<DexClass*>& scope,
  const std::unordered_set<DexMethod*>& privates
) {
  walk_opcodes(
    scope,
    [](DexMethod*) { return true; },
    [&](DexMethod*, IRInstruction* inst) {
      if (!inst->has_method()) return;
      auto callee = inst->get_method();
      if (!callee->is_concrete()) {
        callee = resolve_method(callee, MethodSearch::Any);
      }
      if (privates.count(callee)) {
        inst->set_method(callee);
        if (!is_static(callee)) {
          inst->set_opcode(
            is_invoke_range(inst->opcode())
            ? OPCODE_INVOKE_DIRECT_RANGE
            : OPCODE_INVOKE_DIRECT);
        }
      }
    }
  );
}

void mark_methods_private(const std::unordered_set<DexMethod*>& privates) {
  for (auto method : privates) {
    TRACE(ACCESS, 2, "Privatized method: %s\n", SHOW(method));
    auto cls = type_class(method->get_class());
    cls->remove_method(method);
    method->set_virtual(false);
    set_private(method);
    cls->add_method(method);
  }
}
}

void AccessMarkingPass::run_pass(
  DexStoresVector& stores,
  ConfigFiles& cfg,
  PassManager& pm
) {
  auto scope = build_class_scope(stores);
  ClassHierarchy ch = build_type_hierarchy(scope);
  SignatureMap sm = build_signature_map(ch);
  if (m_finalize_classes) {
    auto n_classes_final = mark_classes_final(scope, ch);
    pm.incr_metric("finalized_classes", n_classes_final);
    TRACE(ACCESS, 1, "Finalized %lu classes\n", n_classes_final);
  }
  if (m_finalize_methods) {
    auto n_methods_final = mark_methods_final(scope, ch);
    pm.incr_metric("finalized_methods", n_methods_final);
    TRACE(ACCESS, 1, "Finalized %lu methods\n", n_methods_final);
  }
  auto candidates = devirtualize(sm);
  auto dmethods = direct_methods(scope);
  candidates.insert(candidates.end(), dmethods.begin(), dmethods.end());
  if (m_privatize_methods) {
    auto privates = find_private_methods(scope, candidates);
    fix_call_sites_private(scope, privates);
    mark_methods_private(privates);
    pm.incr_metric("privatized_methods", privates.size());
    TRACE(ACCESS, 1, "Privatized %lu methods\n", privates.size());
  }
}

static AccessMarkingPass s_pass;
