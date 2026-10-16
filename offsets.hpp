#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <mutex>
#include <atomic>
#include "features/aim/automation.hpp"
#include "features/aim/input_init.hpp"
namespace lemming::aim {
struct Vec3{float x{},y{},z{};};struct Angles{float pitch{},yaw{};};
enum class Body:unsigned{Head,Neck,Chest,Stomach,Pelvis,Arms,Legs,Feet,Count};
inline constexpr const char* kBodyNames[]={"Head","Neck","Chest","Stomach","Pelvis","Arms","Legs","Feet"};
inline constexpr std::uint32_t bodyBit(Body b){return 1u<<unsigned(b);}
inline int bodyForBone(int b){switch(b){case 10:return 0;case 9:return 1;case 8:case 54:return 2;case 7:return 3;case 0:return 4;case 11:case 12:case 13:case 14:case 15:case 16:case 17:case 18:return 5;case 1:case 2:case 3:case 4:return 6;case 5:case 6:case 19:case 20:return 7;default:return -1;}}
struct Settings{bool enabled=false,multipoints=false,onAttack=true,lockTarget=false,showFov=false,rcs=false,visibleCheck=false,prediction=true;std::uint32_t bodies=bodyBit(Body::Head)|bodyBit(Body::Chest);int priority=0;float fov=8,maxDistance=150,smoothing=.12f,pointScale=.5f,rcsStrength=100,predictionMs=20;};
struct Configuration{Settings normal{},silent{};automation::Options autoFire{};bool pauseInMenu=true;Configuration(){normal.onAttack=false;silent.onAttack=false;}};
inline std::mutex g_configMutex;inline Configuration g_config;
inline std::atomic<bool> g_active{false},g_menuOpen{true},g_normalHookReady{false},g_silentHookReady{false};
inline std::atomic<bool> g_rcsReady{false},g_visibilityReady{false};
inline std::atomic<bool> g_inputHookReady{false},g_autoScopeReady{false};
inline std::atomic<int> g_autoStage{0},g_inputInstallReason{input_init::NotAttempted};
inline std::atomic<int> g_normalState{0},g_silentState{0};inline std::atomic<std::uint64_t> g_normalApplications{0},g_silentApplications{0};
inline float finiteClamp(float x,float fallback,float lo,float hi){return std::isfinite(x)?std::clamp(x,lo,hi):fallback;}
inline Settings sanitize(Settings s){s.bodies&=255;s.priority=std::clamp(s.priority,0,2);s.fov=finiteClamp(s.fov,8,.1f,180);s.maxDistance=finiteClamp(s.maxDistance,150,1,500);s.smoothing=finiteClamp(s.smoothing,.12f,0,1);s.pointScale=finiteClamp(s.pointScale,.5f,0,.9f);s.rcsStrength=finiteClamp(s.rcsStrength,100,0,100);s.predictionMs=finiteClamp(s.predictionMs,20,0,100);return s;}
inline void publish(Configuration c){c.normal=sanitize(c.normal);c.silent=sanitize(c.silent);c.autoFire=automation::sanitize(c.autoFire);{std::lock_guard<std::mutex> l(g_configMutex);g_config=c;}g_active.store(c.normal.enabled||c.silent.enabled||c.autoFire.enabled,std::memory_order_release);}
inline Configuration configuration(){std::lock_guard<std::mutex> l(g_configMutex);return g_config;}
inline bool finite(Vec3 v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z)&&std::fabs(v.x)<100000&&std::fabs(v.y)<100000&&std::fabs(v.z)<100000;}
inline Vec3 sub(Vec3 a,Vec3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}inline float dot(Vec3 a,Vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}inline float length(Vec3 v){return std::sqrt(dot(v,v));}
inline Vec3 normalized(Vec3 v){float n=length(v);return n>1e-5f&&std::isfinite(n)?Vec3{v.x/n,v.y/n,v.z/n}:Vec3{};}
inline float wrap(float a){return std::isfinite(a)?std::remainder(a,360.0f):0;}
inline Angles anglesFor(Vec3 d){auto v=normalized(d);return {-std::atan2(v.y,std::hypot(v.x,v.z))*57.295779513f,std::atan2(v.x,v.z)*57.295779513f};}
inline Vec3 forwardFor(Angles a){float p=a.pitch*.01745329252f,y=a.yaw*.01745329252f;return {std::cos(p)*std::sin(y),-std::sin(p),std::cos(p)*std::cos(y)};}
inline Angles smooth(Angles current,Angles desired,float seconds,float dt){float t=seconds<=.0001f?1:1-std::exp(-std::clamp(dt,0.0f,.1f)/seconds);return {std::clamp(current.pitch+wrap(desired.pitch-current.pitch)*t,-89.0f,89.0f),wrap(current.yaw+wrap(desired.yaw-current.yaw)*t)};}
struct HitVolume{Vec3 center{},extents{};int bone=-1;};
inline bool validVolume(const HitVolume& v){return finite(v.center)&&finite(v.extents)&&v.extents.x>0&&v.extents.y>0&&v.extents.z>0&&v.extents.x<2&&v.extents.y<2&&v.extents.z<2&&bodyForBone(v.bone)>=0;}
inline unsigned samples(const HitVolume& v,const Settings& s,std::array<Vec3,7>& p){if(!validVolume(v))return 0;p[0]=v.center;if(!s.multipoints||s.pointScale<=0)return 1;float r=std::min({v.extents.x,v.extents.y,v.extents.z})*std::clamp(s.pointScale,0.0f,.9f);p[1]={v.center.x+r,v.center.y,v.center.z};p[2]={v.center.x-r,v.center.y,v.center.z};p[3]={v.center.x,v.center.y+r,v.center.z};p[4]={v.center.x,v.center.y-r,v.center.z};p[5]={v.center.x,v.center.y,v.center.z+r};p[6]={v.center.x,v.center.y,v.center.z-r};return 7;}
struct Selection{bool valid=false;std::uintptr_t player=0;Vec3 point{};float angle=0,distance=0,score=std::numeric_limits<float>::infinity();int bone=-1;HitVolume volume{};};
inline void consider(Selection& best,const Settings& s,std::uintptr_t player,float hp,Vec3 origin,Vec3 forward,const HitVolume& v,std::uintptr_t locked=0,bool(*visible)(void*,std::uintptr_t,Vec3,Vec3)=nullptr,void* context=nullptr){int body=bodyForBone(v.bone);if(body<0||!(s.bodies&(1u<<body)))return;std::array<Vec3,7> p{};unsigned n=samples(v,s,p);forward=normalized(forward);if(!finite(origin)||length(forward)<.9f)return;for(unsigned i=0;i<n;++i){auto d=sub(p[i],origin);float distance=length(d);if(!std::isfinite(distance)||distance<.2f||distance>s.maxDistance)continue;float angle=std::acos(std::clamp(dot(normalized(d),forward),-1.0f,1.0f))*57.295779513f;if(!std::isfinite(angle)||angle>s.fov)continue;float score=s.priority==1?distance:(s.priority==2?hp:angle);if(s.lockTarget&&locked==player)score-=10000;if(!best.valid||score<best.score-1e-5f||(std::fabs(score-best.score)<1e-5f&&angle<best.angle)){if(s.visibleCheck&&(!visible||!visible(context,player,origin,p[i])))continue;best={true,player,p[i],angle,distance,score,v.bone,v};}}}
inline const char* stateText(int s){switch(s){case 1:return "Waiting for live hooks";case 2:return "Paused while menu is open";case 3:return "Waiting for local player";case 4:return "Waiting for attack";case 5:return "No eligible hitbox in FOV";case 6:return "Tracking target (device test needed)";case 7:return "Profile / hook validation failed";case 8:return "Visibility profile is not ready";case 9:return "RCS only (no eligible target)";case 10:return "Camera owned by active automation cycle";default:return "Disabled";}}
}
