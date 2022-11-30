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
#ifndef LOGGING_HEADER_DEFINE
#define LOGGING_HEADER_DEFINE

#include "LoggingHelper.hpp"
#include "MessageQueue.hpp"

#include <boost/mpl/string.hpp>
#include <stdint.h>
#include <time.h>
#include <pthread.h>
#include <sched.h>
#include <array>
#include <execinfo.h>
#include <iostream>
#include <fstream>
#include <signal.h>

// seems to take 10-40 micros with regular printf

#define INFO(A,...) do { \
  const auto& _info_tm = LoggingHelper::Util::util()->timeParts(); \
  if (::detail::LoggingBackgroundThread::on()) { \
    ::detail::LoggingBackgroundThread::instance()->fprintf(stdout,         \
        "%02d:%02d:%02d.%06ld %s:" "%d " A "\n",std::get<0>(_info_tm), std::get<1>(_info_tm), std::get<2>(_info_tm), \
        std::get<3>(_info_tm), Logging::ForwardFilename(__FILE__),__LINE__,##__VA_ARGS__); \
  } else { \
    fprintf(stdout, \
        "%02d:%02d:%02d.%06ld %s:" "%d " A "\n",std::get<0>(_info_tm), std::get<1>(_info_tm), std::get<2>(_info_tm), \
        std::get<3>(_info_tm), Logging::ForwardFilename(__FILE__),__LINE__,##__VA_ARGS__); \
  } \
} while (0)

// note: WARN() define conflicts with one used by Rcpp
#define ZZWARN(A,...) do { \
  const auto& _info_tm = LoggingHelper::Util::util()->timeParts(); \
  if (::detail::LoggingBackgroundThread::on()) { \
    ::detail::LoggingBackgroundThread::instance()->fprintf(stderr,         \
        "%02d:%02d:%02d.%06ld %s:" "%d !!WARNING!! " A "\n",std::get<0>(_info_tm), std::get<1>(_info_tm), std::get<2>(_info_tm), \
        std::get<3>(_info_tm), Logging::ForwardFilename(__FILE__),__LINE__,##__VA_ARGS__); \
  } else { \
    fprintf(stderr, \
        "%02d:%02d:%02d.%06ld %s:" "%d " A "\n",std::get<0>(_info_tm), std::get<1>(_info_tm), std::get<2>(_info_tm), \
        std::get<3>(_info_tm), Logging::ForwardFilename(__FILE__),__LINE__,##__VA_ARGS__); \
  } \
} while (0)

#define FATAL(A,...) do { \
  ZZWARN("!!FATAL!! " A,##__VA_ARGS__); \
  throw std::runtime_error("Fatal exception thrown. See log for details."); \
} while (0)

class Logging {
  public:
    static const char* ForwardFilename(const char* c) {
      do {
        const char* b = c;
        while (*b != 0 && *b != '/') ++b;
        if (*b == 0) return(c);
        else c = b+1;
      } while (1);
    }
    static void sync(); 
    static bool& yieldViaSleep() { // set to true if we want to call sleep when syncing() (i.e., in qa or backtest)
      static bool b = false;
      return b;
    }
    static bool& logOnJunk() { // set to true if we want to move the logging thread to the junk (last) core
      static bool b = false;
      return b;
    }
    template<typename... Args> static void fprintf(FILE* file, const char * format, Args... args);
};

namespace detail {
  class LoggingBackgroundThread {
    public:
      static bool& on() { // call on() == false to turn off logging
        static bool b = true;
        return(b);
      }
      static LoggingBackgroundThread* instance() {
        if (_instance == NULL) _instance = new LoggingBackgroundThread();
        return _instance;
      }
      LoggingBackgroundThread() {
        pthread_create(&bg_thread, NULL, &run, (void*)this);
      }
      inline static void* run(void *vself) {
        static bool switchedToJunk = false;
        auto* self = reinterpret_cast<LoggingBackgroundThread*>(vself);
        while (!self->_exit) {
          if (Logging::logOnJunk() && !switchedToJunk) {
            ::fprintf(stderr, "Setting logger affinity\n");
            ::LoggingHelper::Util::util()->setJunkThreadAffinity();
            switchedToJunk = true;
          }
          auto msgp = self->_mq.recv(self->_readCount);
          if (msgp) {
            try {
              const auto* p = reinterpret_cast<const LoggingHelper::Printer*>(&(msgp->front()));
              p->print();
            } catch (const std::exception& e) {
              static int whingeCount = 0;
              if (++whingeCount < 100) {
                ::fprintf(stderr, "!!WARNING!! Exception caught in background logger: %s\n", e.what());
                try {
                  const auto* p = reinterpret_cast<const LoggingHelper::Printer*>(&(msgp->front()));
                  ::fprintf(stderr, "Format line was '%s'\n", p->getFormat());
                } catch (...) { }
              } else if (whingeCount == 100) {
                ::fprintf(stderr, "!!WARNING!! Background logger will stop whinging now\n");
              }
            }
          } else {
            ::fflush(NULL);
            ::LoggingHelper::Util::util()->realUSleep(10LL * 1000 - 1); // sleep 10ms (-1 micro, to distinguish this call)
          }
        }
        self->_finished = true;
        return nullptr;
      }
      ~LoggingBackgroundThread() {
        while (_readCount != _mq.writeCount()) {
          ::LoggingHelper::Util::util()->realUSleep(1000 * 10);
        }
        _exit = true;
        while (!_finished) { 
          ::LoggingHelper::Util::util()->realUSleep(1000 * 10);
        }
      }
      void sync() const {
        while (int64_t(_mq.writeCount()) != int64_t(_readCount)) {
          if (Logging::yieldViaSleep()) {
            ::LoggingHelper::Util::util()->realUSleep(1000*100);
          } else {
            sched_yield();
          }
        }
      }
      template <typename... Params>
      void fprintf(FILE *f, const char* fmt, Params... params) {
        while (int64_t(_mq.writeCount()) - int64_t(_readCount) >= int64_t(_mq.capacity())-1) {
          if (Logging::yieldViaSleep()) {
            ::LoggingHelper::Util::util()->realUSleep(1000*100);
          } else {
            sched_yield();
          }
        }
        auto wrt = _mq.nextWriteSlot();
        LoggingHelper::Printer::createPrinter<sizeof(*wrt)>(f, &(wrt->front()), fmt, params...);
      }
      static LoggingBackgroundThread* _instance;
      pthread_t bg_thread;
      volatile bool _exit = false;
      volatile bool _finished = false;
      BackgroundLogger::MessageQueue<std::array<char,1024*16> > _mq;
      volatile int64_t _readCount=0;

  };
}
inline void Logging::sync() { 
  if (detail::LoggingBackgroundThread::_instance != NULL) {
    detail::LoggingBackgroundThread::_instance->sync();
  }
}
template<typename... Args> void Logging::fprintf(FILE* file, const char * format, Args... args) {
  ::detail::LoggingBackgroundThread::instance()->fprintf(file, format, args...);
}

#endif

