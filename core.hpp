// Included inside lemming::aim::runtime, after field/native/Actor helpers.
inline bool bytesMatch(std::uintptr_t,std::initializer_list<std::uint32_t>);
using RaycastFn=bool(*)(const std::uint64_t*,const Ray*,assists::RaycastHit*,float,int,int);
using DefaultSceneFn=std::uint64_t(*)(const void*);
using EntityIdFn=std::int32_t(*)(void*,const void*);
inline RaycastFn g_raycast=nullptr;inline DefaultSceneFn g_defaultScene=nullptr;inline EntityIdFn g_entityId=nullptr;
inline assists::RecoilTracker g_recoilTracker{};inline unsigned g_visibilityBudget=0;
inline Clock::time_point g_lastAssistBind{};
inline void prepareAssists(bool rcs,bool visibility){
 if(!rcs&&!visibility)return;auto now=Clock::now();if(now-g_lastAssistBind<std::chrono::seconds(1))return;g_lastAssistBind=now;auto b=ed::g_runtime.unityBase;
 if(rcs&&!g_rcsReady.load()){
  bool ok=bytesMatch(b+aim_offsets::kRecoilScale,{0xd10103ff,0x6d0123e9,0xa90257fe,0xa9034ff4})&&bytesMatch(b+aim_offsets::kRecoilPointLoad,{0x2d4f26a8})&&bytesMatch(b+aim_offsets::kRecoilMultiplierLoad,{0xbd404261});
  g_rcsReady.store(ok);if(ok)debug::log("RCS profile validated: ActualPoint/Multiplier and native ray-builder axes");
 }
 if(visibility&&!g_visibilityReady.load()){
  bool ok=bytesMatch(b+aim_offsets::kNativeRaycast,{0xf81f0ffe,0xf9400000,0x9407c92c,0x12000000})&&bytesMatch(b+aim_offsets::kDefaultPhysicsScene,{0xaa1f03e0,0xd65f03c0})&&bytesMatch(b+aim_offsets::kObjectEntityId,{0xf81e0ffe,0xa9014ff4});
  if(ok){g_raycast=reinterpret_cast<RaycastFn>(b+aim_offsets::kNativeRaycast);g_defaultScene=reinterpret_cast<DefaultSceneFn>(b+aim_offsets::kDefaultPhysicsScene);g_entityId=reinterpret_cast<EntityIdFn>(b+aim_offsets::kObjectEntityId);g_visibilityReady.store(true);debug::log("visibility profile validated: 8-byte PhysicsScene, 44-byte RaycastHit, first collider ownership");}
 }
}
inline bool readRecoil(void* weaponPart,Angles& out){
 debug::rcsReads.fetch_add(1);assists::Point2 point{};float mult=0;
 bool ok=g_rcsReady.load()&&named(weaponPart,"nrk")&&field(weaponPart,aim_offsets::kRecoilActualPoint,point)&&field(weaponPart,aim_offsets::kRecoilMultiplier,mult)&&assists::recoilAngles(point,mult,out);
 if(!ok){debug::rcsRejected.fetch_add(1);return false;}debug::recoilPitch.store(out.pitch);debug::recoilYaw.store(out.yaw);return true;
}
inline bool visibilityAccept(void* context,std::uintptr_t player,Vec3 origin,Vec3 point){
 auto* actor=static_cast<const Actor*>(context);
 if(!actor||reinterpret_cast<std::uintptr_t>(actor->player)!=player||!g_visibilityReady.load()||!g_raycast||!g_defaultScene||!ed::isUnityMainCallback()||!g_visibilityBudget){debug::visibilityUnknown.fetch_add(1);return false;}
 bool owned=false;for(unsigned i=0;i<actor->colliderCount&&i<actor->colliderIds.size();++i)owned|=actor->colliderIds[i]!=0;if(!owned){debug::visibilityUnknown.fetch_add(1);return false;}
 --g_visibilityBudget;auto delta=sub(point,origin);float distance=length(delta);
 if(!finite(origin)||!finite(point)||!std::isfinite(distance)||distance<.2f||distance>501){debug::visibilityUnknown.fetch_add(1);return false;}
 Ray ray{origin,normalized(delta)};assists::RaycastHit hit{};auto scene=g_defaultScene(nullptr);debug::visibilityRays.fetch_add(1);
 // All standard raycast layers except IgnoreRaycast; include hitbox triggers.
 bool collision=g_raycast(&scene,&ray,&hit,distance+.05f,-5,2);
 if(!collision){debug::visibilityClear.fetch_add(1);return true;}
 if(!hit.collider||!std::isfinite(hit.distance)||hit.distance<0||hit.distance>distance+.06f){debug::visibilityUnknown.fetch_add(1);return false;}
 for(unsigned i=0;i<actor->colliderCount&&i<actor->colliderIds.size();++i)if(actor->colliderIds[i]&&actor->colliderIds[i]==hit.collider){debug::visibilityTarget.fetch_add(1);return true;}
 debug::visibilityBlocked.fetch_add(1);return false;
}
