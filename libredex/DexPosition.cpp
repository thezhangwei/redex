/**
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#include <fstream>
#include <iostream>
#include <json/json.h>

#include "DexClass.h"
#include "DexPosition.h"
#include "DexUtil.h"

DexPosition::DexPosition(uint32_t line) : line(line), parent(nullptr) {}

void DexPosition::bind(DexMethod* method_, DexString* file_) {
  always_assert(method_ != nullptr);
  this->method = method_;
  this->file = file_;
}

void RealPositionMapper::register_position(DexPosition* pos) {
  m_pos_line_map[pos] = -1;
}

uint32_t RealPositionMapper::get_line(DexPosition* pos) {
  return m_pos_line_map.at(pos) + 1;
}

uint32_t RealPositionMapper::position_to_line(DexPosition* pos) {
  auto idx = m_positions.size();
  m_positions.emplace_back(pos);
  m_pos_line_map[pos] = idx;
  return get_line(pos);
}

void RealPositionMapper::write_map() {
  if (m_filename != "") {
    write_map_v1();
  }
  if (m_filename_v2 != "") {
    write_map_v2();
  }
}

void RealPositionMapper::write_map_v1() {
  // to ensure that the line numbers in the Dex are as compact as possible,
  // we put the emitted positions at the start of the list and rest at the end
  for (auto item : m_pos_line_map) {
    auto line = item.second;
    if (line == -1) {
      auto idx = m_positions.size();
      m_positions.emplace_back(item.first);
      m_pos_line_map[item.first] = idx;
    }
  }
  /*
   * Map file layout:
   * 0xfaceb000 (magic number)
   * version (4 bytes)
   * string_pool_size (4 bytes)
   * string_pool[string_pool_size]
   * positions_size (4 bytes)
   * positions[positions_size]
   *
   * Each member of the string pool is encoded as follows:
   * string_length (4 bytes)
   * char[string_length]
   */
  std::stringstream pos_out;
  std::unordered_map<DexString*, uint32_t> string_ids;
  std::vector<DexString*> string_pool;

  for (auto pos : m_positions) {
    uint32_t parent_line = 0;
    try {
      parent_line = pos->parent == nullptr ? 0 : get_line(pos->parent);
    } catch (std::out_of_range& e) {
      std::cerr << "Parent position " << show(pos->parent) << " of "
                << show(pos) << " was not registered" << std::endl;
    }
    if (string_ids.find(pos->file) == string_ids.end()) {
      string_ids[pos->file] = string_pool.size();
      string_pool.push_back(pos->file);
    }
    auto string_id = string_ids[pos->file];
    pos_out.write((const char*)&string_id, sizeof(string_id));
    pos_out.write((const char*)&pos->line, sizeof(pos->line));
    pos_out.write((const char*)&parent_line, sizeof(parent_line));
  }

  std::ofstream ofs(m_filename.c_str(),
                    std::ofstream::out | std::ofstream::trunc);
  uint32_t magic = 0xfaceb000; // serves as endianess check
  ofs.write((const char*)&magic, sizeof(magic));
  uint32_t version = 1;
  ofs.write((const char*)&version, sizeof(version));
  uint32_t spool_count = string_pool.size();
  ofs.write((const char*)&spool_count, sizeof(spool_count));
  for (auto s : string_pool) {
    uint32_t ssize = s->size();
    ofs.write((const char*)&ssize, sizeof(ssize));
    ofs << s->c_str();
  }
  uint32_t pos_count = m_positions.size();
  ofs.write((const char*)&pos_count, sizeof(pos_count));
  ofs << pos_out.str();
}

void RealPositionMapper::write_map_v2() {
  // to ensure that the line numbers in the Dex are as compact as possible,
  // we put the emitted positions at the start of the list and rest at the end
  for (auto item : m_pos_line_map) {
    auto line = item.second;
    if (line == -1) {
      auto idx = m_positions.size();
      m_positions.emplace_back(item.first);
      m_pos_line_map[item.first] = idx;
    }
  }
  /*
   * Map file layout:
   * 0xfaceb000 (magic number)
   * version (4 bytes)
   * string_pool_size (4 bytes)
   * string_pool[string_pool_size]
   * positions_size (4 bytes)
   * positions[positions_size]
   *
   * Each member of the string pool is encoded as follows:
   * string_length (4 bytes)
   * char[string_length]
   */
  std::stringstream pos_out;
  std::unordered_map<std::string, uint32_t> string_ids;
  std::vector<std::string> string_pool;

  auto id_of_string = [&](const std::string& s) -> uint32_t {
    if (string_ids.find(s) == string_ids.end()) {
      string_ids[s] = string_pool.size();
      string_pool.push_back(s);
    }
    return string_ids.at(s);
  };

  for (auto pos : m_positions) {
    uint32_t parent_line = 0;
    try {
      parent_line = pos->parent == nullptr ? 0 : get_line(pos->parent);
    } catch (std::out_of_range& e) {
      std::cerr << "Parent position " << show(pos->parent) << " of "
                << show(pos) << " was not registered" << std::endl;
    }
    // of the form "class_name.method_name:(arg_types)return_type"
    auto full_method_name = pos->method->get_deobfuscated_name();
    // strip out the args and return type
    auto qualified_method_name =
      full_method_name.substr(0, full_method_name.find(":"));
    auto class_name = JavaNameUtil::internal_to_external(
        qualified_method_name.substr(0, qualified_method_name.rfind(".")));
    auto method_name =
        qualified_method_name.substr(qualified_method_name.rfind(".") + 1);
    auto class_id = id_of_string(class_name);
    auto method_id = id_of_string(method_name);
    auto file_id = id_of_string(pos->file->c_str());
    pos_out.write((const char*)&class_id, sizeof(class_id));
    pos_out.write((const char*)&method_id, sizeof(method_id));
    pos_out.write((const char*)&file_id, sizeof(file_id));
    pos_out.write((const char*)&pos->line, sizeof(pos->line));
    pos_out.write((const char*)&parent_line, sizeof(parent_line));
  }

  std::ofstream ofs(m_filename_v2.c_str(),
                    std::ofstream::out | std::ofstream::trunc);
  uint32_t magic = 0xfaceb000; // serves as endianess check
  ofs.write((const char*)&magic, sizeof(magic));
  uint32_t version = 2;
  ofs.write((const char*)&version, sizeof(version));
  uint32_t spool_count = string_pool.size();
  ofs.write((const char*)&spool_count, sizeof(spool_count));
  for (auto s : string_pool) {
    uint32_t ssize = s.size();
    ofs.write((const char*)&ssize, sizeof(ssize));
    ofs << s;
  }
  uint32_t pos_count = m_positions.size();
  ofs.write((const char*)&pos_count, sizeof(pos_count));
  ofs << pos_out.str();
}

PositionMapper* PositionMapper::make(const std::string& map_filename,
                                     const std::string& map_filename_v2) {
  if (map_filename == "" && map_filename_v2 == "") {
    // If no path is provided for the map, just pass the original line numbers
    // through to the output. This does mean that the line numbers will be
    // incorrect for inlined code.
    return new NoopPositionMapper();
  } else {
    return new RealPositionMapper(map_filename, map_filename_v2);
  }
}

DexString* RealPositionMapper::get_source_file(const DexClass*) {
  // Note: When remapping line numbers, we don't simply emit DEX_NO_INDEX for
  // the source_file_idx because that would cause stack traces to print
  // "at com.foo.bar (Unknown source)" even when line number data is
  // available. So we make the source_file_idx point at an empty string
  // instead.
  return DexString::make_string("");
}

DexString* NoopPositionMapper::get_source_file(const DexClass* clz) {
  return clz->get_source_file();
}


uint32_t NoopPositionMapper::get_next_line(const DexDebugItem* dbg) {
  // XXX: we could be smarter and look for the first position entry
  return dbg->get_line_start();
}
