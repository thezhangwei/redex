/**
 * Copyright (c) 2017-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#include "dump-oat.h"
#include "memory-accounter.h"
#include "util.h"

#include <getopt.h>

#ifndef ANDROID
#include <wordexp.h>
#endif

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

#include <string>
#include <vector>

namespace {

enum class Action {
  DUMP,
  BUILD,
  NONE,
};

struct Arguments {
  Action action = Action::NONE;

  // if true, write elf file, else write bare oat file.
  bool write_elf = false;
  std::string oat_file;
  std::vector<DexInput> dex_files;

  std::string oat_version;

  bool dump_classes = false;
  bool dump_tables = false;
  bool dump_memory_usage = false;

  bool print_unverified_classes = false;

  std::string arch;

  std::string art_image_location;
};

#ifndef ANDROID
std::string expand(const std::string& path) {
  wordexp_t exp_result;
  std::string ret;
  if (wordexp(path.c_str(), &exp_result, 0) == 0) {
    ret = std::string(exp_result.we_wordv[0]);
  } else {
    ret = path;
  }
  wordfree(&exp_result);
  return ret;
}
#else
// We don't expand ~ in paths on android
std::string expand(const std::string& path) {
  return path;
}
#endif

Arguments parse_args(int argc, char* argv[]) {

  struct option options[] = {{"dump", no_argument, nullptr, 'd'},
                             {"build", no_argument, nullptr, 'b'},
                             {"write-elf", no_argument, nullptr, 'e'},
                             {"dex", required_argument, nullptr, 'x'},
                             {"dex-location", required_argument, nullptr, 'l'},
                             {"oat", required_argument, nullptr, 'o'},
                             {"oat-version", required_argument, nullptr, 'v'},
                             {"dump-classes", no_argument, nullptr, 'c'},
                             {"dump-tables", no_argument, nullptr, 't'},
                             {"dump-memory-usage", no_argument, nullptr, 'm'},
                             {"print-unverified-classes", no_argument, nullptr, 'p'},
                             {"arch", required_argument, nullptr, 'a'},
                             {"art-image-location", required_argument, nullptr, 0},
                             {nullptr, 0, nullptr, 0}};

  Arguments ret;
  std::vector<std::string> dex_files;
  std::vector<std::string> dex_locations;

  int c;
  while ((c = getopt_long(argc, argv, "cetmpdbx:l:o:v:a:", &options[0], nullptr)) !=
         -1) {
    switch (c) {
    case 'd':
      if (ret.action != Action::DUMP && ret.action != Action::NONE) {
        fprintf(stderr, "Only one of --dump, --build may be set\n");
        exit(1);
      }
      ret.action = Action::DUMP;
      break;

    case 'e':
      ret.write_elf = true;
      break;

    case 'p':
      ret.print_unverified_classes = true;
      break;

    case 'b':
      if (ret.action != Action::BUILD && ret.action != Action::NONE) {
        fprintf(stderr, "Only one of --dump, --build may be set\n");
        exit(1);
      }
      ret.action = Action::BUILD;
      break;

    case 'a':
      ret.arch = optarg;
      break;

    case 'o':
      if (!ret.oat_file.empty()) {
        fprintf(stderr, "--oat may only be set once.");
        exit(1);
      }
      ret.oat_file = expand(optarg);
      break;

    case 'x':
      dex_files.push_back(expand(optarg));
      break;

    case 'l':
      dex_locations.push_back(optarg);
      break;

    case 'c':
      ret.dump_classes = true;
      break;

    case 't':
      ret.dump_tables = true;
      break;

    case 'm':
      ret.dump_memory_usage = true;
      break;

    case 'v':
      ret.oat_version = optarg;
      break;

    case 0:
      ret.art_image_location = optarg;
      break;

    case ':':
      fprintf(stderr, "ERROR: %s requires an argument\n", argv[optind - 1]);
      exit(1);
      break;

    default:
      fprintf(stderr, "invalid arguments.\n");
      exit(1);
      break;
    }
  }

  if (ret.action != Action::DUMP && ret.print_unverified_classes) {
    fprintf(stderr, "-p/--print-unverified-classes can only be used with -d/--dump\n");
    exit(1);
  }

  if (dex_locations.size() > 0) {
    if (dex_locations.size() != dex_files.size()) {
      fprintf(stderr,
          "ERROR: number of -l arguments must match number of -x arguments.\n");
      exit(1);
    }

    foreach_pair(dex_files, dex_locations,
      [&](const std::string& file, const std::string& loc) {
        ret.dex_files.push_back(DexInput{file, loc});
      }
    );
  } else {
    for (const auto& f : dex_files) {
      ret.dex_files.push_back(DexInput{f, f});
    }
  }

  return ret;
}

int dump(const Arguments& args) {
  if (args.oat_file.empty()) {
    fprintf(stderr, "-o/--oat required\n");
    return 1;
  }

  auto file = FileHandle(fopen(args.oat_file.c_str(), "r"));
  if (file.get() == nullptr) {
    fprintf(stderr,
            "failed to open file %s %s\n",
            args.oat_file.c_str(),
            std::strerror(errno));
    return 1;
  }

  auto file_size = get_filesize(file);

  // We don't run dumping during install on device, so it is allowed to consume
  // lots
  // of memory.
  std::unique_ptr<char[]> file_contents(new char[file_size]);
  auto bytesRead = fread(file_contents.get(), 1, file_size, file.get());
  if (bytesRead != file_size) {
    fprintf(stderr,
            "Failed to read file %s (%zd)\n",
            std::strerror(errno),
            bytesRead);
    return 1;
  }

  ConstBuffer buf{file_contents.get(), file_size};
  auto ma_scope = MemoryAccounter::NewScope(buf);

  auto oatfile = OatFile::parse(buf);
  oatfile->print(args.dump_classes, args.dump_tables, args.print_unverified_classes);

  if (args.dump_memory_usage) {
    cur_ma()->print();
  }

  return oatfile->status() == OatFile::Status::PARSE_SUCCESS ? 0 : 1;
}

int build(const Arguments& args) {
  if (args.oat_file.empty()) {
    fprintf(stderr, "-o/--oat required\n");
    return 1;
  }

  if (args.dex_files.empty()) {
    fprintf(stderr, "one or more `-x dexfile` args required.\n");
    return 1;
  }

  if (args.oat_version.empty()) {
    fprintf(stderr, "-v is required. valid versions: 079\n");
    return 1;
  }

  OatFile::build(args.oat_file, args.dex_files, args.oat_version, args.arch,
      args.write_elf, args.art_image_location);

  return 0;
}

}

int main(int argc, char* argv[]) {

  auto args = parse_args(argc, argv);

  switch (args.action) {
  case Action::BUILD:
    return build(args);
  case Action::DUMP:
    return dump(args);
  case Action::NONE:
    fprintf(stderr, "Please specify --dump or --build\n");
    return 1;
  }
}
