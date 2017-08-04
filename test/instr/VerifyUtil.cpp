/**
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#include <algorithm>
#include <gtest/gtest.h>

#include "VerifyUtil.h"

DexClass* find_class_named(const DexClasses& classes, const char* name) {
  auto it = std::find_if(classes.begin(), classes.end(), [&name](DexClass* cls){
    return !strcmp(name, cls->get_name()->c_str());
  });
  return it == classes.end() ? nullptr : *it;
}

DexMethod* find_vmethod_named(const DexClass& cls, const char* name) {
  auto vmethods = cls.get_vmethods();
  auto it =
      std::find_if(vmethods.begin(), vmethods.end(), [&name](DexMethod* m) {
        return strcmp(name, m->get_name()->c_str()) == 0;
      });
  return it == vmethods.end() ? nullptr : *it;
}

DexMethod* find_dmethod_named(const DexClass& cls, const char* name) {
  auto dmethods = cls.get_dmethods();
  auto it =
      std::find_if(dmethods.begin(), dmethods.end(), [&name](DexMethod* m) {
        return strcmp(name, m->get_name()->c_str()) == 0;
      });
  return it == dmethods.end() ? nullptr : *it;
}

DexOpcodeMethod* find_invoke(const DexMethod* m, uint32_t opcode,
    const char* target_mname) {
  auto insns = m->get_dex_code()->get_instructions();
  return find_invoke(insns.begin(), insns.end(), opcode, target_mname);
}

DexOpcodeMethod* find_invoke(
    std::vector<DexInstruction*>::iterator begin,
    std::vector<DexInstruction*>::iterator end,
    uint32_t opcode,
    const char* target_mname) {
  auto it = std::find_if(begin, end,
    [opcode, target_mname](DexInstruction* insn) {
      if (insn->opcode() != opcode) {
        return false;
      }
      auto mname =
        static_cast<DexOpcodeMethod*>(insn)->get_method()->get_name();
      return mname == DexString::get_string(target_mname);
    });
  return it == end ? nullptr : static_cast<DexOpcodeMethod*>(*it);
}

IRInstruction* find_instruction(DexMethod* m, uint32_t opcode) {
  m->balloon();
  auto insns = InstructionIterable(m->get_code());
  return find_instruction(insns.begin(), insns.end(), opcode);
}

IRInstruction* find_instruction(
    InstructionIterator begin,
    InstructionIterator end,
    uint32_t opcode) {
  auto it = std::find_if(begin, end,
    [opcode](MethodItemEntry& mie) {
      IRInstruction* insn = mie.insn;
      if (mie.insn->opcode() != opcode) {
        return false;
      }
      return true;
    });
  return it == end ? nullptr : (*it).insn;
}
