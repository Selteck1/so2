#pragma once
#include "features/aim/core.hpp"
namespace lemming::aim::silent_accuracy {
// Keep an optional live hitscan lead inside the CURRENT selected hitbox.
inline Vec3 innerPoint(const HitVolume& v,Vec3 proposed){
 if(!validVolume(v)||!finite(proposed))return v.center;
 float radius=std::min({v.extents.x,v.extents.y,v.extents.z})*.35f;
 auto offset=sub(proposed,v.center);float n=length(offset);
 if(!std::isfinite(n))return v.center;
 if(n>radius&&n>1e-6f)offset={offset.x*radius/n,offset.y*radius/n,offset.z*radius/n};
 return {v.center.x+offset.x,v.center.y+offset.y,v.center.z+offset.z};
}
inline bool inFov(Vec3 origin,Vec3 point,Vec3 forward,const Settings& s){
 auto d=sub(point,origin);float n=length(d);forward=normalized(forward);
 if(!finite(origin)||!finite(point)||!std::isfinite(n)||n<.2f||n>s.maxDistance||length(forward)<.9f)return false;
 float angle=std::acos(std::clamp(dot(normalized(d),forward),-1.f,1.f))*57.295779513f;
 return std::isfinite(angle)&&angle<=s.fov;
}
}
