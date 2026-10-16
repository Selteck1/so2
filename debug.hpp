#pragma once
#include "features/aim/core.hpp"
namespace lemming::aim::prediction {
struct Motion {
 Vec3 position{},velocity{};double sampled=0;std::uintptr_t world=0;int team=0;unsigned samples=0;
 void reset(){*this={};}
 bool update(Vec3 p,double now,std::uintptr_t w,int t){
  if(!finite(p)||!std::isfinite(now)||!w){reset();return false;}
  double dt=now-sampled;
  if(samples&&world==w&&team==t&&dt==0)return samples>=2;
  if(!samples||world!=w||team!=t||dt<=0||dt>.25){position=p;velocity={};sampled=now;world=w;team=t;samples=1;return false;}
  if(dt<.005)return samples>=2;
  auto delta=sub(p,position);float distance=length(delta);auto measured=Vec3{delta.x/float(dt),delta.y/float(dt),delta.z/float(dt)};
  if(distance>3||!finite(measured)||length(measured)>30){position=p;velocity={};sampled=now;samples=1;return false;}
  float alpha=samples<2?1.f:1-std::exp(-float(dt)/.045f);
  if(dot(measured,velocity)<0||length(measured)<.05f)alpha=1;
  velocity={velocity.x+(measured.x-velocity.x)*alpha,velocity.y+(measured.y-velocity.y)*alpha,velocity.z+(measured.z-velocity.z)*alpha};
  position=p;sampled=now;samples=std::min(samples+1,1000u);return samples>=2;
 }
 Vec3 lead(Vec3 point,double now,float extraMs,float smoothing,bool enabled=true)const{
  if(!enabled||samples<2||!finite(point)||!finite(velocity)||!std::isfinite(now)||now<sampled||now-sampled>.1||!std::isfinite(extraMs)||!std::isfinite(smoothing))return point;
  float horizon=std::clamp(float(now-sampled)+std::clamp(extraMs,0.f,100.f)*.001f+std::clamp(smoothing,0.f,.15f),0.f,.25f);
  auto offset=Vec3{velocity.x*horizon,velocity.y*horizon,velocity.z*horizon};float n=length(offset);if(!std::isfinite(n))return point;if(n>1)offset={offset.x/n,offset.y/n,offset.z/n};
  Vec3 next{point.x+offset.x,point.y+offset.y,point.z+offset.z};return finite(next)?next:point;
 }
};
inline bool cameraLease(bool enabled,bool back,bool ready,bool paused,bool activeOrRestored,double now,double until,std::uintptr_t owner,std::uintptr_t world,std::uintptr_t currentOwner,std::uintptr_t currentWorld){return enabled&&back&&ready&&!paused&&activeOrRestored&&std::isfinite(now)&&std::isfinite(until)&&until>now&&owner&&world&&owner==currentOwner&&world==currentWorld;}
}
