#pragma once
#include "AcStats.h"
#include <Windows.h>
#include <jni.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Anticheat {

// Checks are addressed by index rather than by name. flag() used to hash the
// check's name into a per-player std::unordered_map<std::string, ...> on every
// call, and Eagle did the same lookup every tick for every tracked player.
enum CheckId {
  CHECK_NOSLOW = 0,
  CHECK_AUTOBLOCK,
  CHECK_EAGLE,
  CHECK_SCAFFOLD,
  CHECK_KILLAURA,
  CHECK_REACH,
  CHECK_VELOCITY,
  CHECK_COUNT
};

// What we managed to reconstruct about one attack. The checks only see a
// single PlayerData, so the attacker/victim pairing is resolved once per tick
// in AcCore (where the whole player map is in scope) and left here for
// KillAura and Reach to read.
struct AttackEvent {
  long tick = 0;
  int victimEid = 0;
  double distance = 0.0;     // attacker eye -> victim centre, blocks
  double aimErrorDeg = 0.0;  // angle between look vector and victim direction
  double yawDeltaBefore = 0.0; // |yaw change| over the tick before the hit
  bool valid = false;
};

struct PlayerData {
  std::string name;
  int entityId = 0;
  bool isLocalPlayer = false;
  bool isFlagged = false;
  long currentTick = 0;

  double posX = 0, posY = 0, posZ = 0;
  double lastPosX = 0, lastPosY = 0, lastPosZ = 0;
  bool havePos = false;
  float rotYaw = 0, rotPitch = 0;
  float lastRotYaw = 0, lastRotPitch = 0;
  bool onGround = false;
  bool isBlocking = false;
  bool isUsingItem = false;
  bool isSwingInProgress = false;
  bool isSneaking = false;
  bool isSprinting = false;
  bool isRiding = false;
  int heldItemId = 0;
  int hurtTime = 0;
  int lastHurtTime = 0;

  bool wasBlocking = false;
  bool wasUsingItem = false;
  bool wasSwingInProgress = false;
  bool wasSneaking = false;

  // isSwingInProgress is smeared over the whole animation, so its rising edge
  // does not mark an attack. rawSwing is the animation flag as read this tick;
  // its rising edge is one attack, and that is what the combat checks pair on.
  bool rawSwing = false;
  bool wasRawSwing = false;
  long swingEdgeTick = -1;

  long lastBlockStartMs = 0;
  long lastSwingMs = 0;

  struct Sample {
    double x, y, z;
    ULONGLONG t;
  };
  Ring<Sample, 20> positions;
  void pushPosition(double x, double y, double z) {
    positions.push(Sample{x, y, z, GetTickCount64()});
  }

  struct Swing {
    long ms;
    bool wasBlockingBefore;
    int afterTrack;
  };
  std::vector<Swing> swings;
  long lastSwingDetectedMs = 0;

  long lastCrouchStartTick = 0;
  long lastCrouchEndTick = 0;
  long lastSwingTick = 0;
  long lastEaglePatternTick = 0;
  long lastOnGroundTick = 0;
  std::vector<int> crouchDurations;

  int useItemTime = 0;
  double autoBlockVL = 0;
  int autoBlockConsecutive = 0;

  int scaffoldConsecutive = 0;
  std::string lastScaffoldType;
  long lastScaffoldTime = 0;

  // ---- AutoBlock evidence -------------------------------------------------
  // Ticks between an attack animation starting and the sword going back up.
  // A person has to physically release and re-press; a macro does not, so the
  // interesting property is agreement between samples, not their size.
  Ring<uint8_t, 16> reblockDelayTicks;
  EventWindow<32> blockStarts;   // for blocks-per-second
  Ring<uint8_t, 16> swingBlocked; // 1 when a block followed the swing quickly
  long pendingSwingTick = 0;      // swing waiting to be paired with a re-block
  bool pendingSwingOpen = false;

  // ---- Aim / combat evidence ---------------------------------------------
  AttackEvent lastAttack;
  // KillAura and Reach consume the same event, so each keeps its own cursor.
  long lastAttackHandledTick = 0;
  long lastAttackHandledTickReach = 0;
  Ring<float, 16> aimErrors;      // degrees off target at the hit tick
  Ring<float, 16> reachDistances; // blocks
  int recentVictims[4] = {0, 0, 0, 0};
  long recentVictimTicks[4] = {0, 0, 0, 0};

  // ---- Knockback evidence -------------------------------------------------
  bool kbPending = false;
  long kbTick = 0;
  double kbFromX = 0, kbFromZ = 0;
  Ring<float, 12> kbDistances;

  struct CheckState {
    double vl = 0;
    ULONGLONG lastAlertMs = 0;
    ULONGLONG lastFlagMs = 0;
    // When the check's own condition started holding, 0 when it is not.
    // Lives here rather than on the Check object because one Check instance
    // serves every tracked player.
    ULONGLONG stateSince = 0;
  };
  CheckState checks[CHECK_COUNT];
};

class Check {
public:
  Check(CheckId id, const char *name, const char *desc)
      : m_id(id), m_name(name), m_desc(desc) {}
  virtual ~Check() = default;
  virtual void onPlayerTick(PlayerData &p, JNIEnv *env, jobject entity) = 0;
  CheckId id() const { return m_id; }
  const char *name() const { return m_name; }
  const char *desc() const { return m_desc; }

private:
  CheckId m_id;
  const char *m_name;
  const char *m_desc;
};

namespace Checks {
Check *makeNoSlow();
Check *makeAutoBlock();
Check *makeEagle();
Check *makeScaffold();
Check *makeKillAura();
Check *makeReach();
Check *makeVelocity();
} // namespace Checks

void flag(PlayerData &p, Check *check, const std::string &info, double vl);
void tickFromRenderThread();
void debugLog(const char *fmt, ...);
void abLog(const char *fmt, ...);
void mapLog(const char *fmt, ...);

} // namespace Anticheat
