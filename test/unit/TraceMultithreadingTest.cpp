/**
 * Copyright (c) 2017-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#include <gtest/gtest.h>
#include <thread>
#include <vector>

#include "Trace.h"

constexpr size_t NUM_THREADS = 10;
constexpr size_t NUM_ITERS = 10'000;

TEST(TraceMultithreadingTest, singleThread) {
  TRACE(TIME, 1, "Test output!\n");
}

TEST(TraceMultithreadingTest, multipleThreadsOnePrint) {
  std::vector<std::thread> threads;
  for (size_t idx = 0; idx < NUM_THREADS; ++idx) {
    threads.emplace_back(
        std::thread([]() { TRACE(TIME, 1, "Test output!\n"); }));
  }
  for (auto& thread : threads) {
    thread.join();
  }
}

TEST(TraceMultithreadingTest, multipleThreadsMultiplePrints) {
  std::vector<std::thread> threads;
  for (size_t idx = 0; idx < NUM_THREADS; ++idx) {
    threads.emplace_back(std::thread([]() {
      for (int j = 0; j < NUM_ITERS; ++j) {
        TRACE(TIME, 1, "Test output count %d\n", j);
      }
    }));
  }
  for (auto& thread : threads) {
    thread.join();
  }
}

TEST(TraceMultithreadingTest, localThreadContext) {
  std::vector<std::thread> threads;
  for (size_t idx = 0; idx < NUM_THREADS; ++idx) {
    threads.emplace_back(std::thread([]() {
      for (int j = 0; j < NUM_ITERS; ++j) {
        TraceContext context("thread context");
        TRACE(TIME, 1, "Test output count %d\n", j);
        TRACE(TIME, 1, "Test output count %d\n", j);
        TRACE(TIME, 1, "Test output count %d\n", j);
      }
    }));
  }
  for (auto& thread : threads) {
    thread.join();
  }
}
