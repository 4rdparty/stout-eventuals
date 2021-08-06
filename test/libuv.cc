#include <list>

#include "gtest/gtest.h"
#include "stout/eventual.h"
#include "stout/just.h"
#include "stout/terminal.h"
#include "uv.h"

namespace stout {
namespace uv {

class Loop {
 public:
  enum RunMode {
    RUN_DEFAULT = 0,
    RUN_ONCE,
    RUN_NOWAIT
  };

  Loop() {
    uv_loop_init(&loop_);
  }

  Loop(const Loop&) = delete;

  ~Loop() {
    uv_loop_close(&loop_);
  }

  void run(const RunMode& run_mode = RUN_DEFAULT) {
    uv_run(&loop_, (uv_run_mode) run_mode);
  }

  size_t timers() {
#ifdef _WIN32
    return loop_.timer_heap->nelts;
#else
    return loop_.timer_heap.nelts;
#endif
  }

  operator uv_loop_t*() {
    return &loop_;
  }

 private:
  uv_loop_t loop_;
};


class Clock {
 public:
  Clock(Loop& loop)
    : loop_(loop) {}

  Clock(const Clock&) = delete;

  void Pause() {
    CHECK(!paused_);

    CHECK_EQ(0, loop_.timers())
        << "pausing the clock with outstanding timers is unsupported";

    paused_.emplace(uv_now(loop_));

    advanced_ = 0;
  }

  bool Paused() {
    return paused_.has_value();
  }

  void Advance(uint64_t milliseconds) {
    CHECK(paused_);

    advanced_ += milliseconds;

    for (auto& timer : timers_) {
      // uint64_t now = *paused_ + advanced_;
      if (timer.valid) {
        if (advanced_ >= timer.milliseconds) {
          timer.start(0);
          // TODO(benh): ideally we remove the timer from 'timers_' but
          // for now we just invalidate it so we don't start it again.
          timer.valid = false;
        }
      }
    }
  }

  void Resume() {
    CHECK(paused_);

    for (auto& timer : timers_) {
      // uint64_t now = *paused_ + advanced_;
      if (timer.valid) {
        timer.start(timer.milliseconds - advanced_);
      }
    }

    timers_.clear();

    paused_.reset();
  }

  void Enqueue(uint64_t milliseconds, Callback<uint64_t> start) {
    CHECK(paused_);
    timers_.emplace_back(Timer{milliseconds, std::move(start)});
  }

  auto& loop() {
    return loop_;
  }

 private:
  Loop& loop_;
  std::optional<uint64_t> paused_;
  uint64_t advanced_;

  struct Timer {
    uint64_t milliseconds;
    Callback<uint64_t> start;
    bool valid = true;
  };

  std::list<Timer> timers_;
};


auto Timer(Clock& clock, uint64_t milliseconds) {
  return eventuals::Eventual<void>()
      .context(uv_timer_t())
      .start([&clock, milliseconds](auto& timer, auto& k) mutable {
        uv_timer_init(clock.loop(), &timer);
        timer.data = &k;

        auto start = [&timer](uint64_t milliseconds) {
          uv_timer_start(
              &timer,
              [](uv_timer_t* timer) {
                eventuals::succeed(*(decltype(&k)) timer->data);
              },
              milliseconds,
              0);
        };

        if (!clock.Paused()) {
          start(milliseconds);
        } else {
          clock.Enqueue(milliseconds, std::move(start));
        }
      });
}

} // namespace uv
} // namespace stout

namespace eventuals = stout::eventuals;

using stout::eventuals::Eventual;
using stout::eventuals::Just;
using stout::eventuals::Terminate;

using stout::uv::Clock;
using stout::uv::Loop;
using stout::uv::Timer;

class Foo {
 public:
  Foo(Clock& clock)
    : clock_(clock) {}

  auto Operation() {
    return Timer(clock_, 5000)
        | Just(42);
  }

 private:
  Clock& clock_;
};

TEST(Libuv, Test) {
  Loop loop;
  Clock clock(loop);

  Foo foo(clock);

  auto e = foo.Operation();

  auto [future, k] = Terminate(e);

  clock.Pause();

  eventuals::start(k);

  EXPECT_FALSE(uv_loop_alive(loop));

  clock.Advance(1000);

  EXPECT_FALSE(uv_loop_alive(loop));

  clock.Advance(4000);

  EXPECT_TRUE(uv_loop_alive(loop));

  loop.run(stout::uv::Loop::RUN_ONCE);

  EXPECT_EQ(42, future.get());
}
