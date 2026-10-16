#pragma once
#include <cstdint>
#include <cmath>
namespace lemming::aim::input_init {
enum Reason {NotAttempted,Ready,NoLocal,WrongThread,InvalidLocal,ComponentMissing,SignatureRejected,ClassRejected,SlotMissing,SlotReadOnly,WriteProbeFailed,ReadbackFailed,UnsupportedArchitecture,RuntimeNotReady,CallsiteRejected,RelayAllocationFailed,RelayProtectionFailed,TextProtectionFailed,RestoreProtectionFailed,PairReadbackFailed};
inline const char* reasonText(int r){switch(r){case CallsiteRejected:return "command-callsite-changed";case RelayAllocationFailed:return "command-relay-allocation-failed";case RelayProtectionFailed:return "command-relay-RX-failed";case TextProtectionFailed:return "command-text-protection-failed";case RestoreProtectionFailed:return "command-restore-RX-failed";case PairReadbackFailed:return "command-pair-verify-or-rollback-failed";case Ready:return "ready";case NoLocal:return "local-player-missing";case WrongThread:return "wrong-thread";case InvalidLocal:return "local-state-rejected";case ComponentMissing:return "input-component-missing";case SignatureRejected:return "native-signature-rejected";case ClassRejected:return "input-class-rejected";case SlotMissing:return "exact-vtable-slot-missing";case SlotReadOnly:return "vtable-slot-readonly";case WriteProbeFailed:return "slot-write-probe-failed";case ReadbackFailed:return "slot-readback-failed";case UnsupportedArchitecture:return "unsupported-architecture";case RuntimeNotReady:return "runtime-not-ready";default:return "not-attempted";}}
struct Retry {
 std::uintptr_t owner=0;double next=0,last=0;
 bool due(double now,bool enabled,bool ready,std::uintptr_t local){
  if(!std::isfinite(now))return false;
  if(!enabled){*this={};return false;}
  if(ready)return false;
  if(!local)return false;
  if(owner!=local||now<last){owner=local;next=0;}
  last=now;if(now<next)return false;next=now+1000;return true;
 }
};
struct ComponentStats {unsigned arrays=0,entries=0,objects=0,foreign=0;bool dictionary=false;};
}
