/**
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#include "Obfuscate.h"
#include "ObfuscateUtils.h"
#include "DexClass.h"
#include "DexUtil.h"
#include "ProguardMap.h"
#include "ReachableClasses.h"
#include "Trace.h"
#include "Transform.h"
#include "Walkers.h"
#include "Resolver.h"
#include "ClassHierarchy.h"
#include "VirtualRenamer.h"
#include <list>

namespace {

static const char* METRIC_FIELD_TOTAL = "fields_total";
static const char* METRIC_FIELD_RENAMED = "fields_renamed";
static const char* METRIC_DMETHODS_TOTAL = "dmethods_total";
static const char* METRIC_DMETHODS_RENAMED = "dmethods_renamed";
static const char* METRIC_VMETHODS_TOTAL = "vmethods_total";
static const char* METRIC_VMETHODS_RENAMED = "vmethods_renamed";

using std::unordered_set;

/* Obfuscates a list of members
 * RenamingContext - the context that we need to be able to do renaming for this
 *   member. Will not be modified and will be shared between all members in a
 *   class.
 * ObfuscationState - keeps track of the new names we're trying to assign to
 *   members, we update this to show what name we chose for a member. Also
 *   contains a set of all used names in this class because that needs to be
 *   updated every time we choose a name.
 * returns the number of find_new_name calls done
 */
template <class T, class R, class K>
int obfuscate_elems(const RenamingContext<T>& context,
    DexElemManager<T, R, K>& name_mapping) {
  int num_renames = 0;
  for (T elem : context.elems) {
    if (!context.can_rename_elem(elem) ||
        !name_mapping[elem]->should_rename()) {
      TRACE(OBFUSCATE, 4, "Ignoring member %s because we shouldn't rename it\n",
          SHOW(elem->get_name()));
      continue;
    }
    context.name_gen.find_new_name(name_mapping[elem]);
    num_renames++;
  }
  return num_renames;
}

void sort_members(std::vector<DexClass*>& classes) {
  // Sort the result because dexes have to be sorted
  for (DexClass* cls : classes) {
    cls->sort_fields();
    cls->sort_methods();
    // Debug logging
    TRACE(OBFUSCATE, 4, "Applying new names:\n  List of ifields\t");
    for (DexField* f : cls->get_ifields())
      TRACE(OBFUSCATE, 4, "%s\t", SHOW(f->get_name()));
    TRACE(OBFUSCATE, 4, "\n");
    TRACE(OBFUSCATE, 4, "  List of sfields\t");
    for (DexField* f : cls->get_sfields())
      TRACE(OBFUSCATE, 4, "%s\t", SHOW(f->get_name()));
    TRACE(OBFUSCATE, 4, "\n");
  }
  TRACE(OBFUSCATE, 3, "Finished applying new names to defs\n");
}

template<typename DexMember, typename DexMemberRef, typename K>
DexMember* find_renamable_ref(DexMember* ref,
    std::unordered_map<DexMember*, DexMember*>& ref_def_cache,
    DexElemManager<DexMember*, DexMemberRef, K>& name_mapping) {
  TRACE(OBFUSCATE, 4, "Found a ref opcode\n");
  DexMember* def = nullptr;
  auto member_itr = ref_def_cache.find(ref);
  if (member_itr != ref_def_cache.end()) {
    def = member_itr->second;
  } else {
    def = name_mapping.def_of_ref(ref);
  }
  ref_def_cache[ref] = def;
  return def;
}

void update_refs(Scope& scope, DexFieldManager& field_name_mapping,
    DexMethodManager& method_name_mapping) {
  std::unordered_map<DexField*, DexField*> f_ref_def_cache;
  std::unordered_map<DexMethod*, DexMethod*> m_ref_def_cache;
  walk_opcodes(scope,
    [](DexMethod*) { return true; },
    [&](DexMethod*, IRInstruction* instr) {
      if (instr->has_field()) {
        DexField* field_ref = instr->get_field();
        if (field_ref->is_def()) return;
        DexField* field_def =
            find_renamable_ref(field_ref, f_ref_def_cache, field_name_mapping);
        if (field_def != nullptr) {
          TRACE(OBFUSCATE, 4, "Found a ref to fixup %s", SHOW(field_ref));
          instr->set_field(field_def);
        }
      } else if (instr->has_method()) {
        DexMethod* method_ref = instr->get_method();
        if (method_ref->is_def()) return;
        DexMethod* method_def =
            find_renamable_ref(method_ref, m_ref_def_cache,
                method_name_mapping);
        if (method_def != nullptr) {
          TRACE(OBFUSCATE, 4, "Found a ref to fixup %s", SHOW(method_ref));
          instr->set_method(method_def);
        }
      }
    });
}

void get_totals(Scope& scope, RenameStats& stats) {
  for (const auto& cls : scope) {
    stats.fields_total += cls->get_ifields().size();
    stats.fields_total += cls->get_sfields().size();
    stats.vmethods_total += cls->get_vmethods().size();
    stats.dmethods_total += cls->get_dmethods().size();
  }
}

} // end namespace

void obfuscate(Scope& scope, RenameStats& stats) {
  get_totals(scope, stats);
  ClassHierarchy ch = build_type_hierarchy(scope);

  DexFieldManager field_name_manager(new_dex_field_manager());
  DexMethodManager method_name_manager = new_dex_method_manager();

  for (DexClass* cls : scope) {
    always_assert_log(!cls->is_external(),
        "Shouldn't rename members of external classes. %s", SHOW(cls));
    // Checks to short-circuit expensive name-gathering logic (code is still
    // correct w/o this, but does unnecessary work)
    bool operate_on_ifields =
        contains_renamable_elem(cls->get_ifields(), field_name_manager);
    bool operate_on_sfields =
        contains_renamable_elem(cls->get_sfields(), field_name_manager);
    bool operate_on_dmethods =
        contains_renamable_elem(cls->get_dmethods(), method_name_manager);
    if (operate_on_ifields || operate_on_sfields) {
      FieldObfuscationState f_ob_state;
      SimpleNameGenerator<DexField*> simple_name_generator(
          f_ob_state.ids_to_avoid, f_ob_state.used_ids);
      StaticFieldNameGenerator static_name_generator(
          f_ob_state.ids_to_avoid, f_ob_state.used_ids);

      TRACE(OBFUSCATE, 3, "Renaming the fields of class %s\n",
          SHOW(cls->get_name()));

      f_ob_state.populate_ids_to_avoid(cls, field_name_manager, true, ch);

      // Keep this for all public ids in the class (they shouldn't conflict)
      if (operate_on_ifields) {
        obfuscate_elems(
            FieldRenamingContext(cls->get_ifields(),
                f_ob_state.ids_to_avoid,
                simple_name_generator, false),
            field_name_manager);
      }
      if (operate_on_sfields) {
        obfuscate_elems(
            FieldRenamingContext(cls->get_sfields(),
                f_ob_state.ids_to_avoid,
                static_name_generator, false),
            field_name_manager);
      }

      // Obfu private fields
      f_ob_state.ids_to_avoid.clear();
      f_ob_state.populate_ids_to_avoid(cls, field_name_manager, false, ch);

      // Keep this for all public ids in the class (they shouldn't conflict)
      if (operate_on_ifields) {
        obfuscate_elems(
            FieldRenamingContext(cls->get_ifields(),
            f_ob_state.ids_to_avoid,
            simple_name_generator, true),
        field_name_manager);
      }
      if (operate_on_sfields) {
        obfuscate_elems(
            FieldRenamingContext(cls->get_sfields(),
                f_ob_state.ids_to_avoid,
                static_name_generator, true),
            field_name_manager);
      }

      // Make sure to bind the new names otherwise not all generators will
      // assign names to the members
      static_name_generator.bind_names();
    }

    // =========== Obfuscate Methods Below ==========
    if (operate_on_dmethods) {
      MethodObfuscationState m_ob_state;
      MethodNameGenerator simple_name_gen(m_ob_state.ids_to_avoid,
          m_ob_state.used_ids);

      TRACE(OBFUSCATE, 3, "Renaming the methods of class %s\n",
                SHOW(cls->get_name()));
      m_ob_state.populate_ids_to_avoid(cls, method_name_manager, true, ch);

      // Keep this for all public ids in the class (they shouldn't conflict)
      obfuscate_elems(
          MethodRenamingContext(cls->get_dmethods(),
              m_ob_state.ids_to_avoid,
              simple_name_gen,
              method_name_manager,
              false),
          method_name_manager);

      // Obfu private methods
      m_ob_state.ids_to_avoid.clear();
      m_ob_state.populate_ids_to_avoid(cls, method_name_manager, false, ch);

      obfuscate_elems(
          MethodRenamingContext(cls->get_dmethods(),
              m_ob_state.ids_to_avoid,
              simple_name_gen,
              method_name_manager,
              true),
          method_name_manager);
    }
  }
  field_name_manager.print_elements();
  method_name_manager.print_elements();


  TRACE(OBFUSCATE, 3, "Finished picking new names\n");

  // Update any instructions with a member that is a ref to the corresponding
  // def for any field that we are going to rename. This allows us to in-place
  // rename the field def and have that change seen everywhere.
  update_refs(scope, field_name_manager, method_name_manager);

  TRACE(OBFUSCATE, 3, "Finished transforming refs\n");

  // Apply new names, recording what we're changing
  stats.fields_renamed = field_name_manager.commit_renamings_to_dex();
  stats.dmethods_renamed = method_name_manager.commit_renamings_to_dex();

  stats.vmethods_renamed = rename_virtuals(scope);

  sort_members(scope);

  TRACE(OBFUSCATE, 1,
      "%s: %ld\n%s: %ld\n"
      "%s: %ld\n%s: %ld\n"
      "%s: %ld\n%s: %ld\n",
      METRIC_FIELD_TOTAL, stats.fields_total,
      METRIC_FIELD_RENAMED, stats.fields_renamed,
      METRIC_DMETHODS_TOTAL, stats.dmethods_total,
      METRIC_DMETHODS_RENAMED, stats.dmethods_renamed,
      METRIC_VMETHODS_TOTAL, stats.vmethods_total,
      METRIC_VMETHODS_RENAMED, stats.vmethods_renamed);
}

void ObfuscatePass::run_pass(DexStoresVector& stores,
                            ConfigFiles& cfg,
                            PassManager& mgr) {
  if (mgr.no_proguard_rules()) {
    TRACE(OBFUSCATE, 1, "ObfuscatePass not run because no ProGuard configuration was provided.");
    return;
  }
  auto scope = build_class_scope(stores);
  RenameStats stats;
  obfuscate(scope, stats);
  mgr.incr_metric(
      METRIC_FIELD_TOTAL, static_cast<int>(stats.fields_total));
  mgr.incr_metric(
      METRIC_FIELD_RENAMED, static_cast<int>(stats.fields_renamed));
  mgr.incr_metric(
      METRIC_DMETHODS_TOTAL, static_cast<int>(stats.dmethods_total));
  mgr.incr_metric(
      METRIC_DMETHODS_RENAMED, static_cast<int>(stats.dmethods_renamed));
  mgr.incr_metric(
      METRIC_VMETHODS_TOTAL, static_cast<int>(stats.vmethods_total));
  mgr.incr_metric(
      METRIC_VMETHODS_RENAMED, static_cast<int>(stats.vmethods_renamed));
}

static ObfuscatePass s_pass;
