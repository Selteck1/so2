#pragma once
#include "features/aim/core.hpp"
#include <cstddef>
namespace lemming::aim::assists {
struct Point2 { float x{}, y{}; };
struct FovGeometry { bool valid=false,coversScreen=false;float centerX{},centerY{},radiusX{},radiusY{}; };
inline FovGeometry fovGeometry(float degrees,float width,float height,float p00,float p11,float shiftX=0,float shiftY=0){
 FovGeometry g{};if(!std::isfinite(degrees)||degrees<=0||degrees>180||!std::isfinite(width)||!std::isfinite(height)||width<1||height<1||width>16384||height>16384||!std::isfinite(p00)||!std::isfinite(p11)||p00<=.01f||p11<=.01f||p00>100||p11>100||!std::isfinite(shiftX)||!std::isfinite(shiftY)||std::fabs(shiftX)>1||std::fabs(shiftY)>1)return g;
 g.valid=true;g.centerX=(1-shiftX)*width*.5f;g.centerY=(1+shiftY)*height*.5f;
 if(degrees>=90){g.coversScreen=true;return g;}
 float t=std::tan(degrees*.01745329252f);g.radiusX=width*.5f*p00*t;g.radiusY=height*.5f*p11*t;
 if(!std::isfinite(g.radiusX)||!std::isfinite(g.radiusY)||g.radiusX<=0||g.radiusY<=0)return {};
 float x=std::max(g.centerX,width-g.centerX)/g.radiusX,y=std::max(g.centerY,height-g.centerY)/g.radiusY;g.coversScreen=x*x+y*y<=1;return g;
}
// The ray builder rotates by +point.y about Transform.up, then -point.x
// about Transform.right. ActualPoint is scaled by RecoilContext.RecoilMult.
inline bool recoilAngles(Point2 point,float multiplier,Angles& out){
 if(!std::isfinite(point.x)||!std::isfinite(point.y)||!std::isfinite(multiplier)||multiplier<0||multiplier>10)return false;
 out={-point.x*multiplier,point.y*multiplier};return std::isfinite(out.pitch)&&std::isfinite(out.yaw)&&std::fabs(out.pitch)<=30&&std::fabs(out.yaw)<=30;
}
inline Angles compensate(Angles desired,Angles recoil,float percent){float s=finiteClamp(percent,100,0,100)*.01f;return {std::clamp(desired.pitch-recoil.pitch*s,-89.f,89.f),wrap(desired.yaw-recoil.yaw*s)};}
inline Angles smoothCompensated(Angles current,Angles target,Angles previousWeighted,Angles recoil,float percent,float seconds,float dt){
 auto unbiased=compensate(current,{-previousWeighted.pitch,-previousWeighted.yaw},100);return compensate(smooth(unbiased,target,seconds,dt),recoil,percent);
}
struct RecoilTracker {
 std::uintptr_t weapon=0;Angles previous{};bool active=false;
 void reset(){weapon=0;previous={};active=false;}
 Angles delta(std::uintptr_t nextWeapon,Angles recoil,bool firing){
  if(!nextWeapon||!firing){reset();return {};}
  if(!active||weapon!=nextWeapon){previous={};weapon=nextWeapon;active=true;}
  Angles d{wrap(recoil.pitch-previous.pitch),wrap(recoil.yaw-previous.yaw)};previous=recoil;return d;
 }
 void synchronize(std::uintptr_t nextWeapon,Angles recoil){weapon=nextWeapon;previous=recoil;active=true;}
};
struct RaycastHit { Vec3 point{},normal{};std::uint32_t face{};float distance{};Point2 uv{};std::int32_t collider{}; };
static_assert(sizeof(RaycastHit)==44&&offsetof(RaycastHit,distance)==0x1c&&offsetof(RaycastHit,collider)==0x28,"1.0.0 native RaycastHit ABI");
}
