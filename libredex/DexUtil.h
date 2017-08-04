/**
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#pragma once

#include <algorithm>
#include <functional>
#include <vector>

#include "DexClass.h"
#include "IRInstruction.h"
#include "PassManager.h"

using TypeVector = std::vector<const DexType*>;

/**
 * Return the DexType for java.lang.Object.
 */
DexType* get_object_type();

/**
 * Return the DexType for a void (V) type.
 */
DexType* get_void_type();

/**
 * Return the DexType for a byte (B) type.
 */
DexType* get_byte_type();

/**
 * Return the DexType for a char (C) type.
 */
DexType* get_char_type();

/**
 * Return the DexType for a short (S) type.
 */
DexType* get_short_type();

/**
 * Return the DexType for an int (I) type.
 */
DexType* get_int_type();

/**
 * Return the DexType for a long (J) type.
 */
DexType* get_long_type();

/**
 * Return the DexType for a boolean (Z) type.
 */
DexType* get_boolean_type();

/**
 * Return the DexType for a float (F) type.
 */
DexType* get_float_type();

/**
 * Return the DexType for a double (D) type.
 */
DexType* get_double_type();

/**
 * Return the DexType for an java.lang.String type.
 */
DexType* get_string_type();

/**
 * Return the DexType for an java.lang.Class type.
 */
DexType* get_class_type();

/**
 * Return the DexType for an java.lang.Enum type.
 */
DexType* get_enum_type();

/**
 * Return true if the type is a primitive.
 */
bool is_primitive(const DexType* type);

/**
 * Return true if the type is either a long or a double
 */
bool is_wide_type(const DexType* type);

/**
 * Return true if method signatures (name and proto) match.
 */
inline bool signatures_match(const DexMethod* a, const DexMethod* b) {
  return a->get_name() == b->get_name() && a->get_proto() == b->get_proto();
}

/*
 * Return the shorty char for this type.
 * int -> I
 * bool -> Z
 * ... primitive etc.
 * any reference -> L
 */
char type_shorty(const DexType* type);

/**
 * Return true if the parent chain leads to known classes.
 * False if one of the parent is in a scope unknown to redex.
 */
bool has_hierarchy_in_scope(DexClass* cls);

/**
 * Basic datatypes used by bytecode.
 */
enum class DataType : uint8_t {
  Void,
  Boolean,
  Byte,
  Short,
  Char,
  Int,
  Long,
  Float,
  Double,
  Object,
  Array
};

/**
 * Return the basic datatype of given DexType.
 */
DataType type_to_datatype(const DexType* t);

/**
 * Return the DexClass that represents the DexType in input or nullptr if
 * no such DexClass exists.
 */
inline DexClass* type_class(const DexType* t) {
  return g_redex->type_class(t);
}

/**
 * Return the DexClass that represents an internal DexType or nullptr if
 * no such DexClass exists.
 */
inline DexClass* type_class_internal(const DexType* t) {
  auto dc = type_class(t);
  if (dc == nullptr || dc->is_external())
    return nullptr;
  return dc;
}

/**
 * Check whether a type can be cast to another type.
 * That is, if 'base_type' is an ancestor or an interface implemented by 'type'.
 * However the check is only within classes known to the app. So
 * you may effectively get false for a check_cast that would succeed at
 * runtime. Otherwise 'true' implies the type can cast.
 */
bool check_cast(const DexType* type, const DexType* base_type);

/**
 * Return true if the type is an array type.
 */
bool is_array(const DexType* type);

/**
 * Return true if the type is an object type (array types included).
 */
bool is_object(const DexType* type);

/**
 * Return the level of the array type, that is the number of '[' in the array.
 * int[] => [I
 * int[][] => [[I
 * etc.
 */
uint32_t get_array_level(const DexType* type);

/**
 * Return the type of a given array type or the type itself if it's not an array
 *
 * Examples:
 *   [java.lang.String -> java.lang.String
 *   java.lang.Integer -> java.lang.Integer
 */
const DexType* get_array_type_or_self(const DexType*);

/**
 * Return the type of a given array type or nullptr if the type is not
 * an array.
 */
DexType* get_array_type(const DexType*);

/**
 * Return the array type of a given type.
 */
DexType* make_array_type(const DexType*);

/**
 * True if the method is a constructor (matches the "<init>" name)
 */
bool is_init(const DexMethod* method);

/**
 * True if the method is a static constructor (matches the "<clinit>" name)
 */
bool is_clinit(const DexMethod* method);

/**
 * Whether the method is a ctor or static ctor.
 */
inline bool is_any_init(const DexMethod* method) {
  return is_init(method) || is_clinit(method);
}

/**
 * Merge the 2 visibility access flags. Return the most permissive visibility.
 */
DexAccessFlags merge_visibility(uint32_t vis1, uint32_t vis2);

/**
 * Sorts and unique-ifies the given vector.
 */
template <class T, class Cmp = std::less<T>>
void sort_unique(std::vector<T>& vec, Cmp cmp = std::less<T>()) {
  std::sort(vec.begin(), vec.end(), cmp);
  auto last = std::unique(vec.begin(), vec.end());
  vec.erase(last, vec.end());
}

/**
 * True if this instruction is passing through all the args of its enclosing
 * method.  This predicate simplifies inlining optimizations since otherwise
 * the optimization would have to re-map the input regs.  The N arguments to
 * the invoke should be the last N registers of the frame.
 */
bool passes_args_through(IRInstruction* insn,
                         const IRCode& code,
                         int ignore = 0);

/**
 * Creates a runtime exception block of instructions. This is primarily used
 * by transformations for substituting instructions which throw an exception
 * at runtime. Currently, used for substituting switch case instructions.
 */
void create_runtime_exception_block(DexString* except_str,
                                    std::vector<IRInstruction*>& block);

/**
 * Generates a Scope& object from a set of Dexes.
 *
 */
template <class T>
Scope build_class_scope(const T& dexen) {
  Scope v;
  for (auto const& classes : dexen) {
    for (auto clazz : classes) {
      v.push_back(clazz);
    }
  }
  return v;
};
Scope build_class_scope(DexStoresVector& stores);

/**
 * Posts the changes made to the Scope& object to the
 * Dexes.
 *
 */
template <class T>
void post_dexen_changes(const Scope& v, T& dexen) {
  std::unordered_set<DexClass*> clookup(v.begin(), v.end());
  for (auto& classes : dexen) {
    classes.erase(
      std::remove_if(
        classes.begin(),
        classes.end(),
        [&](DexClass* cls) {
          return !clookup.count(cls);
        }),
      classes.end());
  }
  if (debug) {
    std::unordered_set<DexClass*> dlookup;
    for (auto const& classes : dexen) {
      for (auto const& cls : classes) {
        dlookup.insert(cls);
      }
    }
    for (auto const& cls : clookup) {
      assert_log(dlookup.count(cls), "Can't add classes in post_dexen_changes");
    }
  }
};
void post_dexen_changes(const Scope& v, DexStoresVector& stores);

void load_root_dexen(
  DexStore& store,
  const std::string& dexen_dir_str,
  bool balloon = false);

/*
 * This exists because in the absence of a register allocator, we need each
 * transformation to keep the ins registers at the end of the frame. Once the
 * register allocator is switched on this function should no longer have many
 * use cases.
 */
size_t sum_param_sizes(const IRCode*);

/**
 * Determine if the given dex item has the given annotation
 *
 * @param t The dex item whose annotations we'll examine
 * @param anno_type The annotation we're looking for, expressed as DexType
 * @return true IFF dex item t is annotated with anno_type
 */
template <typename T>
bool has_anno(const T* t, const DexType* anno_type) {
  if (anno_type == nullptr) return false;
  if (t->get_anno_set() == nullptr) return false;
  for (const auto& anno : t->get_anno_set()->get_annotations()) {
    if (anno->type() == anno_type) {
      return true;
    }
  }
  return false;
}

struct dex_stats_t {
  int num_types = 0;
  int num_classes = 0;
  int num_methods = 0;
  int num_method_refs = 0;
  int num_fields = 0;
  int num_field_refs = 0;
  int num_strings = 0;
  int num_protos = 0;
  int num_static_values = 0;
  int num_annotations = 0;
  int num_type_lists = 0;
  int num_bytes = 0;
  int num_instructions = 0;
};

dex_stats_t&
  operator+=(dex_stats_t& lhs, const dex_stats_t& rhs);

namespace JavaNameUtil {

// Example: "Ljava/lang/String;" --> "java.lang.String"
inline std::string internal_to_external(const std::string& internal_name) {
  auto external_name = internal_name.substr(1, internal_name.size() - 2);
  std::replace(external_name.begin(), external_name.end(), '/', '.');
  return external_name;
}

// Example: "java.lang.String" --> "Ljava/lang/String;"
inline std::string external_to_internal(const std::string& external_name) {
  auto internal_name = "L" + external_name + ";";
  std::replace(internal_name.begin(), internal_name.end(), '.', '/');
  return internal_name;
}

}
