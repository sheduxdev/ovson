#pragma once
// The scoring rules the checks use, as pure functions.
//
// These sit apart from AcChecks.cpp so they can be exercised without a JVM, a
// Minecraft client or a config file. The checks gather evidence; everything
// that decides what the evidence means lives here.

#include "AcStats.h"
#include <cstddef>
#include <cstdint>

namespace Anticheat {

struct Verdict {
  bool flag = false;
  double confidence = 0.0; // 0..1
  double vlGain = 0.0;     // how hard to push the violation level
};

// ---------------------------------------------------------------------------
// AutoBlock
// ---------------------------------------------------------------------------
struct AutoBlockEvidence {
  size_t samples = 0;      // paired attack -> re-block observations
  double agreement = 0.0;  // modalRatio of the re-block delays
  uint8_t modeTicks = 0;   // the delay those samples agree on
  double blocksPerSecond = 0.0;
  double followThrough = 0.0; // fraction of attacks that got a block at all
};

namespace AutoBlockLimits {
constexpr size_t kMinSamples = 8;
constexpr uint8_t kMachineModeTicks = 1;
constexpr double kHumanBps = 7.0;
constexpr double kImpossibleBps = 13.0;
constexpr double kMinConfidence = 0.55;
} // namespace AutoBlockLimits

// A single "blocked right after attacking" event is exactly what a good manual
// blockhitter produces, so no one property can raise a flag alone. Each is
// scored separately and at least two must be convincing before the combined
// confidence is even consulted.
inline Verdict scoreAutoBlock(const AutoBlockEvidence &e) {
  using namespace AutoBlockLimits;
  Verdict v;
  if (e.samples < kMinSamples)
    return v;

  // Delays that agree on a value only a machine can hit.
  double sAgree =
      (e.modeTicks <= kMachineModeTicks) ? ramp(e.agreement, 0.70, 0.95) : 0.0;
  // Re-pressing the button is a physical action with a physical ceiling.
  double sRate = ramp(e.blocksPerSecond, kHumanBps, kImpossibleBps);
  // People miss some of their re-blocks. Macros do not.
  double sFollow = ramp(e.followThrough, 0.75, 1.0);

  int strong =
      (sAgree > 0.5 ? 1 : 0) + (sRate > 0.5 ? 1 : 0) + (sFollow > 0.5 ? 1 : 0);
  if (strong < 2)
    return v;

  double confidence = sAgree * 0.45 + sRate * 0.30 + sFollow * 0.25;
  if (confidence < kMinConfidence)
    return v;

  v.flag = true;
  v.confidence = confidence;
  v.vlGain = 1.0 + confidence * 4.0;
  return v;
}

// ---------------------------------------------------------------------------
// KillAura
// ---------------------------------------------------------------------------
struct AimEvidence {
  size_t samples = 0;
  double meanErrorDeg = 0.0;
  double stddevErrorDeg = 0.0;
  double yawDeltaBeforeHit = 0.0;
  double lastErrorDeg = 0.0;
};

namespace AimLimits {
constexpr size_t kMinSamples = 6;
constexpr double kAccurateDeg = 3.0;
constexpr double kSteadyDeg = 2.0;
constexpr double kSnapDeg = 35.0;
constexpr double kBehindDeg = 95.0;
constexpr double kMinConfidence = 0.55;
} // namespace AimLimits

inline Verdict scoreAim(const AimEvidence &e) {
  using namespace AimLimits;
  Verdict v;
  if (e.samples < kMinSamples)
    return v;

  // Accuracy is one signal; repeatability is a separate one. A hand wanders
  // even when it is on target, so an unnaturally steady error matters as much
  // as an unnaturally small one.
  double sAccuracy = ramp(kAccurateDeg - e.meanErrorDeg, 0.0, kAccurateDeg);
  double sSteady = ramp(kSteadyDeg - e.stddevErrorDeg, 0.0, kSteadyDeg);
  // A large rotation in the tick before the hit that lands dead on target is a
  // snap, which skill does not reproduce.
  double sSnap = (e.yawDeltaBeforeHit > kSnapDeg && e.lastErrorDeg < 2.0)
                     ? ramp(e.yawDeltaBeforeHit, kSnapDeg, 90.0)
                     : 0.0;

  int strong = (sAccuracy > 0.5 ? 1 : 0) + (sSteady > 0.5 ? 1 : 0) +
               (sSnap > 0.5 ? 1 : 0);
  if (strong < 2)
    return v;

  double confidence = sAccuracy * 0.4 + sSteady * 0.4 + sSnap * 0.2;
  if (confidence < kMinConfidence)
    return v;

  v.flag = true;
  v.confidence = confidence;
  v.vlGain = 1.0 + confidence * 3.0;
  return v;
}

// ---------------------------------------------------------------------------
// Reach
// ---------------------------------------------------------------------------
struct ReachEvidence {
  size_t samples = 0;
  double overLimitFraction = 0.0;
  double worstDistance = 0.0;
};

namespace ReachLimits {
constexpr size_t kMinSamples = 6;
constexpr double kLimit = 3.15; // 3.0 survival reach plus slack
constexpr double kAbsurd = 7.0; // beyond this the attacker/victim pairing is wrong
constexpr double kOverFraction = 0.5;
constexpr double kMinConfidence = 0.5;
} // namespace ReachLimits

// Positions arrive interpolated and a tick or two stale, so one long hit means
// nothing. Lag moves individual samples; it does not move the whole set.
inline Verdict scoreReach(const ReachEvidence &e) {
  using namespace ReachLimits;
  Verdict v;
  if (e.samples < kMinSamples)
    return v;
  if (e.overLimitFraction < kOverFraction)
    return v;

  double confidence = ramp(e.overLimitFraction, kOverFraction, 0.9) * 0.6 +
                      ramp(e.worstDistance, kLimit, kLimit + 0.8) * 0.4;
  if (confidence < kMinConfidence)
    return v;

  v.flag = true;
  v.confidence = confidence;
  v.vlGain = 1.0 + confidence * 3.0;
  return v;
}

} // namespace Anticheat
