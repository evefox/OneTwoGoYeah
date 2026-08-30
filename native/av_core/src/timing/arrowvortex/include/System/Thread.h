#pragma once

#include <mutex>

namespace Vortex {

class CriticalSection {
 public:
  void lock() { mutex_.lock(); }
  void unlock() { mutex_.unlock(); }

 private:
  std::mutex mutex_;
};

class ParallelThreads {
 public:
  virtual ~ParallelThreads() = default;
  virtual void exec(int item, int thread) = 0;

  void run(int count) {
    for (int thread = 0; thread < count; ++thread) {
      exec(thread, thread);
    }
  }
};

}  // namespace Vortex
