#pragma once
#include <cstdint>
#include <cstddef>
namespace lemming::aim_offsets {
// 1.0.0 ARM64: source hashes and provenance in docs/AIM_OFFSETS_1.0.0_ARM64.md.
inline constexpr std::uintptr_t kCameraTick=0xA0903DC; // ios::iqkr(ExecuteTime)
inline constexpr std::uintptr_t kCameraClamp=0xA08C2B4; // ios::kjuc(Vector2) -> Vector2
inline constexpr std::uintptr_t kCameraApply=0xA08C460; // ios::iwlo(Vector2)
inline constexpr std::uintptr_t kCameraRefresh=0xA08C4F8; // ios::jqcv()
inline constexpr std::uintptr_t kShotEmit=0x9739FA4; // jxy::rxjx(WeaponContext,Span<Ray>): actually emits shot data
inline constexpr std::uintptr_t kShotCallsite=0x973A9E0; // BL rxjx inside jxy::vrsg: 0x97fffd71
// Verified native/dump provenance in docs/game/ANGLE_ASSISTS_1.0.0_ARM64.md.
inline constexpr std::uintptr_t kNativeRaycast=0x538BD0C;
inline constexpr std::uintptr_t kDefaultPhysicsScene=0x904D9C4;
inline constexpr std::uintptr_t kObjectEntityId=0x67F0658;
inline constexpr std::uintptr_t kRecoilScale=0x972976C;
inline constexpr std::uintptr_t kRecoilPointLoad=0x9736FD8;
inline constexpr std::uintptr_t kRecoilMultiplierLoad=0x97297B0;
inline constexpr std::size_t kRecoilActualPoint=0xA8,kRecoilMultiplier=0x70;
inline constexpr std::uintptr_t kInputTick=0xAAF79D0,kCommandAttack=0x944E2A8,kCommandScope=0x945BB7C,kCommandScopeHold=0x945B774,kCommandLook=0x94479F8;
// Verified ltc early/late command consumer pair, v16.
inline constexpr std::uintptr_t kCommandGate=0x94A0990,kCommandGateSite=0x94A3354;
inline constexpr std::uintptr_t kCommandEnd=0x949ECD8,kCommandEndSite=0x94A6A1C;
inline constexpr std::uint8_t kAttackState=1; // WeaponState.Attack _raw, _modifiers ignored
}
