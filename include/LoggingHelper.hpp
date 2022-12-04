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

/** Detail/Internal functions used by Logging.h **/

#ifndef LOGGING_HELPER_DEFINE
#define LOGGING_HELPER_DEFINE

#include <boost/format.hpp>
#include <sstream>
#include <string.h>

namespace LoggingHelper {
  struct Util {
    // buffer 'to' must be able to hold len+1 characters. Returns size of ecrypted string
    virtual size_t encrypt(const char* from, char* to, size_t len) { 
      memcpy(to,from,len);
      return len;
    }
    // buffer 'to' must be able to hold len+1 characters. Returns size of decrypted string
    virtual size_t decrypt(const char* from, char* to, size_t len) {
      memcpy(to,from,len);
      return len;
    }
    virtual void setJunkThreadAffinity(bool f = true) { // sets to last CPU
      int ncpus = sysconf(_SC_NPROCESSORS_ONLN);
      pthread_t current_thread = pthread_self();    
      cpu_set_t cpuset;
      CPU_ZERO(&cpuset);
      if (f) {
        CPU_SET(ncpus - 1, &cpuset);
        fprintf(stderr, "Thread %ld set to run on cpu %ld\n", int64_t(current_thread), int64_t(ncpus-1));
      } else {
        for (int i=0; i < ncpus; ++i) CPU_SET(i, &cpuset);
      }
      if (pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset)) {
        perror("pthread_setaffinity_np");
      }
    }

    virtual std::tuple<int, int, int, int64_t> timeParts(int64_t ts=0) {
      timespec tp;
      if (ts==0) {
        clock_gettime(CLOCK_REALTIME, &tp);
      } else {
        auto d = std::div(ts, int64_t(1000)*1000*1000);
        tp.tv_sec = d.quot;
        tp.tv_nsec = d.rem;
      }
      int64_t secsSinceMidnight = tp.tv_sec % (24 * 60 * 60);
      return std::make_tuple(
          secsSinceMidnight / 60 / 60,
          (secsSinceMidnight / 60) % 60,
          (secsSinceMidnight % 60),
          tp.tv_nsec / 1000);

    }
    virtual int realUSleep(useconds_t usec) { return ::usleep(usec); }

    // reset pointer to derived instance to replace w/ custom line encryption
    static Util*& util() { static Util* ptr = new ::LoggingHelper::Util(); return(ptr); }
  };
  inline void CheckFormat(int) { }
  
  // some template magic to determine the minimum space our parameter pack will take when we
  // write it out. Basically just the size of all the params, except for strings
  // which only need 1 character (for empty strings, which we'll use if/when we run out
  // of space
  template <typename C> static inline size_t getSingleSize(C c) {
    return sizeof(C);
  }
  template <> inline size_t getSingleSize<const char*>(const char* c) { return 1; }
  template <> inline size_t getSingleSize<char*>(char* c) { return 1; }

  inline size_t getMinSize() { return 0; }
  template <typename C, typename ... Types>
  static inline size_t getMinSize(C c, Types... rest) {
    return getSingleSize(c) + getMinSize(rest...);
  }

  // The actual helper code to write out the argument list
  template <typename C>
  inline void writeOutSingle(char*& stack, size_t& left, C c) {
    *(reinterpret_cast<C*>(stack)) = c;
    stack += sizeof(C);
  }
  template <>
  inline void writeOutSingle<const char*>(char*& stack, size_t& left, const char* c) {
    do {
      if (left==0 || *c == 0) {
        *stack=0;
        ++stack;
        return;
      } else {
        *stack = *c;
        ++stack;
        --left;
        ++c;
      }
    } while (1);
  }
  template <>
  inline void writeOutSingle<char*>(char*& stack, size_t& left, char* c) {
    writeOutSingle<const char*>(stack, left, c);
  }

  inline void writeOut(char* stack, size_t left) { }
  template <typename C, typename ... Types>
  static inline void writeOut(char* stack, size_t left, C c, Types... rest) { 
    writeOutSingle(stack, left, c); // updates stack and left
    writeOut(stack, left, rest...);
  }


  struct Printer {
    virtual void print() const = 0;
    template <size_t bSize, class C, typename... Params>
      static void createPrinter(FILE* out, void* buf, C fmt, Params... parameters);
    FILE* _out = NULL;
    template <typename... Types> struct doPrint;
    template <typename C> static void doPrintDetail(boost::format& fmt, const char*& stack) {
      const auto* c = reinterpret_cast<const C*>(stack);
      fmt % *c;
      stack += sizeof(C);
    }
    virtual const char* getFormat() const { return ""; }
  };
  template <> inline void Printer::doPrintDetail<const char*>(boost::format& fmt, const char*& stack) {
    const char* c = reinterpret_cast<const char*>(stack);
    fmt % c;
    while (*stack != 0) ++stack;
    ++stack;
  }
  template <> inline void Printer::doPrintDetail<char*>(boost::format& fmt, const char*& stack) {
    doPrintDetail<const char*>(fmt, stack);
  }
  template <typename C, typename ...Types> struct Printer::doPrint<C, Types...> {
    inline void operator()(boost::format&fmt, const char* stack, FILE* out) {
      Printer::doPrintDetail<C>(fmt, stack);
      Printer::doPrint<Types...>().operator()(fmt, stack, out);
    }
  };
  template <> struct Printer::doPrint<> {
    inline void operator()(boost::format& fmt, const char* stack, FILE* out) {
      static bool encryption = (getenv("ZZ_ENCRYPT_FILES") != nullptr);
      std::stringstream ss;
      ss << fmt;
      const std::string& s = ss.str();
      if (encryption) {
        static std::vector<char> buf;
        if (buf.size() < s.size()+2) buf.resize(s.size()+2);
        auto& lc = *(::LoggingHelper::Util::util());
        size_t esz = lc.encrypt(s.c_str(), &buf[0], s.size());
        fwrite(&buf[0], esz, 1, out);
      } else {
        fwrite(s.c_str(), s.size(), 1, out);
      }
    }
  };

  template <size_t bufSize, typename... Params> struct PrinterT: public Printer {
    PrinterT(FILE* out, const char* format, Params... parameters) : _format(format) {
      char* buf = reinterpret_cast<char*>(this);
      _out = out;
      size_t minSize = getMinSize(parameters...);
      char* stack = ((char*)buf) + sizeof(*this);
      writeOut(stack, bufSize - sizeof(*this) - minSize, parameters...);
    }
    virtual void print() const override {
      const char* stack = ((const char*)this) + sizeof(*this);
      boost::format fmt(_format);
      doPrint<Params...>()(fmt, stack, _out);
    }
    const char* _format = NULL;
    virtual const char* getFormat() const override { return _format; }
  };

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-security"
  template <size_t bSize, class C, typename... Params>
    inline void Printer::createPrinter(FILE* out, void* buf, C fmt, Params... parameters) {
      char mybuf[1024 * 16];
      snprintf(mybuf, sizeof(mybuf), fmt, parameters...);
      new (buf)PrinterT<bSize, const char*>(out, "%s", mybuf);
    }
#pragma GCC diagnostic pop
}


#endif
