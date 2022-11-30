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
detail::LoggingBackgroundThread* detail::LoggingBackgroundThread::_instance = NULL;
struct LoggingBackgroundThreadDeleter {
  static inline void myterminate() {
    static int64_t justExit = 0;
    justExit += 1;
    Logging::sync();
    static bool tried_throw = false;
    try {
      // try once to re-throw currently active exception
      if (!tried_throw) { tried_throw = true; throw; }
    }
    catch (const std::exception &e) {
      std::cerr << __FUNCTION__ << " caught unhandled exception. what(): "
        << e.what() << std::endl;
    }
    catch (...) {
      std::cerr << __FUNCTION__ << " caught unknown/unhandled exception." 
        << std::endl;
    }

    void * array[50];
    int size = backtrace(array, 50);    

    std::cerr << __FUNCTION__ << " backtrace returned " 
      << size << " frames\n\n";

    char ** messages = backtrace_symbols(array, size);

    for (int i = 0; i < size && messages != NULL; ++i) {
      std::cerr << "[bt]: (" << i << ") " << messages[i] << std::endl;
    }
    std::cerr << std::endl;

    free(messages);

    if (justExit == 2) { exit(-1); }
    abort();
  }
  static inline void myterminate1(int s) {
    fprintf(stderr, "Caught SIGNAL %d\n", s);
    myterminate();
  }
  LoggingBackgroundThreadDeleter() {
    std::set_terminate(myterminate);
    signal(SIGSEGV, myterminate1);
    signal(SIGBUS, myterminate1);
    signal(SIGABRT, myterminate1);
  }
  ~LoggingBackgroundThreadDeleter() { 
    Logging::sync();
    delete(detail::LoggingBackgroundThread::_instance);
    detail::LoggingBackgroundThread::_instance = NULL;
  }
} _loggingBackgroundThreadDeleter;

