// Exercises the anticheat's statistics helpers and its scoring rules.
//
// The checks themselves need a JVM and a live Minecraft client, but every
// decision they make is delegated to the pure functions in AcModels.h, so the
// interesting half is testable on its own. The cases below are written as
// "what would a person's numbers look like" against "what would a macro's look
// like", because a check that fires on both is worse than no check.

#include "../Utils/Anticheat/AcModels.h"
#include "../Utils/Anticheat/AcStats.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace Anticheat;

namespace {

#define REQUIRE(condition)                                                     \
  do {                                                                         \
    if (!(condition))                                                          \
      throw std::runtime_error(std::string("requirement failed: ") +           \
                               #condition);                                    \
  } while (false)

bool near(double a, double b, double tol = 1e-9) {
  return std::fabs(a - b) <= tol;
}

// ---------------------------------------------------------------------------
// Ring
// ---------------------------------------------------------------------------
void testRingKeepsNewestInOrder() {
  Ring<int, 4> r;
  REQUIRE(r.size() == 0);
  for (int i = 1; i <= 3; ++i)
    r.push(i);
  REQUIRE(r.size() == 3);
  REQUIRE(r[0] == 1);
  REQUIRE(r[2] == 3);
  REQUIRE(r.newest() == 3);

  // Overflow must evict the oldest, not the newest.
  r.push(4);
  r.push(5);
  REQUIRE(r.size() == 4);
  REQUIRE(r.full());
  REQUIRE(r[0] == 2);
  REQUIRE(r[3] == 5);
  REQUIRE(r.newest() == 5);

  r.clear();
  REQUIRE(r.size() == 0);
}

void testMeanAndStddev() {
  Ring<double, 8> r;
  REQUIRE(near(mean(r), 0.0));
  REQUIRE(near(stddev(r), 0.0));

  r.push(2.0);
  REQUIRE(near(mean(r), 2.0));
  REQUIRE(near(stddev(r), 0.0)); // one sample has no spread

  r.push(4.0);
  REQUIRE(near(mean(r), 3.0));
  REQUIRE(near(stddev(r), 1.0));
}

void testModalRatio() {
  Ring<uint8_t, 8> r;
  for (int i = 0; i < 8; ++i)
    r.push(1);
  uint8_t mode = 0;
  REQUIRE(near(modalRatio(r, &mode), 1.0));
  REQUIRE(mode == 1);

  Ring<uint8_t, 8> spread;
  const uint8_t human[] = {2, 4, 3, 5, 2, 6, 4, 3};
  for (uint8_t v : human)
    spread.push(v);
  double ratio = modalRatio(spread, &mode);
  REQUIRE(ratio < 0.4); // 2, 3 and 4 each appear twice out of eight
}

void testFractionWithinAndRamp() {
  Ring<int, 4> r;
  r.push(1);
  r.push(5);
  r.push(9);
  r.push(3);
  REQUIRE(near(fractionWithin(r, 1, 5), 0.75));

  REQUIRE(near(ramp(5.0, 0.0, 10.0), 0.5));
  REQUIRE(near(ramp(-3.0, 0.0, 10.0), 0.0)); // clamps low
  REQUIRE(near(ramp(99.0, 0.0, 10.0), 1.0)); // clamps high
}

void testEventWindowRate() {
  EventWindow<16> w;
  // Ten events spread over the last second.
  for (int i = 0; i < 10; ++i)
    w.push(10000 + (uint64_t)i * 100);
  REQUIRE(w.countSince(10900, 1000) == 10);
  REQUIRE(near(w.ratePerSecond(10900, 1000), 10.0));
  // Same events, asked about much later: all outside the window.
  REQUIRE(w.countSince(20000, 1000) == 0);
}

// ---------------------------------------------------------------------------
// AutoBlock scoring
// ---------------------------------------------------------------------------
AutoBlockEvidence macroBlocker() {
  AutoBlockEvidence e;
  e.samples = 12;
  e.agreement = 0.97;   // every re-block lands on the same tick
  e.modeTicks = 1;
  e.blocksPerSecond = 11.0;
  e.followThrough = 1.0; // never misses one
  return e;
}

AutoBlockEvidence skilledHuman() {
  AutoBlockEvidence e;
  e.samples = 12;
  e.agreement = 0.42;   // delays scattered over several ticks
  e.modeTicks = 3;
  e.blocksPerSecond = 5.5;
  e.followThrough = 0.7;
  return e;
}

void testAutoBlockFlagsMacro() {
  Verdict v = scoreAutoBlock(macroBlocker());
  REQUIRE(v.flag);
  REQUIRE(v.confidence > AutoBlockLimits::kMinConfidence);
  REQUIRE(v.vlGain > 1.0);
}

void testAutoBlockIgnoresHuman() {
  REQUIRE(!scoreAutoBlock(skilledHuman()).flag);
}

void testAutoBlockWaitsForSamples() {
  AutoBlockEvidence e = macroBlocker();
  e.samples = AutoBlockLimits::kMinSamples - 1;
  REQUIRE(!scoreAutoBlock(e).flag);
}

void testAutoBlockNeedsMoreThanOneSignal() {
  // A blocks-per-second rate no hand reaches, but the timing is scattered and
  // plenty of attacks go unblocked. One loud number must not be enough.
  AutoBlockEvidence e = skilledHuman();
  e.blocksPerSecond = 14.0;
  REQUIRE(!scoreAutoBlock(e).flag);

  // Perfectly agreeing timing on its own is likewise not enough.
  AutoBlockEvidence tidy = skilledHuman();
  tidy.agreement = 0.99;
  tidy.modeTicks = 1;
  tidy.blocksPerSecond = 4.0;
  tidy.followThrough = 0.6;
  REQUIRE(!scoreAutoBlock(tidy).flag);
}

void testAutoBlockIgnoresSlowButTidyReblocks() {
  // Agreement is high, but the player consistently re-blocks four ticks after
  // attacking. That is a person with a rhythm, not a macro closing the gap.
  AutoBlockEvidence e = macroBlocker();
  e.modeTicks = 4;
  e.blocksPerSecond = 6.0;
  REQUIRE(!scoreAutoBlock(e).flag);
}

// ---------------------------------------------------------------------------
// Aim scoring
// ---------------------------------------------------------------------------
void testAimFlagsLockedAim() {
  AimEvidence e;
  e.samples = 10;
  e.meanErrorDeg = 0.4;   // dead on target
  e.stddevErrorDeg = 0.2; // and never wavers
  e.yawDeltaBeforeHit = 60.0;
  e.lastErrorDeg = 0.3;
  Verdict v = scoreAim(e);
  REQUIRE(v.flag);
  REQUIRE(v.confidence > AimLimits::kMinConfidence);
}

void testAimIgnoresGoodPlayer() {
  AimEvidence e;
  e.samples = 10;
  e.meanErrorDeg = 6.0;
  e.stddevErrorDeg = 4.0;
  e.yawDeltaBeforeHit = 12.0;
  e.lastErrorDeg = 5.0;
  REQUIRE(!scoreAim(e).flag);
}

void testAimIgnoresAccurateButHumanSpread() {
  // Accurate on average, but the error still wanders the way a hand does, and
  // there is no snap. Only one signal fires, so no flag.
  AimEvidence e;
  e.samples = 10;
  e.meanErrorDeg = 1.2;
  e.stddevErrorDeg = 3.5;
  e.yawDeltaBeforeHit = 5.0;
  e.lastErrorDeg = 1.0;
  REQUIRE(!scoreAim(e).flag);
}

void testAimWaitsForSamples() {
  AimEvidence e;
  e.samples = AimLimits::kMinSamples - 1;
  e.meanErrorDeg = 0.1;
  e.stddevErrorDeg = 0.05;
  e.yawDeltaBeforeHit = 80.0;
  e.lastErrorDeg = 0.1;
  REQUIRE(!scoreAim(e).flag);
}

// ---------------------------------------------------------------------------
// Reach scoring
// ---------------------------------------------------------------------------
void testReachFlagsSustainedOverreach() {
  ReachEvidence e;
  e.samples = 10;
  e.overLimitFraction = 0.9;
  e.worstDistance = 3.9;
  Verdict v = scoreReach(e);
  REQUIRE(v.flag);
  REQUIRE(v.confidence > ReachLimits::kMinConfidence);
}

void testReachIgnoresOccasionalLagSpike() {
  // One hit in ten measured long is what stale positions look like.
  ReachEvidence e;
  e.samples = 10;
  e.overLimitFraction = 0.1;
  e.worstDistance = 3.8;
  REQUIRE(!scoreReach(e).flag);
}

void testReachWaitsForSamples() {
  ReachEvidence e;
  e.samples = ReachLimits::kMinSamples - 1;
  e.overLimitFraction = 1.0;
  e.worstDistance = 4.5;
  REQUIRE(!scoreReach(e).flag);
}

struct TestCase {
  const char *name;
  void (*fn)();
};

const TestCase kTests[] = {
    {"ring keeps newest in order", testRingKeepsNewestInOrder},
    {"mean and stddev", testMeanAndStddev},
    {"modal ratio separates macro from human", testModalRatio},
    {"fractionWithin and ramp", testFractionWithinAndRamp},
    {"event window rate", testEventWindowRate},
    {"autoblock flags a macro", testAutoBlockFlagsMacro},
    {"autoblock ignores a skilled human", testAutoBlockIgnoresHuman},
    {"autoblock waits for samples", testAutoBlockWaitsForSamples},
    {"autoblock needs more than one signal",
     testAutoBlockNeedsMoreThanOneSignal},
    {"autoblock ignores slow but tidy reblocks",
     testAutoBlockIgnoresSlowButTidyReblocks},
    {"aim flags locked aim", testAimFlagsLockedAim},
    {"aim ignores a good player", testAimIgnoresGoodPlayer},
    {"aim ignores accurate but human spread",
     testAimIgnoresAccurateButHumanSpread},
    {"aim waits for samples", testAimWaitsForSamples},
    {"reach flags sustained overreach", testReachFlagsSustainedOverreach},
    {"reach ignores an occasional lag spike",
     testReachIgnoresOccasionalLagSpike},
    {"reach waits for samples", testReachWaitsForSamples},
};

} // namespace

int main() {
  int failed = 0;
  for (const TestCase &t : kTests) {
    try {
      t.fn();
      std::cout << "[ ok ] " << t.name << "\n";
    } catch (const std::exception &e) {
      std::cout << "[FAIL] " << t.name << ": " << e.what() << "\n";
      ++failed;
    }
  }
  if (failed) {
    std::cout << failed << " test(s) failed\n";
    return 1;
  }
  std::cout << "all " << (sizeof(kTests) / sizeof(kTests[0]))
            << " anticheat tests passed\n";
  return 0;
}
