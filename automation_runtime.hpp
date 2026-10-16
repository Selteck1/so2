#pragma once
#include <cstdint>
#include <algorithm>
#include <cmath>
namespace lemming::aim::automation {
struct Options {bool enabled=false,returnCamera=false,autoScope=false;float delayMs=0,intervalMs=120,timeoutMs=800;};
inline Options sanitize(Options o){auto b=[](float n,float f,float lo,float hi){return std::isfinite(n)?std::clamp(n,lo,hi):f;};o.delayMs=b(o.delayMs,0,0,300);o.intervalMs=b(o.intervalMs,120,30,1000);o.timeoutMs=b(o.timeoutMs,800,200,2000);return o;}
enum class Stage {Idle,Aligning,Scoping,WaitingShot,Returning,Cooldown};
struct Observation {bool safe=false,target=false,aligned=false,scopeSupported=false,scoped=false,manual=false,shot=false;std::uintptr_t player=0,world=0,weapon=0,targetId=0;};
struct Actions {bool save=false,aim=false,scopePulse=false,attack=false,restore=false,completed=false,timedOut=false;};
struct Cycle {
 Stage stage=Stage::Idle;std::uintptr_t player=0,world=0,weapon=0,target=0;double began=0,alignedAt=0,next=0;bool originalScoped=false,scopeOpened=false,scopePulseSent=false,returnRequested=false,scopeRequested=false;
 bool active()const{return stage!=Stage::Idle&&stage!=Stage::Cooldown;}
 void reset(){*this={};}
 Actions cancel(double now,const Options& c){Actions a{};a.restore=active()&&returnRequested;stage=Stage::Cooldown;next=(std::isfinite(now)?now:began)+sanitize(c).intervalMs;return a;}
 Actions step(double now,Options c,const Observation& o){c=sanitize(c);Actions a{};
  if(!std::isfinite(now)||now<began){a=cancel(now,c);a.timedOut=true;return a;}
  if(stage==Stage::Cooldown){if(now<next)return a;stage=Stage::Idle;}
  if(!c.enabled||!o.safe||o.manual){if(active())return cancel(now,c);return a;}
  if(stage==Stage::Idle){if(!o.target||!o.player||!o.world||!o.weapon||!o.targetId)return a;player=o.player;world=o.world;weapon=o.weapon;target=o.targetId;began=now;alignedAt=0;originalScoped=o.scoped;scopeOpened=false;scopePulseSent=false;returnRequested=c.returnCamera;scopeRequested=c.autoScope;stage=Stage::Aligning;a.save=c.returnCamera;}
  if(o.player!=player||o.world!=world||o.weapon!=weapon||c.returnCamera!=returnRequested||c.autoScope!=scopeRequested)return cancel(now,c);
  bool confirmed=o.shot&&stage==Stage::WaitingShot;
  if(!confirmed&&(!o.target||o.targetId!=target))return cancel(now,c);
  if(!confirmed&&now-began>c.timeoutMs){a=cancel(now,c);a.timedOut=true;return a;}
  if(stage==Stage::Aligning){a.aim=returnRequested;if(!o.aligned)return a;alignedAt=now;stage=c.autoScope&&o.scopeSupported&&!o.scoped?Stage::Scoping:Stage::WaitingShot;}
  if(stage==Stage::Scoping){a.aim=returnRequested;if(!scopePulseSent){a.scopePulse=true;scopePulseSent=true;scopeOpened=true;}if(!o.scoped)return a;stage=Stage::WaitingShot;alignedAt=now;}
  if(stage==Stage::WaitingShot){a.aim=returnRequested;if(o.shot)stage=Stage::Returning;else if(o.aligned&&now-alignedAt>=c.delayMs){a.attack=true;return a;}else return a;}
  if(stage==Stage::Returning){a.aim=false;a.restore=returnRequested;a.scopePulse=scopeOpened&&!originalScoped&&o.scoped;a.completed=true;stage=Stage::Cooldown;next=now+c.intervalMs;}
  return a;
 }
};
inline const char* stageText(Stage s){switch(s){case Stage::Aligning:return "Aligning";case Stage::Scoping:return "Waiting for scope";case Stage::WaitingShot:return "Waiting for shot";case Stage::Returning:return "Returning camera";case Stage::Cooldown:return "Cooldown";default:return "Idle";}}
}
