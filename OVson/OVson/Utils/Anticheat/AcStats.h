#pragma once
// Small fixed-capacity statistics helpers for the anticheat checks.
//
// Everything here is allocation-free on purpose: the checks run for every
// tracked player on every anticheat tick, on the render thread. The old code
// used std::vector with erase(begin()) and an unordered_map keyed by check
// name, which meant a hash, a heap touch and an O(n) memmove per player per
// tick. These do the same jobs with a plain array.

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Anticheat {

// Ring buffer of the last N samples, oldest evicted first.
template <typename T, size_t N> class Ring {
public:
  void push(T v) {
    m_v[m_head] = v;
    m_head = (m_head + 1) % N;
    if (m_n < N)
      ++m_n;
  }
  void clear() { m_n = 0; m_head = 0; }
  size_t size() const { return m_n; }
  bool full() const { return m_n == N; }
  static constexpr size_t capacity() { return N; }

  // 0 is the oldest retained sample.
  T operator[](size_t i) const {
    size_t start = (m_head + N - m_n) % N;
    return m_v[(start + i) % N];
  }
  T newest() const { return m_n ? (*this)[m_n - 1] : T{}; }

private:
  T m_v[N]{};
  size_t m_n = 0;
  size_t m_head = 0;
};

// Mean of a numeric ring. Returns 0 for an empty ring.
template <typename T, size_t N> double mean(const Ring<T, N> &r) {
  if (!r.size())
    return 0.0;
  double s = 0.0;
  for (size_t i = 0; i < r.size(); ++i)
    s += (double)r[i];
  return s / (double)r.size();
}

// Population standard deviation. Returns 0 for fewer than two samples.
template <typename T, size_t N> double stddev(const Ring<T, N> &r) {
  if (r.size() < 2)
    return 0.0;
  double m = mean(r);
  double s = 0.0;
  for (size_t i = 0; i < r.size(); ++i) {
    double d = (double)r[i] - m;
    s += d * d;
  }
  return std::sqrt(s / (double)r.size());
}

// How much of the ring sits on its single most common value, and what that
// value is. This is the workhorse for "is this timing machine-generated":
// a human retains a spread of values, a macro collapses onto one.
//
// Sampling quantises every latency we can observe to whole Minecraft ticks,
// so a spread measured in milliseconds is mostly quantisation noise. The
// modal ratio is quantisation-proof: it asks whether the samples agree, not
// by how many milliseconds they differ.
template <typename T, size_t N>
double modalRatio(const Ring<T, N> &r, T *outMode = nullptr) {
  if (!r.size())
    return 0.0;
  size_t best = 0;
  T bestV = r[0];
  for (size_t i = 0; i < r.size(); ++i) {
    size_t c = 0;
    for (size_t j = 0; j < r.size(); ++j)
      if (r[j] == r[i])
        ++c;
    if (c > best) {
      best = c;
      bestV = r[i];
    }
  }
  if (outMode)
    *outMode = bestV;
  return (double)best / (double)r.size();
}

// Fraction of samples within [lo, hi].
template <typename T, size_t N>
double fractionWithin(const Ring<T, N> &r, T lo, T hi) {
  if (!r.size())
    return 0.0;
  size_t c = 0;
  for (size_t i = 0; i < r.size(); ++i)
    if (r[i] >= lo && r[i] <= hi)
      ++c;
  return (double)c / (double)r.size();
}

// Timestamps of recent events, used for "how many times per second".
template <size_t N> class EventWindow {
public:
  void push(uint64_t ms) { m_t.push(ms); }
  void clear() { m_t.clear(); }
  size_t size() const { return m_t.size(); }
  uint64_t newest() const { return m_t.newest(); }

  // Events recorded in the last windowMs milliseconds.
  int countSince(uint64_t nowMs, uint64_t windowMs) const {
    int c = 0;
    for (size_t i = 0; i < m_t.size(); ++i)
      if (nowMs - m_t[i] <= windowMs)
        ++c;
    return c;
  }
  // Same, expressed as events per second.
  double ratePerSecond(uint64_t nowMs, uint64_t windowMs) const {
    if (!windowMs)
      return 0.0;
    return countSince(nowMs, windowMs) * 1000.0 / (double)windowMs;
  }

private:
  Ring<uint64_t, N> m_t;
};

// Maps a raw measurement onto 0..1, clamped. Used to turn each signal into a
// comparable score before the checks weigh them against each other.
inline double ramp(double v, double zeroAt, double oneAt) {
  if (oneAt == zeroAt)
    return v >= oneAt ? 1.0 : 0.0;
  double t = (v - zeroAt) / (oneAt - zeroAt);
  return t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
}

} // namespace Anticheat
