#pragma once

#include <stdint.h>
#include <string>

namespace net {

// 时间戳，微秒。now表示当前时间戳的具体时间，精确到微秒

class Timestamp {
public:
  Timestamp() : microSecondsSinceEpoch_(0) {}
  explicit Timestamp(int64_t microSecondsSinceEpoch)
      : microSecondsSinceEpoch_(microSecondsSinceEpoch) {}

  void swap(Timestamp &that) {
    std::swap(microSecondsSinceEpoch_, that.microSecondsSinceEpoch_);
  }

  bool valid() const { return microSecondsSinceEpoch_ > 0; }
  int64_t microSecondsSinceEpoch() const { return microSecondsSinceEpoch_; }

  std::string toString() const;
  std::string toFormattedString(bool showMicroseconds = true) const;

  static Timestamp now();
  static Timestamp invalid() { return Timestamp(); }

  static const int kMicroSecondsPerSecond = 1000 * 1000;

private:
  int64_t microSecondsSinceEpoch_;
};

inline bool operator<(Timestamp lhs, Timestamp rhs) {
  return lhs.microSecondsSinceEpoch() < rhs.microSecondsSinceEpoch();
}

inline bool operator==(Timestamp lhs, Timestamp rhs) {
  return lhs.microSecondsSinceEpoch() == rhs.microSecondsSinceEpoch();
}

// 给定时间戳，返回从传入 timestamp 的 seconds 后的时间戳
inline Timestamp addTime(Timestamp timestamp, double seconds) {
  int64_t delta =
      static_cast<int64_t>(seconds * Timestamp::kMicroSecondsPerSecond);
  return Timestamp(timestamp.microSecondsSinceEpoch() + delta);
}

} // namespace net
