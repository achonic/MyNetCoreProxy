#pragma once

#include "utils/noncopyable.h"
#include "timestamp.h"
#include "channel.h"
#include "timer_id.h"
#include <set>
#include <vector>
#include <memory>

namespace net {

class EventLoop;
class Timer;

class TimerQueue : private utils::NonCopyable {
public:
  using TimerCallback = std::function<void()>;

  explicit TimerQueue(EventLoop *loop);
  ~TimerQueue();

  // Schedules the callback to be run at given time,
  // repeats if interval > 0.0.
  TimerId addTimer(TimerCallback cb, Timestamp when, double interval);
  void cancel(TimerId timerId);

private:
  using Entry = std::pair<Timestamp, Timer*>;
  using TimerList = std::set<Entry>;
  using ActiveTimer = std::pair<Timer*, int64_t>;
  using ActiveTimerSet = std::set<ActiveTimer>;

  void addTimerInLoop(Timer *timer);
  void cancelInLoop(TimerId timerId);

  // called when timerfd alarms
  void handleRead();

  // move out all expired timers
  std::vector<Entry> getExpired(Timestamp now);
  void reset(const std::vector<Entry> &expired, Timestamp now);

  bool insert(Timer *timer);

  EventLoop *loop_;
  const int timerfd_;
  Channel timerfdChannel_;

  TimerList timers_;

  ActiveTimerSet activeTimers_;
  bool callingExpiredTimers_;
  ActiveTimerSet cancelingTimers_;
};

} // namespace net
