/**
  * Copyright (C) 2020 Salvo Limited Hong Kong
  * 
  *  Licensed under the Apache License, Version 2.0 (the "License");
  *  you may not use this file except in compliance with the License.
  *  You may obtain a copy of the License at
  *
  *      http://www.apache.org/licenses/LICENSE-2.0
  *
  *  Unless required by applicable law or agreed to in writing, software
  *  distributed under the License is distributed on an "AS IS" BASIS,
  *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  *  See the License for the specific language governing permissions and
  *  limitations under the License.
  *
***/
#include "../include/Logging.hpp"

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MAIN
#include <boost/test/included/unit_test.hpp>

__attribute__((hot, always_inline))
  static inline int64_t epochNanos() {
    __sync_synchronize();
    timespec tp;
    clock_gettime(CLOCK_REALTIME, &tp);
    int64_t ret = int64_t(tp.tv_sec)*1000*1000*1000 + int64_t(tp.tv_nsec);
    __sync_synchronize();
    return ret;
  }

static constexpr size_t LOOP_NUM=200;
static constexpr size_t REPEATS=4;

template <bool B>
int64_t runBackgroundTest() {
  INFO("Starting test.");
  Logging::sync();
  fflush(nullptr);

  bool saveTmp = ::detail::LoggingBackgroundThread::on();
  ::detail::LoggingBackgroundThread::on() = B;
  int64_t startTime = epochNanos();
  INFO("Inside this %s", "test");
  ZZWARN("this is a warning.");
  for (size_t i = 0; i < LOOP_NUM ; ++i) {
    INFO("Hello %ld, %d", i, (unsigned int) i);
  }
  int64_t endTime = epochNanos();
  INFO("Finishing test.");
  Logging::sync();
  fflush(nullptr);
  ::detail::LoggingBackgroundThread::on() = saveTmp;
  return (endTime - startTime);
}

BOOST_AUTO_TEST_CASE( LoggingTest )
{
  Logging::logOnJunk() = true;
  int64_t backgroundTest=0, printfTest=0;
  for (size_t i = 0; i < REPEATS; ++i) {
    backgroundTest += runBackgroundTest<true>();
    printfTest += runBackgroundTest<false>();
  }
  fprintf(stdout, "Total test time: %ld (bg) vs %ld (fg, straight printf), for a savings of %ld nanos\n",
      backgroundTest, printfTest, printfTest - backgroundTest);
  fprintf(stdout, "Per loop test time: %.2f (bg) vs %.2f (fg, straight printf), for a savings of %.2f nanos\n",
      backgroundTest / double(LOOP_NUM * REPEATS),
      printfTest / double(LOOP_NUM * REPEATS),
      (printfTest - backgroundTest) / double(LOOP_NUM * REPEATS));
  INFO("This should cause a boost::format error: %zd", sizeof(int));
  try {
    FATAL("This is a fatal exception (but I'm catching it)");
  } catch (std::runtime_error& e) {
    fflush(nullptr);
    fprintf(stderr, "Runtime exception caught! (expected)\n");
  }
  { // test from documentation
    Logging::logOnJunk() = true; // sets the background thread that outputs log messages to the last CPU
    std::string s = "testMe";
    INFO("String contains '%s' which is %ld characters long", s.c_str(), s.size());
    ZZWARN("String still contains '%s' which is %ld characters long", s.c_str(), s.size());
    Logging::sync();
    try {
      FATAL("That's all '%s' (expected to be caught)", s.c_str());
    } catch (std::runtime_error& e) {
      Logging::sync(); // just here to ensure sensible ordering. You don't need to keep calling sync() in production code
      fprintf(stderr, "Another runtime exception caught! (expected)\n");
      Logging::sync(); // just here to ensure sensible ordering. You don't need to keep calling sync() in production code
    }
    Logging::fprintf(stdout, "This is a test of straight logging on line %ld.\n", __LINE__);
  }
}

