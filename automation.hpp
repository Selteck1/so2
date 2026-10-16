#pragma once
#include "features/aim/core.hpp"
#include "features/aim/prediction.hpp"
#include "features/aim/silent_accuracy.hpp"
#include "features/aim/native_pair.hpp"
#include "features/aim/offsets.hpp"
#include "features/aim/debug.hpp"
#include "features/aim/hook_support.hpp"
#include "features/aim/assists.hpp"
#include "features/visuals/esp_runtime.hpp"
#include <sys/mman.h>
#include <cstring>
namespace lemming::aim::runtime {
namespace ed=lemming::esp::detail;using Clock=std::chrono::steady_clock;
struct Bounds{Vec3 center,extents;};struct Ray{Vec3 origin,direction;};struct ExecuteTime{float time,delta;};
static_assert(sizeof(Bounds)==24&&sizeof(Ray)==24&&sizeof(ExecuteTime)==8,"ARM64 value type ABI");
using BoundsFn=void(*)(std::uintptr_t,Bounds*);using ClampFn=Angles(*)(void*,Angles,const void*);using ApplyFn=void(*)(void*,Angles,const void*);using RefreshFn=void(*)(void*,const void*);using CameraTickFn=void(*)(void*,ExecuteTime,const void*);
// Verified call-site: x0=jxy, x1=unboxed WeaponContext*, x2=Ray*, x3=length/padding.
// rxjx EMITS the pre-generated rays; redirect before original, not after it.
using EmitFn=void(*)(void*,void*,Ray*,std::uint64_t);
inline std::atomic<CameraTickFn> g_cameraOriginal{nullptr};inline EmitFn g_emitOriginal=nullptr;inline BoundsFn g_bounds=nullptr;inline ClampFn g_clamp=nullptr;inline ApplyFn g_apply=nullptr;inline RefreshFn g_refresh=nullptr;inline bool g_functionsValid=false;
inline std::uintptr_t g_lastNormal=0,g_lastSilent=0;inline Clock::time_point g_lastInstall{};inline void* g_relay=nullptr;
struct Actor{void* player=nullptr;void* world=nullptr;float hp=0;int team=0;std::array<HitVolume,32> volumes{};unsigned count=0;std::array<std::int32_t,33> colliderIds{};unsigned colliderCount=0;Clock::time_point captured{};prediction::Motion motion{};};inline std::array<Actor,128> g_actors{};
inline bool unityCode(std::uintptr_t p){auto b=ed::g_runtime.unityBase;return b&&p>=b+offsets::elf::kTextBegin&&p<b+offsets::elf::kTextEnd;}
template<class T> inline bool field(void* o,std::size_t off,T& out){return o&&ed::readValue(reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(o)+off),out);}
inline void* object(void* o,std::size_t off){return o?ed::readObject(reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(o)+off)):nullptr;}
inline bool named(void* o,const char* n){return o&&ed::nameEquals(ed::className(ed::objectClass(o)),n);}
inline std::uintptr_t native(void* o,const char* n=nullptr){if(!o||(n&&!named(o,n)))return 0;auto p=reinterpret_cast<std::uintptr_t>(object(o,0x10));std::uintptr_t vt=0,first=0;if(!p||!ed::readValue(reinterpret_cast<void*>(p),vt)||!ed::readValue(reinterpret_cast<void*>(vt),first)||!unityCode(first))return 0;return p;}
inline void* part(void* player,const char* n){void* list=object(object(player,offsets::field::mfc::kPartsContainer),0x10);std::int32_t count=0;if(!field(list,0x18,count)||count<=0||count>64)return nullptr;void* arr=object(list,0x10);std::uintptr_t len=0;if(!field(arr,0x18,len)||len<static_cast<unsigned>(count)||len>256)return nullptr;for(int i=0;i<count;++i){void* p=object(arr,0x20+std::size_t(i)*8);if(named(p,n))return p;}return nullptr;}
inline bool localAlive(void* p,ed::PlayerSnapshot& s,void*& world){std::uint8_t local=0;world=object(p,offsets::field::mfc::kWorld);int mask=0;if(p)mask|=1;if(p&&ed::objectClass(p)==ed::g_runtime.playerClass)mask|=2;if(field(p,offsets::field::mfc::kLocalOwned,local))mask|=4;if(local==1)mask|=8;if(world)mask|=16;if((mask&31)==31&&ed::readPlayerSnapshot(p,s))mask|=32;if(s.localOwned)mask|=64;if(s.statsValid)mask|=128;if(s.hp>0)mask|=256;if(ed::validTeam(s.team))mask|=512;if(s.weaponPart)mask|=1024;if(s.activeWeapon)mask|=2048;debug::localMask.store(mask);debug::localRaw.store(local);debug::localHp.store(s.hp);debug::localTeam.store(s.team);return mask==4095;}
inline void* localPlayer(){std::lock_guard<std::mutex> l(ed::g_hookPlayerMutex);return ed::g_hookLocalPlayer;}
// The next header deliberately shares this runtime namespace.
#include "features/aim/assist_runtime.hpp"
inline void capture(void* p,bool collectIds=false){debug::captureCalls.fetch_add(1);
 ed::PlayerSnapshot s{};if(!ed::readPlayerSnapshot(p,s)||!s.statsValid||!s.hp||!ed::validTeam(s.team)){debug::captureSnapshot.fetch_add(1);return;}if(s.localOwned){debug::captureLocal.fetch_add(1);return;}
 Actor a{};a.player=p;a.world=object(p,offsets::field::mfc::kWorld);a.hp=s.hp;a.team=s.team;a.captured=Clock::now();if(!a.world){debug::captureWorld.fetch_add(1);return;}
 void* arr=object(part(p,"jgi"),offsets::field::jgi::kHitBoxColliders);std::uintptr_t count=0;if(!field(arr,0x18,count)||!count||count>64){debug::captureArray.fetch_add(1);return;}
 esp::Vec3 root{};if(!ed::readPlayerPoseSnapshot(p,root)){debug::capturePose.fetch_add(1);return;}
 for(std::uintptr_t i=0;i<count&&a.count<a.volumes.size();++i){void* hb=object(arr,0x20+i*8);debug::hitboxes.fetch_add(1);if(!named(hb,"HitBoxCollider")){debug::badClass.fetch_add(1);continue;}int bone=-1;if(!field(hb,0x20,bone)){debug::unknownBone.fetch_add(1);continue;}debug::boundsBone(bone);if(bodyForBone(bone)<0){debug::unknownBone.fetch_add(1);continue;}void* c=object(hb,0x28);if(!named(c,"BoxCollider")&&!named(c,"CapsuleCollider")&&!named(c,"SphereCollider")){debug::badCollider.fetch_add(1);continue;}auto ptr=native(c);if(!ptr){debug::badNative.fetch_add(1);continue;}std::uintptr_t vt=0,fn=0;if(!ed::readValue(reinterpret_cast<void*>(ptr),vt)||!ed::readValue(reinterpret_cast<void*>(vt+0xf0),fn)||!unityCode(fn)){debug::badVirtual.fetch_add(1);continue;}
  Bounds b{};g_bounds(ptr,&b);HitVolume v{b.center,b.extents,bone};if(!validVolume(v)){debug::badBounds.fetch_add(1);continue;}if(length(sub(v.center,{root.x,root.y,root.z}))>4){debug::farBounds.fetch_add(1);continue;}a.volumes[a.count++]=v;if(collectIds&&g_entityId&&a.colliderCount<a.colliderIds.size())a.colliderIds[a.colliderCount++]=g_entityId(c,nullptr);}
 if(collectIds&&g_entityId&&a.colliderCount<a.colliderIds.size()){void* bindings=object(part(p,"dwc"),0x38);void* capsule=object(bindings,0x28);if(named(bindings,"PlayerBindings")&&(named(capsule,"CapsuleCollider")||named(capsule,"CharacterController"))&&native(capsule))a.colliderIds[a.colliderCount++]=g_entityId(capsule,nullptr);}
 Actor* dst=nullptr;for(auto& slot:g_actors){if(slot.player==p){dst=&slot;break;}if(!dst&&(!slot.player||a.captured-slot.captured>std::chrono::seconds(1)))dst=&slot;}if(dst){if(dst->player==p&&dst->world==a.world&&dst->team==a.team&&dst->hp>0)a.motion=dst->motion;{std::lock_guard<std::mutex> lock(ed::g_playerPoseMutex);for(const auto& pose:ed::g_playerPoses)if(pose.player==p&&pose.world==a.world){a.motion.update({pose.position.x,pose.position.y,pose.position.z},std::chrono::duration<double>(pose.captured.time_since_epoch()).count(),reinterpret_cast<std::uintptr_t>(a.world),a.team);break;}}*dst=a;debug::storedActors.fetch_add(1);debug::storedVolumes.fetch_add(a.count);}
 int actors=0,volumes=0;for(const auto& slot:g_actors)if(slot.count&&a.captured-slot.captured<std::chrono::milliseconds(100)){++actors;volumes+=slot.count;}debug::freshActors.store(actors);debug::freshVolumes.store(volumes);
}
inline Selection select(const Settings& s,void* world,int team,Vec3 origin,Vec3 forward,std::uintptr_t& last){
 debug::selectCalls.fetch_add(1);g_visibilityBudget=64;Selection best{};auto now=Clock::now();float nearest=180,nearestRange=0;
 for(const auto& a:g_actors){if(!a.player)continue;debug::selectActors.fetch_add(1);if(a.world!=world||a.hp<=0||a.team==team||now-a.captured>std::chrono::milliseconds(100))continue;debug::selectFresh.fetch_add(1);
 std::uint16_t hp=0;auto stats=object(a.player,offsets::field::ake::kStats);if(ed::objectClass(a.player)!=ed::g_runtime.playerClass||object(a.player,offsets::field::mfc::kWorld)!=world||!field(stats,offsets::field::ylw::kHp,hp)||!hp)continue;debug::selectLive.fetch_add(1);
 for(unsigned i=0;i<a.count;++i){debug::selectVolumes.fetch_add(1);auto& v=a.volumes[i];int body=bodyForBone(v.bone);if(body<0||!(s.bodies&(1u<<body)))debug::bodyReject.fetch_add(1);else{std::array<Vec3,7> ps{};auto n=samples(v,s,ps);auto f=normalized(forward);for(unsigned j=0;j<n;++j){debug::pointCandidates.fetch_add(1);auto d=sub(ps[j],origin);float range=length(d);if(!std::isfinite(range)||range<.2f||range>s.maxDistance){debug::rangeReject.fetch_add(1);continue;}float angle=std::acos(std::clamp(dot(normalized(d),f),-1.f,1.f))*57.295779513f;if(angle<nearest){nearest=angle;nearestRange=range;}if(!std::isfinite(angle)||angle>s.fov)debug::fovReject.fetch_add(1);else debug::accepted.fetch_add(1);}}consider(best,s,reinterpret_cast<std::uintptr_t>(a.player),float(hp),origin,forward,v,last,visibilityAccept,const_cast<Actor*>(&a));}}
 debug::nearestAngle.store(nearest);debug::nearestDistance.store(nearestRange);last=best.valid?best.player:0;return best;
}
inline Vec3 predictPoint(const Selection& target,const Settings& s,Vec3 origin,bool angleMode){
 if(!target.valid||!s.prediction)return target.point;
 double now=std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
 for(const auto& a:g_actors)if(reinterpret_cast<std::uintptr_t>(a.player)==target.player){auto point=a.motion.lead(target.point,now,s.predictionMs,angleMode?s.smoothing:0,s.prediction);auto delta=sub(point,origin);float n=length(delta);if(!finite(point)||!std::isfinite(n)||n<.2f||n>s.maxDistance)return target.point;if(s.visibleCheck&&!visibilityAccept(const_cast<Actor*>(&a),target.player,origin,point))return target.point;return point;}
 return target.point;
}
#include "features/aim/silent_runtime.hpp"
inline bool transformPose(void* transform,Vec3& origin,Vec3& forward){auto ptr=native(transform,"Transform");if(!ptr||!ed::g_runtime.transformPositionRotation)return false;esp::Vec3 pos{};esp::Quaternion q{};ed::g_runtime.transformPositionRotation(ptr,&pos,&q);origin={pos.x,pos.y,pos.z};float norm=q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w;if(!finite(origin)||!std::isfinite(norm)||std::fabs(norm-1)>.1f)return false;forward=normalized({2*(q.x*q.z+q.w*q.y),2*(q.y*q.z-q.w*q.x),1-2*(q.x*q.x+q.y*q.y)});return length(forward)>.9f;}
inline bool gate(const Configuration& c,const Settings& s,std::atomic<int>& status){if(!s.enabled){status.store(0);return false;}if(c.pauseInMenu&&g_menuOpen.load()){status.store(2);return false;}if(!s.bodies){status.store(5);return false;}return true;}
inline bool attacking(void* part){std::uint8_t raw=0;bool ok=field(part,offsets::field::nrk::kWeaponState,raw);debug::attackRaw.store(ok?int(raw):-1);return ok&&raw==aim_offsets::kAttackState;}
inline bool stackSpan(Ray* rays,int count){static thread_local std::uintptr_t lo=0,hi=0;if(!lo){pthread_attr_t a;void* base=nullptr;std::size_t size=0;if(pthread_getattr_np(pthread_self(),&a))return false;int r=pthread_attr_getstack(&a,&base,&size);pthread_attr_destroy(&a);if(r)return false;lo=reinterpret_cast<std::uintptr_t>(base);hi=lo+size;}auto p=reinterpret_cast<std::uintptr_t>(rays);return count>0&&count<=64&&p>=lo&&p<=hi&&std::size_t(count)*sizeof(Ray)<=hi-p;}
// This header shares the runtime namespace and uses the verified helpers above.
#include "features/aim/automation_runtime.hpp"
inline void cameraTick(void* self,ExecuteTime time,const void* method){debug::cameraCalls.fetch_add(1);
 auto original=g_cameraOriginal.load(std::memory_order_acquire);if(original)original(self,time,method);if(!ed::isUnityMainCallback()){debug::normalReason.store(4);return;}if(!g_functionsValid){debug::normalReason.store(5);return;}auto c=configuration();auto s=c.normal;if(automationOwnsCamera(self,c)){g_recoilTracker.reset();debug::normalReason.store(17);g_normalState.store(10);return;}if(!gate(c,s,g_normalState)){g_recoilTracker.reset();debug::normalReason.store(!s.enabled?1:(c.pauseInMenu&&g_menuOpen.load()?2:3));return;}
 void* p=object(self,0x18);void* world=nullptr;ed::PlayerSnapshot local{};if(!localAlive(p,local,world)||object(self,0x20)!=world||!named(object(self,0x38),"eir")||!object(self,0x48)||!native(object(self,0x50),"Transform")||!native(object(self,0x58),"Transform")){g_recoilTracker.reset();debug::normalReason.store(7);g_normalState.store(3);return;}if(s.onAttack&&!attacking(local.weaponPart)){g_recoilTracker.reset();debug::normalReason.store(8);g_normalState.store(4);return;}
 Vec3 origin{},forward{};if(!transformPose(object(local.weaponPart,offsets::field::nrk::kHitTransform),origin,forward)){g_recoilTracker.reset();debug::normalReason.store(9);g_normalState.store(3);return;}
 auto target=select(s,world,local.team,origin,forward,g_lastNormal);auto current=anglesFor(forward);auto desired=target.valid?anglesFor(sub(predictPoint(target,s,origin,true),origin)):current;bool recoilOnly=false,recoilUsed=false;Angles recoil{};auto next=target.valid?smooth(current,desired,s.smoothing,time.delta):current;
 if(s.rcs&&s.rcsStrength>0&&attacking(local.weaponPart)&&readRecoil(local.weaponPart,recoil)){
  auto weapon=reinterpret_cast<std::uintptr_t>(local.activeWeapon);
  if(target.valid){Angles previous=g_recoilTracker.active&&g_recoilTracker.weapon==weapon?g_recoilTracker.previous:Angles{};next=assists::smoothCompensated(current,desired,previous,recoil,s.rcsStrength,s.smoothing,time.delta);g_recoilTracker.synchronize(weapon,{recoil.pitch*s.rcsStrength*.01f,recoil.yaw*s.rcsStrength*.01f});recoilUsed=std::fabs(recoil.pitch)+std::fabs(recoil.yaw)>.0001f;}
  else{auto delta=g_recoilTracker.delta(weapon,{recoil.pitch*s.rcsStrength*.01f,recoil.yaw*s.rcsStrength*.01f},true);recoilOnly=std::fabs(delta.pitch)+std::fabs(delta.yaw)>.0001f;recoilUsed=recoilOnly;next=assists::compensate(current,delta,100);}
 }else g_recoilTracker.reset();
 if(!target.valid&&!recoilOnly){bool unavailable=s.visibleCheck&&!g_visibilityReady.load();debug::normalReason.store(unavailable?15:10);g_normalState.store(unavailable?8:5);return;}
 next=g_clamp(self,next,nullptr);if(!std::isfinite(next.pitch)||!std::isfinite(next.yaw)){g_recoilTracker.reset();debug::normalReason.store(14);return;}g_apply(self,next,nullptr);g_refresh(self,nullptr);g_normalApplications.fetch_add(1);if(recoilUsed)debug::rcsApplied.fetch_add(1);debug::normalReason.store(recoilOnly?16:11);g_normalState.store(recoilOnly?9:6);
}
inline void emitRays(void* self,void* ctx,Ray* rays,std::uint64_t span){debug::silentCalls.fetch_add(1);
 auto c=configuration();auto s=c.silent;int count=static_cast<std::int32_t>(span);
 debug::silentReason.store(!s.enabled?1:(!ed::isUnityMainCallback()?4:(!g_functionsValid?5:(!rays||!stackSpan(rays,count)?12:(c.pauseInMenu&&g_menuOpen.load()?2:(!s.bodies?3:0))))));
 if(ed::isUnityMainCallback()&&g_functionsValid&&rays&&stackSpan(rays,count)&&gate(c,s,g_silentState)){
  void* p=localPlayer();void* world=nullptr;ed::PlayerSnapshot local{};std::uint64_t owner=0,id=0;
  if(localAlive(p,local,world)&&object(self,0x18)==world&&field(ctx,0,owner)&&field(p,0x10,id)&&owner==id&&object(ctx,0x48)==local.activeWeapon){
   Vec3 origin{},forward{};Ray ray{};if(ed::readValue(rays,ray)&&transformPose(object(ctx,0x10),origin,forward)){auto target=select(s,world,local.team,ray.origin,forward,g_lastSilent);if(target.valid){unsigned changed=0;Vec3 point{};std::int32_t colliderId=0;bool resolved=silentLivePoint(target,s,ray.origin,forward,point,colliderId);if(resolved)for(int i=0;i<count;++i){Ray r{};if(!ed::readValue(rays+i,r)||!silent_accuracy::inFov(r.origin,point,forward,s)||(s.visibleCheck&&!silentHitsBone(r.origin,point,colliderId)))continue;auto direction=normalized(sub(point,r.origin));if(length(direction)<.9f)continue;std::memcpy(&rays[i].direction,&direction,sizeof(direction));++changed;}g_silentApplications.fetch_add(changed);debug::silentReason.store(changed?11:18);g_silentState.store(changed?6:5);}else{debug::silentReason.store(10);g_silentState.store(5);}}else{debug::silentReason.store(9);g_silentState.store(3);}
  }else{debug::silentReason.store(13);g_silentState.store(3);}
 }
 // Forward once; bounded prediction changes only the selected direction. Automation observes local emission.
 if(g_emitOriginal)g_emitOriginal(self,ctx,rays,span);
 autoShotObserved(self,ctx,rays,count);
}
inline bool bytesMatch(std::uintptr_t addr,std::initializer_list<std::uint32_t> words){unsigned i=0;for(auto expected:words){std::uint32_t actual=0;if(!ed::readValue(reinterpret_cast<void*>(addr+4*i++),actual)||actual!=expected){static thread_local Clock::time_point last{};auto now=Clock::now();if(now-last>std::chrono::seconds(5)){last=now;debug::log("signature mismatch rva=0x%llx word=%u got=%08x expected=%08x",(unsigned long long)(addr-ed::g_runtime.unityBase),i-1,actual,expected);}return false;}}return true;}
inline bool validateFunctions(){auto b=ed::g_runtime.unityBase;if(!b)return false;bool ok=bytesMatch(b+aim_offsets::kCameraApply,{0xd10103ff,0xfd000bea})&&bytesMatch(b+aim_offsets::kCameraClamp,{0x6dbb33ed,0x6d012beb})&&bytesMatch(b+aim_offsets::kCameraTick,{0xa9bf4ffe,0xaa0003f3})&&bytesMatch(b+aim_offsets::kCameraRefresh,{0xd100c3ff,0xa900fbff})&&bytesMatch(b+aim_offsets::kShotEmit,{0xd10183ff,0xa9035ffe,0xa90457f6,0xa9054ff4})&&bytesMatch(b+offsets::unity::kColliderGetBounds,{0xd100c3ff,0xa9024ffe});if(!ok)return false;g_bounds=reinterpret_cast<BoundsFn>(b+offsets::unity::kColliderGetBounds);g_clamp=reinterpret_cast<ClampFn>(b+aim_offsets::kCameraClamp);g_apply=reinterpret_cast<ApplyFn>(b+aim_offsets::kCameraApply);g_refresh=reinterpret_cast<RefreshFn>(b+aim_offsets::kCameraRefresh);g_emitOriginal=reinterpret_cast<EmitFn>(b+aim_offsets::kShotEmit);return true;}
inline bool installCamera(){
#if defined(__aarch64__)
 bool trace=debug::due(debug::cameraAttempts);if(g_normalHookReady.load())return true;
 if(!g_functionsValid){debug::normalInstallReason.store(13);return false;}
 void* klass=ed::classFromSlot(offsets::typeinfo::kCameraComponent,"ios");
 if(!klass){debug::normalInstallReason.store(2);return false;}
 auto base=ed::g_runtime.unityBase,target=base+aim_offsets::kCameraTick;debug::normalInstallReason.store(3);
 for(unsigned i=0;i<128;++i){
  auto slot=reinterpret_cast<std::uintptr_t*>(reinterpret_cast<std::uintptr_t>(klass)+ed::kRuntimeVtableOffset+i*ed::kVirtualInvokeDataSize);
  std::uintptr_t fn=0,metadata=0;if(!ed::readValue(slot,fn)||!ed::readValue(slot+1,metadata))continue;
  if(!hook_support::cameraSlotMatches(fn,target,metadata,ed::readable(reinterpret_cast<void*>(metadata),16),base+offsets::elf::kTextBegin,base+offsets::elf::kTextEnd))continue;
  ed::refreshMaps(true);if(!ed::writable(slot,8)){debug::normalInstallReason.store(4);if(trace)debug::log("camera slot not writable index=%u",i);return false;}
  std::uint64_t probe=fn;iovec src{&probe,8},dst{slot,8};
  if(syscall(__NR_process_vm_writev,getpid(),&src,1,&dst,1,0)!=8){debug::normalInstallReason.store(4);if(trace)debug::log("camera slot write-probe denied errno=%d",errno);return false;}
  g_cameraOriginal.store(reinterpret_cast<CameraTickFn>(fn),std::memory_order_release);
  auto replacement=reinterpret_cast<std::uintptr_t>(&cameraTick);__atomic_store_n(slot,replacement,__ATOMIC_RELEASE);
  std::uintptr_t current=0;if(!ed::readValue(slot,current)||current!=replacement){__atomic_store_n(slot,fn,__ATOMIC_RELEASE);g_cameraOriginal.store(nullptr,std::memory_order_release);debug::normalInstallReason.store(12);return false;}
  debug::normalInstallReason.store(1);g_normalHookReady.store(true);debug::log("camera hook ready index=%u match=exact-validated-native-pointer metadata=%p",i,reinterpret_cast<void*>(metadata));return true;
 }
#else
 debug::normalInstallReason.store(11);
#endif
 return false;
}
inline bool installSilent(){
#if defined(__aarch64__)
 bool trace=debug::due(debug::silentAttempts);if(g_silentHookReady.load())return true;
 if(!g_functionsValid){debug::silentInstallReason.store(13);return false;}
 auto site=ed::g_runtime.unityBase+aim_offsets::kShotCallsite;std::uint32_t word=0;
 if(!ed::readValue(reinterpret_cast<void*>(site),word)||word!=0x97fffd71){debug::silentInstallReason.store(5);return false;}
 long rawPage=sysconf(_SC_PAGESIZE);if(rawPage<=0||!hook_support::pageSizeValid(std::uintptr_t(rawPage))){debug::silentInstallReason.store(6);return false;}
 auto pageSize=std::uintptr_t(rawPage);hook_support::AllocationStats stats{};void* relay=hook_support::allocateNear(site,pageSize,stats);
 if(trace)debug::log("silent relay search candidates=%u attempts=%u relocated=%u errno=%d pageSize=%lu selected=%p",stats.candidates,stats.attempts,stats.relocated,stats.lastError,(unsigned long)pageSize,relay==MAP_FAILED?nullptr:relay);
 if(relay==MAP_FAILED){debug::silentInstallReason.store(7);return false;}
 std::uint32_t branch=0;if(!hook_support::branchWord(site,reinterpret_cast<std::uintptr_t>(relay),branch)){munmap(relay,pageSize);debug::silentInstallReason.store(7);return false;}
 std::uint32_t instructions[2]={0x58000051,0xd61f0220};auto replacement=reinterpret_cast<std::uintptr_t>(&emitRays);
 std::memcpy(relay,instructions,8);std::memcpy(static_cast<char*>(relay)+8,&replacement,8);__builtin___clear_cache(static_cast<char*>(relay),static_cast<char*>(relay)+16);
 if(mprotect(relay,pageSize,PROT_READ|PROT_EXEC)){debug::silentInstallReason.store(8);if(trace)debug::log("silent relay RX denied errno=%d",errno);munmap(relay,pageSize);return false;}
 auto page=site&~(pageSize-1);
 if(mprotect(reinterpret_cast<void*>(page),pageSize,PROT_READ|PROT_WRITE|PROT_EXEC)){debug::silentInstallReason.store(9);if(trace)debug::log("silent text RWX denied errno=%d",errno);munmap(relay,pageSize);return false;}
 std::uint32_t current=0;if(!ed::readValue(reinterpret_cast<void*>(site),current)||current!=word){mprotect(reinterpret_cast<void*>(page),pageSize,PROT_READ|PROT_EXEC);munmap(relay,pageSize);debug::silentInstallReason.store(5);return false;}
 __atomic_store_n(reinterpret_cast<std::uint32_t*>(site),branch,__ATOMIC_RELEASE);__builtin___clear_cache(reinterpret_cast<char*>(site),reinterpret_cast<char*>(site+4));
 if(mprotect(reinterpret_cast<void*>(page),pageSize,PROT_READ|PROT_EXEC)){debug::silentInstallReason.store(10);if(trace)debug::log("silent restore RX denied errno=%d",errno);__atomic_store_n(reinterpret_cast<std::uint32_t*>(site),word,__ATOMIC_RELEASE);__builtin___clear_cache(reinterpret_cast<char*>(site),reinterpret_cast<char*>(site+4));mprotect(reinterpret_cast<void*>(page),pageSize,PROT_READ|PROT_EXEC);return false;}
 g_relay=relay;debug::silentInstallReason.store(1);g_silentHookReady.store(true);debug::log("silent shot call-site ready relay=%p branch=%08x",relay,branch);return true;
#else
 debug::silentInstallReason.store(11);
#endif
 return false;
}
inline input_init::Retry g_inputRetry{};
inline void onPlayer(void* p){
 debug::playerCalls.fetch_add(1);if(!g_active.load(std::memory_order_acquire)){debug::inactive.fetch_add(1);return;}if(!ed::g_runtime.ready){debug::runtimeNotReady.fetch_add(1);return;}if(!ed::isUnityMainCallback()){debug::wrongThread.fetch_add(1);return;}
 debug::mainCalls.fetch_add(1);debug::mainBase.store(ed::g_runtime.unityBase);debug::callbackTid.store(int(syscall(SYS_gettid)));debug::lastPlayer.store(reinterpret_cast<std::uintptr_t>(p));
 if(!g_functionsValid){debug::validationAttempts.fetch_add(1);g_functionsValid=validateFunctions();if(!g_functionsValid){debug::validationFailures.fetch_add(1);g_normalState.store(7);g_silentState.store(7);return;}debug::log("function signatures validated on UnityMain");}
 auto now=Clock::now();auto c=configuration();if(now-g_lastInstall>std::chrono::seconds(1)){g_lastInstall=now;if((c.normal.enabled||c.autoFire.enabled)&&!installCamera())g_normalState.store(1);if((c.silent.enabled||c.autoFire.enabled)&&!installSilent())g_silentState.store(1);}auto* local=localPlayer();if(g_inputRetry.due(autoNow(),c.autoFire.enabled,g_inputHookReady.load(),reinterpret_cast<std::uintptr_t>(local)))installInput(local);else if(c.autoFire.enabled&&!g_inputHookReady.load()&&!local)g_inputInstallReason.store(input_init::NoLocal);bool visibility=c.normal.visibleCheck||c.silent.enabled||c.autoFire.enabled;prepareAssists(c.normal.rcs,visibility);capture(p,visibility&&g_visibilityReady.load());
}
}
