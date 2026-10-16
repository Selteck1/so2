// Included after predictPoint, inside lemming::aim::runtime.
inline bool liveSilentVolume(const Selection& selected,HitVolume& volume,std::int32_t& colliderId){
 if(!selected.valid||!g_bounds||!ed::isUnityMainCallback())return false;
 auto* p=reinterpret_cast<void*>(selected.player);if(ed::objectClass(p)!=ed::g_runtime.playerClass)return false;
 auto* arr=object(part(p,"jgi"),offsets::field::jgi::kHitBoxColliders);std::uintptr_t count=0;
 if(!field(arr,0x18,count)||!count||count>64)return false;
 for(std::uintptr_t i=0;i<count;++i){auto* hb=object(arr,0x20+i*8);int bone=-1;if(!named(hb,"HitBoxCollider")||!field(hb,0x20,bone)||bone!=selected.bone)continue;auto* c=object(hb,0x28);if(!named(c,"BoxCollider")&&!named(c,"CapsuleCollider")&&!named(c,"SphereCollider"))continue;auto ptr=native(c);std::uintptr_t vt=0,fn=0;if(!ptr||!ed::readValue(reinterpret_cast<void*>(ptr),vt)||!ed::readValue(reinterpret_cast<void*>(vt+0xf0),fn)||!unityCode(fn))continue;
  Bounds b{};g_bounds(ptr,&b);volume={b.center,b.extents,bone};if(!validVolume(volume))continue;
  esp::Vec3 root{};if(!ed::readPlayerPoseSnapshot(p,root)||length(sub(volume.center,{root.x,root.y,root.z}))>4)continue;
  colliderId=g_visibilityReady.load()&&g_entityId?g_entityId(c,nullptr):0;debug::silentLiveRefresh.fetch_add(1);return true;
 }
 return false;
}
inline bool silentHitsBone(Vec3 origin,Vec3 point,std::int32_t colliderId){
 if(!colliderId||!g_visibilityReady.load()||!g_raycast||!g_defaultScene||!ed::isUnityMainCallback())return false;
 auto d=sub(point,origin);float distance=length(d);if(!finite(origin)||!finite(point)||!std::isfinite(distance)||distance<.2f||distance>501)return false;
 Ray ray{origin,normalized(d)};assists::RaycastHit hit{};auto scene=g_defaultScene(nullptr);
 return g_raycast(&scene,&ray,&hit,distance+.05f,-5,2)&&hit.collider==colliderId&&std::isfinite(hit.distance)&&hit.distance>=.2f&&hit.distance<=distance+.06f;
}
inline bool silentLivePoint(const Selection& selected,const Settings& s,Vec3 origin,Vec3 forward,Vec3& point,std::int32_t& colliderId){
 HitVolume live{};if(!liveSilentVolume(selected,live,colliderId)){debug::silentPointRejected.fetch_add(1);return false;}
 Vec3 proposed=live.center;
 if(s.multipoints&&bodyForBone(selected.bone)!=int(Body::Head)&&validVolume(selected.volume)){auto offset=sub(selected.point,selected.volume.center);proposed={live.center.x+offset.x,live.center.y+offset.y,live.center.z+offset.z};}
 double now=std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
 if(s.prediction)for(const auto& a:g_actors)if(reinterpret_cast<std::uintptr_t>(a.player)==selected.player&&a.world==object(reinterpret_cast<void*>(selected.player),offsets::field::mfc::kWorld)&&now>=a.motion.sampled&&now-a.motion.sampled<=.1){
  // Fresh geometry: do not apply the OLD snapshot age again.
  proposed=a.motion.lead(proposed,a.motion.sampled,s.predictionMs,0,true);break;
 }
 auto candidate=silent_accuracy::innerPoint(live,proposed);
 if(length(sub(candidate,live.center))>1e-5f&&!silentHitsBone(origin,candidate,colliderId)){candidate=live.center;debug::silentCenterFallback.fetch_add(1);}
 if(!silent_accuracy::inFov(origin,candidate,forward,s)){debug::silentPointRejected.fetch_add(1);return false;}
 if(s.visibleCheck&&!silentHitsBone(origin,candidate,colliderId)){
  if(candidate.x!=live.center.x||candidate.y!=live.center.y||candidate.z!=live.center.z){candidate=live.center;debug::silentCenterFallback.fetch_add(1);}
  if(!silent_accuracy::inFov(origin,candidate,forward,s)||!silentHitsBone(origin,candidate,colliderId)){debug::silentPointRejected.fetch_add(1);return false;}
 }
 point=candidate;debug::silentBone.store(selected.bone);debug::silentLeadCm.store(length(sub(point,live.center))*100);return true;
}
