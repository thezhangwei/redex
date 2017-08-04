/**
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#pragma once

#include <cstdlib>
#include <cstring>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <sstream>
#include <string>

#include "DexAccess.h"
#include "DexAnnotation.h"
#include "DexDebugInstruction.h"
#include "DexDefs.h"
#include "DexIdx.h"
#include "DexInstruction.h"
#include "DexPosition.h"
#include "RedexContext.h"
#include "ReferencedState.h"
#include "Show.h"
#include "Trace.h"
#include "Util.h"

/*
 * The structures defined here are literal representations
 * of what can be represented in a dex.  The main purpose of
 * the translations present here are to decouple the limitations
 * of "Idx" representation.  All of the "Idx"'s are indexes into
 * arrays of types in the dex format.  They are specific to each
 * dexfile.  So, we transform them in a way that we can load
 * multiple dexes in memory and compare them symbolically.
 *
 * In doing so, we enforce the uniqueness requirements of Idx's
 * within dexes.  There's only one DexString* with the same
 * set of characters.  Only one DexType* that has name "Foo;".
 * That simplifies the process of re-marshalling to dex after
 * we've completed whatever transforms we are going to do.
 *
 * UNIQUENESS:
 * The private constructor pattern enforces the uniqueness of
 * the pointer values of each type that has a uniqueness requirement.
 */

class DexClass;
class DexDebugInstruction;
class DexOutputIdx;
class DexString;
class DexType;
using Scope = std::vector<DexClass*>;

// Forward decls to break cycle with ProguardMap.h
std::string proguard_name(const DexType* cls);
std::string proguard_name(const DexClass* cls);
std::string proguard_name(const DexMethod* method);
std::string proguard_name(const DexField* field);

class DexString {
  friend struct RedexContext;

  std::string m_storage;
  uint32_t m_utfsize;

  // See UNIQUENESS above for the rationale for the private constructor pattern.
  DexString(std::string nstr, uint32_t utfsize) :
    m_storage(std::move(nstr)), m_utfsize(utfsize) {
  }

 public:
  uint32_t size() const { return static_cast<uint32_t>(m_storage.size()); }

  // UTF-aware length
  uint32_t length() const;

  // DexString retrieval/creation

  // If the DexString exists, return it, otherwise create it and return it.
  // See also get_string()
  static DexString* make_string(const char* nstr, uint32_t utfsize) {
    return g_redex->make_string(nstr, utfsize);
  }

  static DexString* make_string(const char* nstr) {
    return make_string(nstr, length_of_utf8_string(nstr));
  }

  static DexString* make_string(const std::string& nstr) {
    return make_string(nstr.c_str());
  }

  // Return an existing DexString or nullptr if one does not exist.
  static DexString* get_string(const char* nstr, uint32_t utfsize) {
    return g_redex->get_string(nstr, utfsize);
  }

  static DexString* get_string(const char* nstr) {
    return get_string(nstr, (uint32_t)strlen(nstr));
  }

 public:
  bool is_simple() const {
    return size() == m_utfsize;
  }

  const char* c_str() const { return m_storage.c_str(); }
  const std::string& str() const { return m_storage; }

  uint32_t get_entry_size() const {
    uint32_t len = uleb128_encoding_size(m_utfsize);
    len += size();
    len++; // NULL byte
    return len;
  }

  void encode(uint8_t* output) {
    output = write_uleb128(output, m_utfsize);
    strcpy((char*)output, c_str());
  }
};

/* Non-optimizing DexSpec compliant ordering */
inline bool compare_dexstrings(const DexString* a, const DexString* b) {
  if (a == nullptr) {
    return b != nullptr;
  } else if (b == nullptr) {
    return false;
  }
  if (a->is_simple() && b->is_simple())
    return (strcmp(a->c_str(), b->c_str()) < 0);
  /*
   * Bother, need to do code-point character-by-character
   * comparison.
   */
  const char* sa = a->c_str();
  const char* sb = b->c_str();
  /* Equivalence test first, so we don't worry about walking
   * off the end.
   */
  if (strcmp(sa, sb) == 0) return false;
  if (strlen(sa) == 0) {
    return true;
  }
  if (strlen(sb) == 0) {
    return false;
  }
  while (1) {
    uint32_t cpa = mutf8_next_code_point(sa);
    uint32_t cpb = mutf8_next_code_point(sb);
    if (cpa == cpb) {
      if (*sa == '\0') return true;
      if (*sb == '\0') return false;
      continue;
    }
    return (cpa < cpb);
  }
}

struct dexstrings_comparator {
  bool operator()(const DexString* a, const DexString* b) const {
    return compare_dexstrings(a, b);
  }
};

class DexType {
  friend struct RedexContext;

  DexString* m_name;

  // See UNIQUENESS above for the rationale for the private constructor pattern.
  DexType(DexString* dstring) {
    m_name = dstring;
  }

 public:
  // DexType retrieval/creation

  // If the DexType exists, return it, otherwise create it and return it.
  // See also get_type()
  static DexType* make_type(DexString* dstring) {
    return g_redex->make_type(dstring);
  }

  static DexType* make_type(const char* type_string) {
    return make_type(DexString::make_string(type_string));
  }

  static DexType* make_type(const char* type_string, int utfsize) {
    return make_type(DexString::make_string(type_string, utfsize));
  }

  // Return an existing DexType or nullptr if one does not exist.
  static DexType* get_type(DexString* dstring) {
    return g_redex->get_type(dstring);
  }

  static DexType* get_type(const char* type_string) {
    return get_type(DexString::get_string(type_string));
  }

  static DexType* get_type(const char* type_string, int utfsize) {
    return get_type(DexString::get_string(type_string, utfsize));
  }

 public:
  void assign_name_alias(DexString* new_name) {
    g_redex->alias_type_name(this, new_name);
  }

  DexString* get_name() const { return m_name; }
  const char* c_str() const { return get_name()->c_str(); }
};

/* Non-optimizing DexSpec compliant ordering */
inline bool compare_dextypes(const DexType* a, const DexType* b) {
  return compare_dexstrings(a->get_name(), b->get_name());
}

struct dextypes_comparator {
  bool operator()(const DexType* a, const DexType* b) const {
    return compare_dextypes(a, b);
  }
};

class DexField {
  friend struct RedexContext;

  DexFieldRef m_ref;
  /* Concrete method members */
  DexAnnotationSet* m_anno;
  DexEncodedValue* m_value; /* Static Only */
  DexAccessFlags m_access;
  bool m_concrete;
  bool m_external;
  std::string m_deobfuscated_name;

  // See UNIQUENESS above for the rationale for the private constructor pattern.
  DexField(DexType* container, DexString* name, DexType* type) {
    m_concrete = false;
    m_external = false;
    m_anno = nullptr;
    m_value = nullptr;
    m_access = static_cast<DexAccessFlags>(0);
    m_ref.cls = container;
    m_ref.name = name;
    m_ref.type = type;
  }

 public:
  ReferencedState rstate; // Tracks whether this field can be deleted or renamed

  // DexField retrieval/creation

  // If the DexField exists, return it, otherwise create it and return it.
  // See also get_field()
  static DexField* make_field(const DexType* container,
                              const DexString* name,
                              const DexType* type) {
    return g_redex->make_field(container, name, type);
  }

  // Return an existing DexField or nullptr if one does not exist.
  static DexField* get_field(const DexType* container,
                             const DexString* name,
                             const DexType* type) {
    return g_redex->get_field(container, name, type);
  }

 public:
  DexAnnotationSet* get_anno_set() const { return m_anno; }
  DexEncodedValue* get_static_value() { return m_value; }
  DexType* get_class() const { return m_ref.cls; }
  DexString* get_name() const { return m_ref.name; }
  const char* c_str() const { return get_name()->c_str(); }
  DexType* get_type() const { return m_ref.type; }
  bool is_def() const { return is_concrete() || is_external(); }
  DexAccessFlags get_access() const {
    always_assert(is_def());
    return m_access;
  }
  bool is_concrete() const { return m_concrete; }
  bool is_external() const { return m_external; }

  void set_access(DexAccessFlags access) {
    always_assert_log(!m_external,
        "Unexpected external field %s\n", SHOW(this));
    m_access = access;
  }

  void set_external() {
    always_assert_log(!m_concrete,
        "Unexpected concrete field %s\n", SHOW(this));
    m_external = true;
  }

  void set_deobfuscated_name(std::string name) { m_deobfuscated_name = name; }
  std::string get_deobfuscated_name() const {
    return is_external() ? proguard_name(this) : m_deobfuscated_name;
  }

  void make_concrete(DexAccessFlags access_flags, DexEncodedValue* v = nullptr);
  void clear_annotations() {
    delete m_anno;
    m_anno = nullptr;
  }

  void change(const DexFieldRef& ref) {
    g_redex->mutate_field(this, ref);
  }

  void attach_annotation_set(DexAnnotationSet* aset) {
    if (m_anno == nullptr && m_concrete == false) {
      m_anno = aset;
      return;
    }
    always_assert_log(false, "attach_annotation_set failed for field %s.%s\n",
                      m_ref.cls->get_name()->c_str(),
                      m_ref.name->c_str());
  }

  void gather_types_shallow(std::vector<DexType*>& ltype);
  void gather_strings_shallow(std::vector<DexString*>& lstring);

  void gather_types(std::vector<DexType*>& ltype) const;
  void gather_strings(std::vector<DexString*>& lstring) const;
  void gather_fields(std::vector<DexField*>& lfield) const;
  void gather_methods(std::vector<DexMethod*>& lmethod) const;

};

/* Non-optimizing DexSpec compliant ordering */
inline bool compare_dexfields(const DexField* a, const DexField* b) {
  if (a == nullptr) {
    return b != nullptr;
  } else if (b == nullptr) {
    return false;
  }
  if (a->get_class() != b->get_class()) {
    return compare_dextypes(a->get_class(), b->get_class());
  }
  if (a->get_name() != b->get_name()) {
    return compare_dexstrings(a->get_name(), b->get_name());
  }
  return compare_dextypes(a->get_type(), b->get_type());
}

struct dexfields_comparator {
  bool operator()(const DexField* a, const DexField* b) const {
    return compare_dexfields(a, b);
  }
};

class DexTypeList {
  friend struct RedexContext;

  std::deque<DexType*> m_list;

  // See UNIQUENESS above for the rationale for the private constructor pattern.
  DexTypeList(std::deque<DexType*>&& p) {
    m_list = std::move(p);
  }

 public:
  // DexTypeList retrieval/creation

  // If the DexTypeList exists, return it, otherwise create it and return it.
  // See also get_type_list()
  static DexTypeList* make_type_list(std::deque<DexType*>&& p) {
    return g_redex->make_type_list(std::move(p));
  }

  // Return an existing DexTypeList or nullptr if one does not exist.
  static DexTypeList* get_type_list(std::deque<DexType*>&& p) {
    return g_redex->get_type_list(std::move(p));
  }

 public:
  const std::deque<DexType*>& get_type_list() const { return m_list; }

  size_t size() const {
    return get_type_list().size();
  }

  /**
   * Returns size of the encoded typelist in bytes, input
   * pointer must be aligned.
   */
  int encode(DexOutputIdx* dodx, uint32_t* output);

  friend bool operator<(DexTypeList& a, DexTypeList& b) {
    auto ita = a.m_list.begin();
    auto itb = b.m_list.begin();
    while (1) {
      if (itb == b.m_list.end()) return false;
      if (ita == a.m_list.end()) return true;
      if (*ita != *itb) {
        const DexType* ta = *ita;
        const DexType* tb = *itb;
        return compare_dextypes(ta, tb);
      }
      ita++;
      itb++;
    }
  }

  void gather_types(std::vector<DexType*>& ltype) const;
};

inline bool compare_dextypelists(DexTypeList* a, DexTypeList* b) {
  if (a == nullptr) {
    return b != nullptr;
  } else if (b == nullptr) {
    return false;
  }
  return *a < *b;
}

struct dextypelists_comparator {
  bool operator()(DexTypeList* a, DexTypeList* b) {
    return compare_dextypelists(a, b);
  }
};

class DexProto {
  friend struct RedexContext;

  DexTypeList* m_args;
  DexType* m_rtype;
  DexString* m_shorty;

  // See UNIQUENESS above for the rationale for the private constructor pattern.
  DexProto(DexType* rtype, DexTypeList* args, DexString* shorty) {
    m_rtype = rtype;
    m_args = args;
    m_shorty = shorty;
  }

 public:
  // DexProto retrieval/creation

  // If the DexProto exists, return it, otherwise create it and return it.
  // See also get_proto()
  static DexProto* make_proto(DexType* rtype,
                              DexTypeList* args,
                              DexString* shorty) {
    return g_redex->make_proto(rtype, args, shorty);
  }

  static DexProto* make_proto(DexType* rtype, DexTypeList* args);

  // Return an existing DexProto or nullptr if one does not exist.
  static DexProto* get_proto(DexType* rtype, DexTypeList* args) {
    return g_redex->get_proto(rtype, args);
  }

 public:
  DexType* get_rtype() const { return m_rtype; }
  DexTypeList* get_args() const { return m_args; }
  DexString* get_shorty() const { return m_shorty; }
  bool is_void() const { return get_rtype() == DexType::make_type("V"); }

  void gather_types(std::vector<DexType*>& ltype) const;
  void gather_strings(std::vector<DexString*>& lstring) const;
};

/* Non-optimizing DexSpec compliant ordering */
inline bool compare_dexprotos(const DexProto* a, const DexProto* b) {
  if (a == nullptr) {
    return b != nullptr;
  } else if (b == nullptr) {
    return false;
  }
  if (a->get_rtype() != b->get_rtype()) {
    return compare_dextypes(a->get_rtype(), b->get_rtype());
  }
  return (*(a->get_args()) < *(b->get_args()));
}

struct dexprotos_comparator {
  bool operator()(const DexProto* a, const DexProto* b) const {
    return compare_dexprotos(a, b);
  }
};

/*
 * Dex files encode debug information as a series of opcodes. Internally, we
 * convert the opcodes that delta-encode position into absolute DexPositions.
 * The other opcodes get passed directly through.
 */
enum class DexDebugEntryType { Instruction, Position };

struct DexDebugEntry final {
  DexDebugEntryType type;
  uint32_t addr;
  union {
    std::unique_ptr<DexPosition> pos;
    std::unique_ptr<DexDebugInstruction> insn;
  };
  DexDebugEntry(uint32_t addr, std::unique_ptr<DexPosition> pos)
      : type(DexDebugEntryType::Position), addr(addr), pos(std::move(pos)) {}
  DexDebugEntry(uint32_t addr, std::unique_ptr<DexDebugInstruction> insn)
      : type(DexDebugEntryType::Instruction),
        addr(addr),
        insn(std::move(insn)) {}
  // should only be copied via DexDebugItem's copy ctor, which is responsible
  // for remapping DexPositions' parent pointer
  DexDebugEntry(const DexDebugEntry&) = delete;
  DexDebugEntry(DexDebugEntry&& other);
  ~DexDebugEntry();
  void gather_strings(std::vector<DexString*>& lstring) const {
    if (type == DexDebugEntryType::Instruction) {
      insn->gather_strings(lstring);
    }
  }
  void gather_types(std::vector<DexType*>& ltype) const {
    if (type == DexDebugEntryType::Instruction) {
      insn->gather_types(ltype);
    }
  }
};

class DexDebugItem {
  uint32_t m_line_start;
  std::vector<DexString*> m_param_names;
  std::vector<DexDebugEntry> m_dbg_entries;
  DexDebugItem(DexIdx* idx, uint32_t offset);

 public:
  DexDebugItem(const DexDebugItem&);
  static std::unique_ptr<DexDebugItem> get_dex_debug(DexIdx* idx,
                                                     uint32_t offset);

 public:
  std::vector<DexDebugEntry>& get_entries() { return m_dbg_entries; }
  void set_entries(std::vector<DexDebugEntry> dbg_entries) {
    m_dbg_entries.swap(dbg_entries);
  }
  uint32_t get_line_start() const { return m_line_start; }
  std::vector<DexString*>& get_param_names() { return m_param_names; }
  void remove_parameter_names() { m_param_names.clear(); };
  void bind_positions(DexMethod* method, DexString* file);

  /* Returns number of bytes encoded, *output has no alignment requirements */
  int encode(DexOutputIdx* dodx, PositionMapper* pos_mapper, uint8_t* output);

  void gather_types(std::vector<DexType*>& ltype) const;
  void gather_strings(std::vector<DexString*>& lstring) const;
};

typedef std::vector<std::pair<DexType*, uint32_t>> DexCatches;

struct DexTryItem {
  uint32_t m_start_addr;
  uint32_t m_insn_count;
  DexCatches m_catches;
  DexTryItem(uint32_t start_addr, uint32_t insn_count):
    m_start_addr(start_addr), m_insn_count(insn_count) {}
};

class IRCode;

class DexCode {
  friend class DexMethod;

  uint16_t m_registers_size;
  uint16_t m_ins_size;
  uint16_t m_outs_size;
  std::unique_ptr<std::vector<DexInstruction*>> m_insns;
  std::vector<std::unique_ptr<DexTryItem>> m_tries;
  std::unique_ptr<DexDebugItem> m_dbg;

 public:
  static std::unique_ptr<DexCode> get_dex_code(DexIdx* idx, uint32_t offset);

  // TODO: make it private and find a better way to allow code creation
  DexCode()
     : m_registers_size(0)
     , m_ins_size(0)
     , m_outs_size(0)
     , m_insns(std::make_unique<std::vector<DexInstruction*>>())
     , m_dbg(nullptr) {}

  DexCode(const DexCode&);

  ~DexCode() {
    if (m_insns) {
      for (auto const& op : *m_insns) {
        delete op;
      }
    }
  }

 public:
  const DexDebugItem* get_debug_item() const { return m_dbg.get(); }
  void set_debug_item(std::unique_ptr<DexDebugItem> dbg) {
    m_dbg = std::move(dbg);
  }
  DexDebugItem* get_debug_item() { return m_dbg.get(); }
  std::unique_ptr<DexDebugItem> release_debug_item() {
    return std::move(m_dbg);
  }
  std::unique_ptr<std::vector<DexInstruction*>> release_instructions() {
    return std::move(m_insns);
  }
  std::vector<DexInstruction*>& reset_instructions() {
    m_insns.reset(new std::vector<DexInstruction*>());
    return *m_insns;
  }
  std::vector<DexInstruction*>& get_instructions() {
    assert(m_insns);
    return *m_insns;
  }
  const std::vector<DexInstruction*>& get_instructions() const {
    assert(m_insns);
    return *m_insns;
  }
  void set_instructions(std::vector<DexInstruction*>* insns) {
    m_insns.reset(insns);
  }
  std::vector<std::unique_ptr<DexTryItem>>& get_tries() { return m_tries; }
  const std::vector<std::unique_ptr<DexTryItem>>& get_tries() const {
    return m_tries;
  }
  uint16_t get_registers_size() const { return m_registers_size; }
  uint16_t get_ins_size() const { return m_ins_size; }
  uint16_t get_outs_size() const { return m_outs_size; }

  void set_registers_size(uint16_t sz) { m_registers_size = sz; }
  void set_ins_size(uint16_t sz) { m_ins_size = sz; }
  void set_outs_size(uint16_t sz) { m_outs_size = sz; }

  /*
   * Returns number of bytes in encoded output, passed in
   * pointer must be aligned.  Does not encode debugitem,
   * that must be done later.
   */
  int encode(DexOutputIdx* dodx, uint32_t* output);

  /*
   * Returns the number of 2-byte code units needed to encode all the
   * instructions.
   */
  uint32_t size() const;

  friend std::string show(const DexCode*);
};

class DexMethod {
  friend struct RedexContext;

  DexMethodRef m_ref;
  /* Concrete method members */
  DexAnnotationSet* m_anno;
  std::unique_ptr<DexCode> m_dex_code;
  std::unique_ptr<IRCode> m_code;
  DexAccessFlags m_access;
  bool m_concrete;
  bool m_virtual;
  bool m_external;
  ParamAnnotations m_param_anno;
  std::string m_deobfuscated_name;

  // See UNIQUENESS above for the rationale for the private constructor pattern.
  DexMethod(DexType* type, DexString* name, DexProto* proto);
  ~DexMethod();

 public:
  // Tracks whether this method can be deleted or renamed
  ReferencedState rstate;

  // DexMethod retrieval/creation

  // If the DexMethod exists, return it, otherwise create it and return it.
  // See also get_method()
  static DexMethod* make_method(DexType* type,
                                DexString* name,
                                DexProto* proto) {
    return g_redex->make_method(type, name, proto);
  }

  /**
   * Create a copy of method `that`
   */
  static DexMethod* make_method_from(DexMethod* that,
                                     DexType* target_cls,
                                     DexString* name);

  /**
   * This creates everything along the chain of Dex<Member>, so it should
   * be used for members that either exist or would be created anyway.
   */
  static DexMethod* make_method(const char* cls_name,
                                const char* meth_name,
                                const char* rtype_str,
                                std::vector<const char*> arg_strs) {
    DexType* cls = DexType::make_type(cls_name);
    DexString* name = DexString::make_string(meth_name);
    DexType* rtype = DexType::make_type(rtype_str);
    std::deque<DexType*> args;
    for (auto const arg_str : arg_strs) {
      DexType* arg = DexType::make_type(arg_str);
      args.push_back(arg);
    }
    DexTypeList* dtl = DexTypeList::make_type_list(std::move(args));
    return make_method(cls, name, DexProto::make_proto(rtype, dtl));
  }

  /**
   * Get a method using a canonical name: Lcls;.name(args)rtype
   */
  static DexMethod* get_method(std::string canon);

  // Return an existing DexMethod or nullptr if one does not exist.
  static DexMethod* get_method(DexType* type,
                               DexString* name,
                               DexProto* proto) {
    return g_redex->get_method(type, name, proto);
  }

  static void erase_method(DexMethod* m) {
    return g_redex->erase_method(m);
  }

 public:
  const DexAnnotationSet* get_anno_set() const { return m_anno; }
  DexAnnotationSet* get_anno_set() { return m_anno; }
  DexType* get_class() const { return m_ref.cls; }
  DexString* get_name() const { return m_ref.name; }
  const char* c_str() const { return get_name()->c_str(); }
  DexProto* get_proto() const { return m_ref.proto; }
  const DexCode* get_dex_code() const { return m_dex_code.get(); }
  DexCode* get_dex_code() { return m_dex_code.get(); }
  IRCode* get_code() { return m_code.get(); }
  const IRCode* get_code() const { return m_code.get(); }
  std::unique_ptr<IRCode> release_code();
  bool is_concrete() const { return m_concrete; }
  bool is_virtual() const { return m_virtual; }
  bool is_external() const { return m_external; }
  bool is_def() const {
    return is_concrete() || is_external();
  }
  DexAccessFlags get_access() const {
    always_assert(is_def());
    return m_access;
  }
  const ParamAnnotations* get_param_anno() const {
    if (m_param_anno.size() == 0) return nullptr;
    return &m_param_anno;
  }
  ParamAnnotations* get_param_anno() {
    if (m_param_anno.size() == 0) return nullptr;
    return &m_param_anno;
  }

  void set_deobfuscated_name(std::string name) { m_deobfuscated_name = name; }
  std::string get_deobfuscated_name() const {
    return is_external() ? proguard_name(this) : m_deobfuscated_name;
  }

  std::string get_simple_deobfuscated_name() const {
    auto full_name = get_deobfuscated_name();
    auto pos = full_name.find(".");
    always_assert(pos != std::string::npos);
    return full_name.substr(pos + 1);
  }

  void set_access(DexAccessFlags access) {
    always_assert_log(!m_external,
        "Unexpected external method %s\n", SHOW(this));
    m_access = access;
  }

  void set_virtual(bool is_virtual) {
    always_assert_log(!m_external,
        "Unexpected external method %s\n", SHOW(this));
    m_virtual = is_virtual;
  }

  void set_external() {
    always_assert_log(!m_concrete,
        "Unexpected concrete method %s\n", SHOW(this));
    m_external = true;
  }
  void set_dex_code(std::unique_ptr<DexCode> code) {
    m_dex_code = std::move(code);
  }
  void set_code(std::unique_ptr<IRCode> code);

  void make_concrete(DexAccessFlags, std::unique_ptr<DexCode>, bool is_virtual);
  void make_concrete(DexAccessFlags, std::unique_ptr<IRCode>, bool is_virtual);
  void make_concrete(DexAccessFlags access, bool is_virtual);

  void make_non_concrete();

  void change(const DexMethodRef& ref, bool rename_on_collision = false) {
    g_redex->mutate_method(this, ref, rename_on_collision);
  }
  void become_virtual();
  void clear_annotations() {
    delete m_anno;
    m_anno = nullptr;
  }
  void attach_annotation_set(DexAnnotationSet* aset) {
    if (m_anno == nullptr && m_concrete == false) {
      m_anno = aset;
      return;
    }
    always_assert_log(false, "attach_annotation_set failed for method %s\n", SHOW(this));
  }
  void attach_param_annotation_set(int paramno, DexAnnotationSet* aset) {
    if (m_param_anno.count(paramno) == 0 && m_concrete == false) {
      m_param_anno[paramno] = aset;
      return;
    }
    always_assert_log(false, "attach_param_annotation_set failed for param %d "
                      "to method %s\n",
                      paramno, SHOW(this));
  }

  void gather_types_shallow(std::vector<DexType*>& ltype) const;
  void gather_strings_shallow(std::vector<DexString*>& lstring) const;

  void gather_types(std::vector<DexType*>& ltype) const;
  void gather_fields(std::vector<DexField*>& lfield) const;
  void gather_methods(std::vector<DexMethod*>& lmethod) const;
  void gather_strings(std::vector<DexString*>& lstring) const;

  /*
   * DexCode <-> IRCode conversion methods.
   *
   * In general DexCode is only used in the load / output phases, and in tests
   * when we wish to verify that we have generated specific instructions.
   *
   * Most operations can and should use IRCode. Optimizations should never
   * have to call sync().
   */
  void balloon();
  void sync();
};

typedef std::map<DexCode*, uint32_t> dexcode_to_offset;

class DexClass {
 private:
  DexAccessFlags m_access_flags;
  DexType* m_super_class;
  DexType* m_self;
  DexTypeList* m_interfaces;
  DexString* m_source_file;
  DexAnnotationSet* m_anno;
  bool m_has_class_data;
  bool m_external;
  std::string m_deobfuscated_name;
  const std::string m_dex_location; // TODO: string interning
  std::vector<DexField*> m_sfields;
  std::vector<DexField*> m_ifields;
  std::vector<DexMethod*> m_dmethods;
  std::vector<DexMethod*> m_vmethods;

  DexClass(){};
  void load_class_annotations(DexIdx* idx, uint32_t anno_off);
  void load_class_data_item(DexIdx* idx,
                            uint32_t cdi_off,
                            DexEncodedValueArray* svalues);

  friend struct ClassCreator;

 public:
  ReferencedState rstate;
  DexClass(DexIdx* idx, dex_class_def* cdef, const std::string& dex_location);

 public:
  const std::vector<DexMethod*>& get_dmethods() const { return m_dmethods; }
  std::vector<DexMethod*>& get_dmethods() {
    always_assert_log(!m_external,
        "Unexpected external class %s\n", SHOW(m_self));
    return m_dmethods;
  }
  const std::vector<DexMethod*>& get_vmethods() const { return m_vmethods; }
  std::vector<DexMethod*>& get_vmethods() {
    always_assert_log(!m_external,
        "Unexpected external class %s\n", SHOW(m_self));
    return m_vmethods;
  }

  /* Gets the clinit method, aka the class initializer method.
   *
   * Unlike constructors, there's only ever one clinit method.
   * It takes no arguments and returns void.
   */
  DexMethod* get_clinit() const {
    for (auto meth : get_dmethods()) {
      if (strcmp(meth->get_name()->c_str(), "<clinit>") == 0) {
        return meth;
      }
    }
    return nullptr;
  }

  void add_method(DexMethod* m);
  // Removes the method from this class
  void remove_method(const DexMethod* m);
  const std::vector<DexField*>& get_sfields() const { return m_sfields; }
  std::vector<DexField*>& get_sfields() { assert(!m_external); return m_sfields; }
  const std::vector<DexField*>& get_ifields() const { return m_ifields; }
  std::vector<DexField*>& get_ifields() { assert(!m_external); return m_ifields; }
  void add_field(DexField* f);
  // Removes the field from this class
  void remove_field(const DexField* f);
  void sort_methods();
  void sort_fields();
  DexField* find_field(const char* name, const DexType* field_type) const;

  DexAnnotationDirectory* get_annotation_directory();
  DexAccessFlags get_access() const { return m_access_flags; }
  DexType* get_super_class() const { return m_super_class; }
  DexType* get_type() const { return m_self; }
  DexString* get_name() const { return m_self->get_name(); }
  const char* c_str() const { return get_name()->c_str(); }
  DexTypeList* get_interfaces() const { return m_interfaces; }
  DexString* get_source_file() const { return m_source_file; }
  bool has_class_data() const { return m_has_class_data; }
  void set_class_data(bool has_class_data) { m_has_class_data = has_class_data; }
  bool is_external() const { return m_external; }
  DexEncodedValueArray* get_static_values();
  const DexAnnotationSet* get_anno_set() const { return m_anno; }
  DexAnnotationSet* get_anno_set() { return m_anno; }
  void attach_annotation_set(DexAnnotationSet* anno) { m_anno = anno; }
  void set_source_file(DexString* source_file) { m_source_file = source_file; }
  void set_deobfuscated_name(std::string name) { m_deobfuscated_name = name; }
  std::string get_deobfuscated_name() const {
    return is_external() ? proguard_name(this) : m_deobfuscated_name;
  }
  const std::string& get_dex_location() const {
    return m_dex_location;
  }

  void set_access(DexAccessFlags access) {
    always_assert_log(!m_external,
        "Unexpected external class %s\n", SHOW(m_self));
    m_access_flags = access;
  }

  void set_interfaces(DexTypeList* intfs) {
    always_assert_log(!m_external,
        "Unexpected external class %s\n", SHOW(m_self));
    m_interfaces = intfs;
  }

  void clear_annotations() {
    delete m_anno;
    m_anno = nullptr;
  }
  /* Encodes class_data_item, returns size in bytes.  No
   * alignment requirements on *output
   */
  int encode(DexOutputIdx* dodx, dexcode_to_offset& dco, uint8_t* output);

  void gather_types(std::vector<DexType*>& ltype) const;
  void gather_strings(std::vector<DexString*>& lstring) const;
  void gather_fields(std::vector<DexField*>& lfield) const;
  void gather_methods(std::vector<DexMethod*>& lmethod) const;
};

inline bool compare_dexclasses(const DexClass* a, const DexClass* b) {
  return compare_dextypes(a->get_type(), b->get_type());
}

struct dexclasses_comparator {
  bool operator()(const DexClass* a, const DexClass* b) const {
    return compare_dexclasses(a, b);
  }
};

using DexClasses = std::vector<DexClass*>;

/* Non-optimizing DexSpec compliant ordering */
inline bool compare_dexmethods(const DexMethod* a, const DexMethod* b) {
  if (a == nullptr) {
    return b != nullptr;
  } else if (b == nullptr) {
    return false;
  }
  if (a->get_class() != b->get_class()) {
    return compare_dextypes(a->get_class(), b->get_class());
  }
  if (a->get_name() != b->get_name()) {
    return compare_dexstrings(a->get_name(), b->get_name());
  }
  return compare_dexprotos(a->get_proto(), b->get_proto());
}

struct dexmethods_comparator {
  bool operator()(const DexMethod* a, const DexMethod* b) const {
    return compare_dexmethods(a, b);
  }
};

DISALLOW_DEFAULT_COMPARATOR(DexClass)
DISALLOW_DEFAULT_COMPARATOR(DexCode)
DISALLOW_DEFAULT_COMPARATOR(DexDebugInstruction)
DISALLOW_DEFAULT_COMPARATOR(DexDebugItem)
DISALLOW_DEFAULT_COMPARATOR(DexField)
DISALLOW_DEFAULT_COMPARATOR(DexMethod)
DISALLOW_DEFAULT_COMPARATOR(DexOutputIdx)
DISALLOW_DEFAULT_COMPARATOR(DexProto)
DISALLOW_DEFAULT_COMPARATOR(DexString)
DISALLOW_DEFAULT_COMPARATOR(DexType)
DISALLOW_DEFAULT_COMPARATOR(DexTypeList)
