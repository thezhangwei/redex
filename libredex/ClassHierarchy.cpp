/**
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#include "ClassHierarchy.h"

#include "DexUtil.h"
#include "Timer.h"
#include "Resolver.h"

namespace {

inline bool match(const DexString* name,
                  const DexProto* proto,
                  const DexMethod* cls_meth) {
  return name == cls_meth->get_name() && proto == cls_meth->get_proto();
}

DexMethod* check_vmethods(const DexString* name,
                          const DexProto* proto,
                          const DexType* type) {
  const DexClass* cls = type_class(type);
  for (const auto& method : cls->get_vmethods()) {
    if (match(name, proto, method)) return method;
  }
  return nullptr;
}

DexMethod* check_dmethods(const DexString* name,
                          const DexProto* proto,
                          const DexType* type) {
  const DexClass* cls = type_class(type);
  for (const auto& method : cls->get_dmethods()) {
    if (match(name, proto, method)) return method;
  }
  return nullptr;
}

/**
 * Given a class, walks up the hierarchy and creates entries from parent to
 * children.
 * If no super is found the type is considered a child of java.lang.Object.
 * If the type is unknown (no DexClass) the walk stops and the hierarchy is
 * formed up to the first unknown type.
 */
void build_class_hierarchy(ClassHierarchy& hierarchy, const DexClass* cls) {
  // ensure an entry for the DexClass is created
  hierarchy[cls->get_type()];
  auto type = cls->get_type();
  const auto super = cls->get_super_class();
  if (super != nullptr) {
    hierarchy[super].insert(type);
  } else {
    if (type != get_object_type()) {
      // if the type in question is not java.lang.Object and it has
      // no super make it a subclass of java.lang.Object
      hierarchy[get_object_type()].insert(type);
      TRACE(VIRT, 4, "[no super on %s]\n", SHOW(type));
    }
  }
}

void build_external_hierarchy(ClassHierarchy& hierarchy) {
  g_redex->walk_type_class(
      [&](const DexType* type, const DexClass* cls) {
        if (!cls->is_external() || is_interface(cls)) return;
        build_class_hierarchy(hierarchy, cls);
      });
}

// Find all the interfaces that extend 'intf'
bool gather_intf_extenders(const DexType* extender,
                           const DexType* intf,
                           std::unordered_set<const DexType*>& intf_extenders) {
  bool extends = false;
  const DexClass* extender_cls = type_class(extender);
  if (!extender_cls) return extends;
  if (is_interface(extender_cls)) {
    for (const auto& extends_intf :
         extender_cls->get_interfaces()->get_type_list()) {
      if (extends_intf == intf ||
          gather_intf_extenders(extends_intf, intf, intf_extenders)) {
        intf_extenders.insert(extender);
        extends = true;
      }
    }
  }
  return extends;
}

void gather_intf_extenders(const Scope& scope,
                           const DexType* intf,
                           std::unordered_set<const DexType*>& intf_extenders) {
  for (const auto& cls : scope) {
    gather_intf_extenders(cls->get_type(), intf, intf_extenders);
  }
}

void build_interface_map(InterfaceMap& interfaces,
                         const ClassHierarchy& hierarchy,
                         const DexClass* current,
                         const TypeSet& implementors) {
  for (const auto& intf : current->get_interfaces()->get_type_list()) {
    interfaces[intf].insert(implementors.begin(), implementors.end());
    const auto intf_cls = type_class(intf);
    if (intf_cls == nullptr) continue;
    build_interface_map(interfaces, hierarchy, intf_cls, implementors);
  }
}

}

ClassHierarchy build_type_hierarchy(const Scope& scope) {
  Timer("Class Hierarchy");
  ClassHierarchy hierarchy;
  // build the type hierarchy
  for (const auto& cls : scope) {
    if (is_interface(cls)) continue;
    build_class_hierarchy(hierarchy, cls);
  }
  build_external_hierarchy(hierarchy);
  return hierarchy;
}

InterfaceMap build_interface_map(const ClassHierarchy& hierarchy) {
  Timer("Interface Map");
  InterfaceMap interfaces;
  // build the type hierarchy
  for (const auto& cls_it : hierarchy) {
    const auto cls = type_class(cls_it.first);
    if (cls == nullptr) continue;
    if (is_interface(cls)) continue;
    TypeSet implementors;
    get_all_children(hierarchy, cls->get_type(), implementors);
    implementors.insert(cls->get_type());
    build_interface_map(interfaces, hierarchy, cls, implementors);
  }
  return interfaces;
}

void get_all_children(
    const ClassHierarchy& hierarchy,
    const DexType* type,
    TypeSet& children) {
  const auto& direct = get_children(hierarchy, type);
  for (const auto& child : direct) {
    children.insert(child);
    get_all_children(hierarchy, child, children);
  }
}

void get_all_implementors(const Scope& scope,
                          const DexType* intf,
                          TypeSet& impls) {
  std::unordered_set<const DexType*> intf_extenders;
  gather_intf_extenders(scope, intf, intf_extenders);

  std::unordered_set<const DexType*> intfs;
  intfs.insert(intf);
  intfs.insert(intf_extenders.begin(), intf_extenders.end());

  for (auto cls : scope) {
    auto cur = cls;
    bool found = false;
    while (!found && cur != nullptr) {
      for (auto impl : cur->get_interfaces()->get_type_list()) {
        if (intfs.count(impl) > 0) {
          impls.insert(cls->get_type());
          found = true;
          break;
        }
      }
      cur = type_class(cur->get_super_class());
    }
  }
}

DexMethod* find_collision_excepting(const ClassHierarchy& ch,
                                    const DexMethod* except,
                                    const DexString* name,
                                    const DexProto* proto,
                                    const DexClass* cls,
                                    bool is_virtual,
                                    bool check_direct) {
  for (auto& method : cls->get_dmethods()) {
    if (match(name, proto, method) && method != except) return method;
  }
  for (auto& method : cls->get_vmethods()) {
    if (match(name, proto, method) && method != except) return method;
  }
  if (!is_virtual) return nullptr;

  auto super = type_class(cls->get_super_class());
  if (super) {
    auto method = resolve_virtual(super, name, proto);
    if (method && method != except) return method;
  }

  TypeSet children;
  get_all_children(ch, cls->get_type(), children);
  for (const auto& child : children) {
    auto vmethod = check_vmethods(name, proto, child);
    if (vmethod && vmethod != except) return vmethod;
    if (check_direct) {
      auto dmethod = check_dmethods(name, proto, child);
      if (dmethod && dmethod != except) return dmethod;
    }
  }
  return nullptr;
}
