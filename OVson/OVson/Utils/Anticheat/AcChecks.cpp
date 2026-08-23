#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

#include "../../Config/Config.h"
#include "../../Java.h"
#include "AcInternal.h"
#include "AcModels.h"
#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <numeric>
#include <sstream>

namespace Anticheat {
namespace Checks {

static const double kPi = 3.14159265358979323846;

static bool isSwordId(int id) {
  return id == 267 || id == 268 || id == 272 || id == 276 || id == 283;
}
static bool isPlaceableBlockId(int id) { return id > 0 && id < 256; }

static bool isFoodOrBowOrSword(int id) {
  if (isSwordId(id))
    return true;
  if (id == 261)
    return true;
  static const int kFoods[] = {260, 282, 297, 319, 320, 322, 349, 350, 357,
                               360, 363, 364, 365, 366, 367, 375, 391, 392,
                               393, 396, 400, 411, 412, 423, 424, 432};
  for (int f : kFoods)
    if (f == id)
      return true;
  return false;
}

static double wrapAngle180(double a) {
  while (a >= 180.0)
    a -= 360.0;
  while (a < -180.0)
    a += 360.0;
  return a;
}

// ---------------------------------------------------------------------------
// NoSlow — sprinting while an action that should slow you down is in progress.
// ---------------------------------------------------------------------------
class NoSlowCheck : public Check {
public:
  NoSlowCheck()
      : Check(CHECK_NOSLOW, "NoSlow", "Sprint while blocking/eating/drawing.") {
  }
  void onPlayerTick(PlayerData &p, JNIEnv *, jobject) override {
    if (!Config::isAnticheatNoSlowEnabled())
      return;
    if (p.isLocalPlayer && !Config::isAnticheatCheckSelfEnabled())
      return;

    bool slowItem = false;
    if (p.isBlocking && isSwordId(p.heldItemId))
      slowItem = true;
    if (p.isUsingItem &&
        (p.heldItemId == 261 || isFoodOrBowOrSword(p.heldItemId)))
      slowItem = true;

    auto &st = p.checks[CHECK_NOSLOW];
    bool now = slowItem && p.isSprinting && !p.isRiding;
    ULONGLONG ms = GetTickCount64();

    if (!now) {
      st.stateSince = 0;
      return;
    }
    if (!st.stateSince) {
      st.stateSince = ms;
      return;
    }
    ULONGLONG dur = ms - st.stateSince;
    if (dur > 200) {
      std::ostringstream ss;
      ss << "duration: " << dur << "ms";
      flag(p, this, ss.str(), 1.0);
      st.stateSince = 0;
    }
  }
};

// ---------------------------------------------------------------------------
// AutoBlock — the sword goes back up after an attack without a human doing the
// releasing and re-pressing.
//
// One "the sword was up right after a swing" event proves nothing: that is
// exactly what a good manual blockhitter produces. What separates a macro from
// a person is the *shape* of the evidence, so three independent properties are
// measured and only their agreement raises violation level.
//
//   1. Re-block delay agreement. Every attack that is followed by a block
//      contributes the gap, in Minecraft ticks, between the two. A person
//      spreads these across a handful of values; a macro puts nearly all of
//      them on one. modalRatio() measures that directly and is immune to the
//      tick quantisation that makes a millisecond standard deviation useless.
//   2. Blocks per second. Each re-press is a physical mouse action. Sustained
//      rates above what a hand produces are their own evidence.
//   3. Follow-through rate. What fraction of attacks are followed by a block
//      at all. A person misses some; a macro does not.
// ---------------------------------------------------------------------------
class AutoBlockCheck : public Check {
public:
  AutoBlockCheck()
      : Check(CHECK_AUTOBLOCK, "AutoBlock",
              "Sword re-blocks after attacks with machine timing") {}

  void onPlayerTick(PlayerData &p, JNIEnv *, jobject) override {
    if (!Config::isAnticheatAutoBlockEnabled())
      return;
    if (p.isLocalPlayer && !Config::isAnticheatCheckSelfEnabled())
      return;
    if (!isSwordId(p.heldItemId)) {
      p.pendingSwingOpen = false;
      return;
    }

    const long tick = p.currentTick;
    const ULONGLONG ms = GetTickCount64();

    // Every fresh press of the block button is one physical action.
    if (p.isBlocking && !p.wasBlocking)
      p.blockStarts.push(ms);

    // A new attack animation opens a window; the sword coming back up inside
    // it closes the window and records the delay.
    bool swingEdge = (p.swingEdgeTick == tick);
    if (swingEdge) {
      // An attack that never got its block is evidence too, so close any
      // window still open from the previous swing before starting a new one.
      if (p.pendingSwingOpen)
        p.swingBlocked.push(0);
      p.pendingSwingOpen = true;
      p.pendingSwingTick = tick;
    }

    if (p.pendingSwingOpen) {
      long age = tick - p.pendingSwingTick;
      if (p.isBlocking && age >= 0 && age <= kMaxReblockTicks) {
        p.reblockDelayTicks.push((uint8_t)age);
        p.swingBlocked.push(1);
        p.pendingSwingOpen = false;
      } else if (age > kMaxReblockTicks) {
        p.swingBlocked.push(0);
        p.pendingSwingOpen = false;
      }
    }

    AutoBlockEvidence e;
    e.samples = p.reblockDelayTicks.size();
    uint8_t mode = 0;
    e.agreement = modalRatio(p.reblockDelayTicks, &mode);
    e.modeTicks = mode;
    e.blocksPerSecond = p.blockStarts.ratePerSecond(ms, 1000);
    e.followThrough = mean(p.swingBlocked);

    Verdict v = scoreAutoBlock(e);
    if (!v.flag)
      return;

    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2) << "delay:" << (int)e.modeTicks
       << "t agree:" << e.agreement << " bps:" << std::setprecision(1)
       << e.blocksPerSecond << " follow:" << std::setprecision(2)
       << e.followThrough << " conf:" << v.confidence;
    flag(p, this, ss.str(), v.vlGain);

    // Do not let the same window of samples flag again on the next tick.
    p.reblockDelayTicks.clear();
    p.swingBlocked.clear();
  }

private:
  // A re-block later than this is a person deciding to block, not a macro
  // finishing an attack.
  static const long kMaxReblockTicks = 6;
};

// ---------------------------------------------------------------------------
// KillAura — the aim is better than a hand produces.
// ---------------------------------------------------------------------------
class KillAuraCheck : public Check {
public:
  KillAuraCheck()
      : Check(CHECK_KILLAURA, "KillAura", "Aim locks onto targets") {}

  void onPlayerTick(PlayerData &p, JNIEnv *, jobject) override {
    if (!Config::isAnticheatKillAuraEnabled())
      return;
    if (p.isLocalPlayer && !Config::isAnticheatCheckSelfEnabled())
      return;
    if (!p.lastAttack.valid)
      return;
    if (p.lastAttack.tick == p.lastAttackHandledTick)
      return;
    p.lastAttackHandledTick = p.lastAttack.tick;

    const AttackEvent &a = p.lastAttack;
    p.aimErrors.push((float)a.aimErrorDeg);

    // Hitting something that is not in front of you needs no statistics.
    if (a.aimErrorDeg > kBehindDeg) {
      std::ostringstream ss;
      ss << std::fixed << std::setprecision(1) << "hit at " << a.aimErrorDeg
         << " deg off";
      flag(p, this, ss.str(), 4.0);
      return;
    }

    // Two different players damaged inside one swing window is aura, not aim.
    int distinct = countRecentVictims(p, a.tick);
    if (distinct >= 2) {
      std::ostringstream ss;
      ss << distinct << " targets in " << kMultiWindowTicks << "t";
      flag(p, this, ss.str(), 3.0);
      return;
    }

    AimEvidence e;
    e.samples = p.aimErrors.size();
    e.meanErrorDeg = mean(p.aimErrors);
    e.stddevErrorDeg = stddev(p.aimErrors);
    e.yawDeltaBeforeHit = a.yawDeltaBefore;
    e.lastErrorDeg = a.aimErrorDeg;

    Verdict v = scoreAim(e);
    if (!v.flag)
      return;

    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2) << "err:" << e.meanErrorDeg
       << "+-" << e.stddevErrorDeg << " snap:" << std::setprecision(1)
       << a.yawDeltaBefore << " conf:" << std::setprecision(2) << v.confidence;
    flag(p, this, ss.str(), v.vlGain);
    p.aimErrors.clear();
  }

private:
  static int countRecentVictims(const PlayerData &p, long tick) {
    int ids[4];
    int n = 0;
    for (int i = 0; i < 4; ++i) {
      if (!p.recentVictims[i])
        continue;
      if (tick - p.recentVictimTicks[i] > kMultiWindowTicks)
        continue;
      bool dup = false;
      for (int j = 0; j < n; ++j)
        if (ids[j] == p.recentVictims[i])
          dup = true;
      if (!dup)
        ids[n++] = p.recentVictims[i];
    }
    return n;
  }

  static const long kMultiWindowTicks = 4;
  static constexpr double kBehindDeg = AimLimits::kBehindDeg;
};

// ---------------------------------------------------------------------------
// Reach — hits landing from further than an arm reaches.
// ---------------------------------------------------------------------------
class ReachCheck : public Check {
public:
  ReachCheck() : Check(CHECK_REACH, "Reach", "Attacks from too far away") {}

  void onPlayerTick(PlayerData &p, JNIEnv *, jobject) override {
    if (!Config::isAnticheatReachEnabled())
      return;
    if (p.isLocalPlayer && !Config::isAnticheatCheckSelfEnabled())
      return;
    if (!p.lastAttack.valid)
      return;
    if (p.lastAttack.tick == p.lastAttackHandledTickReach)
      return;
    p.lastAttackHandledTickReach = p.lastAttack.tick;

    double d = p.lastAttack.distance;
    if (d <= 0.0 || d > ReachLimits::kAbsurd)
      return; // pairing is probably wrong, not a long arm
    p.reachDistances.push((float)d);

    ReachEvidence e;
    e.samples = p.reachDistances.size();
    double over = 0.0, worst = 0.0;
    for (size_t i = 0; i < p.reachDistances.size(); ++i) {
      double v0 = (double)p.reachDistances[i];
      if (v0 > ReachLimits::kLimit)
        over += 1.0;
      worst = std::max(worst, v0);
    }
    e.overLimitFraction = e.samples ? over / (double)e.samples : 0.0;
    e.worstDistance = worst;

    Verdict v = scoreReach(e);
    if (!v.flag)
      return;

    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2) << "max:" << e.worstDistance
       << " over:" << (int)(e.overLimitFraction * 100) << "%";
    flag(p, this, ss.str(), v.vlGain);
    p.reachDistances.clear();
  }
};

// ---------------------------------------------------------------------------
// Velocity — knockback that does not move the player.
// ---------------------------------------------------------------------------
class VelocityCheck : public Check {
public:
  VelocityCheck()
      : Check(CHECK_VELOCITY, "Velocity", "Takes hits without knockback") {}

  void onPlayerTick(PlayerData &p, JNIEnv *, jobject) override {
    if (!Config::isAnticheatVelocityEnabled())
      return;
    if (p.isLocalPlayer && !Config::isAnticheatCheckSelfEnabled())
      return;

    const long tick = p.currentTick;

    // Damage just landed: remember where they were standing.
    if (p.hurtTime > p.lastHurtTime && p.hurtTime > 0 && !p.kbPending) {
      p.kbPending = true;
      p.kbTick = tick;
      p.kbFromX = p.posX;
      p.kbFromZ = p.posZ;
      return;
    }

    if (!p.kbPending)
      return;
    if (tick - p.kbTick < kMeasureTicks)
      return;

    double dx = p.posX - p.kbFromX;
    double dz = p.posZ - p.kbFromZ;
    p.kbDistances.push((float)std::sqrt(dx * dx + dz * dz));
    p.kbPending = false;

    if (p.kbDistances.size() < kMinSamples)
      return;

    // Any single hit can be absorbed by a wall or a block, so only a run of
    // hits that all failed to move the player counts.
    double stopped = 0.0;
    for (size_t i = 0; i < p.kbDistances.size(); ++i)
      if (p.kbDistances[i] < kMinTravel)
        stopped += 1.0;
    stopped /= (double)p.kbDistances.size();

    if (stopped < 0.8)
      return;

    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2) << "travel:" << mean(p.kbDistances)
       << "b over " << (int)p.kbDistances.size() << " hits";
    flag(p, this, ss.str(), 2.0);
    p.kbDistances.clear();
  }

private:
  static const long kMeasureTicks = 4;
  static const size_t kMinSamples = 8;
  static constexpr double kMinTravel = 0.18;
};

// ---------------------------------------------------------------------------
// Eagle — the mechanical sneak-release rhythm of a bridging assist.
// ---------------------------------------------------------------------------
class EagleCheck : public Check {
public:
  EagleCheck()
      : Check(CHECK_EAGLE, "Eagle", "LegitScaffold (mechanical sneak-bridge)") {
  }
  void onPlayerTick(PlayerData &p, JNIEnv *, jobject) override {
    if (!Config::isAnticheatEagleEnabled())
      return;
    if (p.isLocalPlayer && !Config::isAnticheatCheckSelfEnabled())
      return;

    long tick = p.currentTick;
    bool released = p.wasSneaking && !p.isSneaking;

    auto &state = p.checks[CHECK_EAGLE];
    if (state.vl > 0)
      state.vl -= 0.01;

    if (!released)
      return;

    long end = p.lastCrouchEndTick;
    long start = p.lastCrouchStartTick;
    long swing = p.lastSwingTick;
    int crouchDur = (int)(end - start);

    bool quickCrouch = crouchDur >= 1 && crouchDur <= 2;
    bool swingTiming = (swing >= end - 1 && swing <= end + 1);

    int fastSamples = 0;
    for (size_t i = 0; i < p.crouchDurations.size() && i < 5; i++) {
      int d = p.crouchDurations[i];
      if (d >= 1 && d <= 2)
        fastSamples++;
    }
    bool consistent = (fastSamples >= 4);

    bool holdingBlock = isPlaceableBlockId(p.heldItemId);
    bool lookingDown = p.rotPitch >= 60.0f;

    if (!lookingDown || !holdingBlock)
      return;
    if (!(quickCrouch && swingTiming && consistent))
      return;

    state.vl += 1.0;
    if (state.vl < 5.0)
      return;
    if (tick - p.lastEaglePatternTick < 10)
      return;

    std::ostringstream ss;
    ss << "Eagle Bridge (VL: " << (int)state.vl << ") | " << crouchDur
       << "t crouch | Samples: " << fastSamples << "/5";
    flag(p, this, ss.str(), 5.0);
    p.lastEaglePatternTick = tick;
    state.vl = 0;
  }
};

// ---------------------------------------------------------------------------
// Scaffold — bridging backwards faster and steadier than the movement allows.
// ---------------------------------------------------------------------------
class ScaffoldCheck : public Check {
public:
  ScaffoldCheck()
      : Check(CHECK_SCAFFOLD, "Scaffold", "Illegal bridging speed/angle.") {}

  void onPlayerTick(PlayerData &p, JNIEnv *, jobject) override {
    if (!Config::isAnticheatScaffoldEnabled())
      return;
    if (p.isLocalPlayer && !Config::isAnticheatCheckSelfEnabled())
      return;
    if (p.isRiding)
      return;
    if (p.positions.size() < 5)
      return;

    const size_t n = p.positions.size();
    // Ring returns by value, so these are copies rather than references.
    const auto cur = p.positions[n - 1];
    const auto p1 = p.positions[n - 2];
    const auto p2 = p.positions[n - 3];
    const auto p3 = p.positions[n - 4];
    const auto p4 = p.positions[n - 5];
    long tick = p.currentTick;

    double dx = (cur.x - p1.x) * 20.0;
    double dz = (cur.z - p1.z) * 20.0;
    double sxz2 = dx * dx + dz * dz;
    double sxz = std::sqrt(sxz2);
    double sy = (p1.y - p2.y) * 20.0;
    double accY = 50.0 * ((p1.y - p2.y) - (p3.y - p4.y));

    double moveAngle = std::atan2(dz, dx) * 180.0 / kPi - 90.0;
    double lookAngle = wrapAngle180(p.rotYaw);
    double diff = std::fmod(moveAngle - lookAngle + 360.0, 360.0);
    if (diff > 180.0)
      diff -= 360.0;

    bool eligible = p.isSwingInProgress && p.hurtTime == 0 &&
                    p.rotPitch > 50.0f && sxz2 > 9.0 && sxz2 < 100.0 &&
                    isPlaceableBlockId(p.heldItemId) &&
                    std::fabs(diff) > 165.0 && std::fabs(accY) >= 0.001;

    bool flagged = false;
    if (eligible) {
      double pitchF = std::clamp((p.rotPitch - 50.0) / 40.0, 0.0, 1.0);
      double angF = std::clamp((std::fabs(diff) - 165.0) / 15.0, 0.0, 1.0);
      double base = 0.0;
      const char *type = "";

      if (sy >= 4.0 && sy <= 15.0 && accY > -25.0) {
        type = "tower";
        double yF = std::clamp((sy - 4.0) / 11.0, 0.0, 1.0);
        double aF = std::clamp((accY + 25.0) / 25.0, 0.0, 1.0);
        double sev = pitchF * 0.3 + angF * 0.3 + yF * 0.3 + aF * 0.1;
        base = 3.0 + sev * 3.0;
      } else if (sy >= -1.0 && sy <= 4.0 && std::fabs(sy) > 0.005 &&
                 sxz2 > 25.0) {
        type = "horizontal";
        double hF = std::clamp((sxz - 5.0) / 5.0, 0.0, 1.0);
        double sev = pitchF * 0.3 + angF * 0.3 + hF * 0.4;
        base = 3.0 + sev * 3.0;
      }

      if (*type) {
        int consec = 0;
        if (p.lastScaffoldType == type && (tick - p.lastScaffoldTime) < 40)
          consec = p.scaffoldConsecutive + 1;
        p.scaffoldConsecutive = consec;
        p.lastScaffoldType = type;
        p.lastScaffoldTime = tick;

        double consecM = 1.0 + std::min(consec, 5) * 0.2;
        double finalVL = std::min(2.0, base * consecM * 0.25);

        std::ostringstream ss;
        ss << "type:" << type << " angle:" << std::fixed << std::setprecision(1)
           << diff << " spd:" << sxz << " accY:" << accY;
        flag(p, this, ss.str(), finalVL);
        flagged = true;
      }
    }

    if (!flagged && (tick - p.lastScaffoldTime) > 60)
      p.scaffoldConsecutive = 0;
  }
};

Check *makeNoSlow() { return new NoSlowCheck(); }
Check *makeAutoBlock() { return new AutoBlockCheck(); }
Check *makeEagle() { return new EagleCheck(); }
Check *makeScaffold() { return new ScaffoldCheck(); }
Check *makeKillAura() { return new KillAuraCheck(); }
Check *makeReach() { return new ReachCheck(); }
Check *makeVelocity() { return new VelocityCheck(); }

} // namespace Checks
} // namespace Anticheat
