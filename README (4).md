// Included in lemming::aim::runtime, after transformPose/stackSpan.
using CommandBoolFn=void(*)(void*,bool,const void*);
struct CommandLook {std::uint8_t has=1,padding[3]{};float pitch=0,yaw=0;};
static_assert(sizeof(CommandLook)==12,"verified nullable Vector2 ARM64 ABI");
using CommandLookFn=void(*)(void*,CommandLook,const void*);
inline CommandBoolFn g_commandAttack=nullptr,g_commandScope=nullptr,g_commandScopeHold=nullptr;
inline CommandLookFn g_commandLook=nullptr;
inline automation::Cycle g_autoCycle{};inline std::uint64_t g_autoShotSerial=0,g_autoStartSerial=0;
inline void* g_ownedCommand=nullptr;inline bool g_ownedAttack=false,g_ownedScope=false,g_ownedScopeHold=false;
inline std::uint8_t g_previousAttack=0,g_previousScope=0,g_previousScopeHold=0;
struct SavedAutoView {bool valid=false;void* player=nullptr;void* world=nullptr;Angles angle{};};inline SavedAutoView g_savedAutoView{};
inline std::uint8_t g_scopeNone=255,g_scopeIn=255,g_scopeSwitch=255,g_scopeReload=255;
inline bool scopeEnumProfile(){
 if(g_scopeNone<16)return true;
 auto& api=ed::g_runtime.api;auto b=ed::g_runtime.unityBase;if(!api.domainAssemblyOpen||!api.assemblyGetImage||!api.classFromName||!ed::g_runtime.domain||!bytesMatch(b+0x6327AD0,{0x14001bce})||!bytesMatch(b+0x6327C50,{0x1400eca0}))return false;
 using FindField=void*(*)(void*,const char*);using Literal=void(*)(void*,void*);auto find=reinterpret_cast<FindField>(b+0x6327AD0);auto literal=reinterpret_cast<Literal>(b+0x6327C50);
 auto* assembly=api.domainAssemblyOpen(ed::g_runtime.domain,"Assembly-CSharp.dll");if(!assembly)assembly=api.domainAssemblyOpen(ed::g_runtime.domain,"Assembly-CSharp");if(!assembly)return false;auto* image=api.assemblyGetImage(assembly);if(!image)return false;auto* klass=api.classFromName(image,"","llo");if(!klass||!ed::nameEquals(ed::className(klass),"llo"))return false;
 const char* names[]={"None","InScope","SwitchScope","ScopeReload"};std::uint8_t values[4]={255,255,255,255};for(int i=0;i<4;++i){auto* f=find(klass,names[i]);if(!f)return false;literal(f,values+i);if(values[i]>=16)return false;for(int j=0;j<i;++j)if(values[i]==values[j])return false;}
 g_scopeNone=values[0];g_scopeIn=values[1];g_scopeSwitch=values[2];g_scopeReload=values[3];g_autoScopeReady.store(true);debug::log("scope enum literals resolved [%u,%u,%u,%u]",values[0],values[1],values[2],values[3]);return true;
}
struct ScopeCleanup {bool pending=false,sent=false;std::uintptr_t player=0,world=0,weapon=0;double expires=0;};inline ScopeCleanup g_scopeCleanup{};
inline double g_cameraLeaseUntil=0;inline std::uintptr_t g_cameraLeasePlayer=0,g_cameraLeaseWorld=0;
inline double autoNow(){return std::chrono::duration<double,std::milli>(Clock::now().time_since_epoch()).count();}
inline bool automationOwnsCamera(void* camera,const Configuration& c){return c.normal.enabled&&prediction::cameraLease(c.autoFire.enabled,c.autoFire.returnCamera,g_inputHookReady.load()&&g_silentHookReady.load()&&g_visibilityReady.load(),g_menuOpen.load(),g_savedAutoView.valid||g_autoShotSerial>g_autoStartSerial,autoNow(),g_cameraLeaseUntil,g_cameraLeasePlayer,g_cameraLeaseWorld,reinterpret_cast<std::uintptr_t>(object(camera,0x18)),reinterpret_cast<std::uintptr_t>(object(camera,0x20)));}
inline void* component(void* p,const char* name,input_init::ComponentStats* stats=nullptr){
 input_init::ComponentStats count{};auto finish=[&](void* c){if(stats)*stats=count;return c;};
 auto* world=object(p,0x18);auto* container=object(p,0x20);if(!p||!world||!named(container,"wmz"))return finish(nullptr);
 auto accept=[&](void* c){if(!c)return false;++count.objects;if(!named(c,name))return false;if(object(c,0x18)!=p||object(c,0x20)!=world){++count.foreign;return false;}return true;};
 for(std::size_t off:{0x10u,0x18u,0x20u,0x28u,0x30u,0x38u}){auto* arr=object(container,off);std::uintptr_t n=0;if(!field(arr,0x18,n)||n>256)continue;++count.arrays;for(std::uintptr_t i=0;i<n;++i){auto* c=object(arr,0x20+8*i);if(accept(c))return finish(c);}}
 // Dictionary<Type,chx> layout verified in matching il2cpp.h: entries+18, count+20, Entry stride18/value10.
 auto* dictionary=object(container,0x40);auto* entries=object(dictionary,0x18);std::int32_t n=0;std::uintptr_t capacity=0;
 if(field(dictionary,0x20,n)&&n>=0&&n<=256&&field(entries,0x18,capacity)&&capacity>=unsigned(n)&&capacity<=512){count.dictionary=true;for(int i=0;i<n;++i){std::int32_t hash=-1;auto off=0x20+std::size_t(i)*0x18;if(!field(entries,off,hash)||hash<0)continue;++count.entries;auto* c=object(entries,off+0x10);if(accept(c))return finish(c);}}
 return finish(nullptr);
}
inline bool autoCameraValid(void* camera,void* p,void* world){return named(camera,"ios")&&object(camera,0x18)==p&&object(camera,0x20)==world&&named(object(camera,0x38),"eir")&&object(camera,0x48)&&native(object(camera,0x50),"Transform")&&native(object(camera,0x58),"Transform");}
inline bool autoApply(void* camera,Angles angle){if(!g_functionsValid||!g_clamp||!g_apply||!g_refresh||!std::isfinite(angle.pitch)||!std::isfinite(angle.yaw))return false;angle=g_clamp(camera,angle,nullptr);if(!std::isfinite(angle.pitch)||!std::isfinite(angle.yaw))return false;g_apply(camera,angle,nullptr);g_refresh(camera,nullptr);return true;}
inline void restoreAutoView(){if(!g_savedAutoView.valid)return;auto save=g_savedAutoView;g_savedAutoView.valid=false;ed::PlayerSnapshot local{};void* world=nullptr;auto* p=localPlayer();if(p==save.player&&localAlive(p,local,world)&&world==save.world){auto* camera=component(p,"ios");if(autoCameraValid(camera,p,world)&&autoApply(camera,save.angle))debug::autoRestores.fetch_add(1);}}
inline int autoGunKind(void* weapon){if(!named(weapon,"qjf"))return 0;auto* definition=object(weapon,0x60);if(named(definition,"GunDefinition"))definition=object(definition,0x38);if(named(definition,"GunWithScopeParameters"))return 2;if(named(definition,"GunParameters")||named(definition,"ShotGunParameters"))return 1;return 0;}
inline bool autoAligned(const Selection& target,void* world,int team,Vec3 origin,Vec3 forward,float range){if(!target.valid||!g_visibilityReady.load()||!g_raycast||!g_defaultScene||!ed::isUnityMainCallback())return false;forward=normalized(forward);if(!finite(origin)||length(forward)<.9f)return false;auto scene=g_defaultScene(nullptr);Ray ray{origin,forward};assists::RaycastHit hit{};debug::autoRayChecks.fetch_add(1);if(!g_raycast(&scene,&ray,&hit,range,-5,2)||!hit.collider||!std::isfinite(hit.distance)||hit.distance<.2f||hit.distance>range+.1f||!finite(hit.point))return false;auto now=Clock::now();for(const auto& a:g_actors)if(reinterpret_cast<std::uintptr_t>(a.player)==target.player&&a.world==world&&a.team!=team&&a.hp>0&&now-a.captured<std::chrono::milliseconds(100)){for(unsigned i=0;i<a.colliderCount;++i)if(a.colliderIds[i]&&a.colliderIds[i]==hit.collider)return true;}return false;}
inline bool ownedCommandValid(){return named(g_ownedCommand,"owv")&&ed::writable(reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(g_ownedCommand)+0x32),3);}
inline void releaseOwnedCommand(){if(ownedCommandValid()){if(g_ownedAttack&&g_commandAttack)g_commandAttack(g_ownedCommand,g_previousAttack!=0,nullptr);if(g_ownedScope&&g_commandScope)g_commandScope(g_ownedCommand,g_previousScope!=0,nullptr);if(g_ownedScopeHold&&g_commandScopeHold)g_commandScopeHold(g_ownedCommand,g_previousScopeHold!=0,nullptr);}g_ownedAttack=g_ownedScope=g_ownedScopeHold=false;g_ownedCommand=nullptr;}
// Commands are leased only between early/late consumers of one producer generation.
struct CommandPhaseLease {void* self=nullptr;void* cmd=nullptr;void* player=nullptr;void* world=nullptr;std::uint32_t token=0;CommandLook look{};bool lookValid=false,lookOwned=false;};inline CommandPhaseLease g_phaseLease{};
inline void discardCommandPhase(){g_ownedAttack=g_ownedScope=g_ownedScopeHold=false;g_ownedCommand=nullptr;g_phaseLease={};}
inline bool beginCommandPhase(void* self,void* cmd){
 if(!ed::isUnityMainCallback()||!named(self,"ltc")||object(self,0x18)!=localPlayer()||!named(cmd,"owv"))return false;
 CommandPhaseLease phase{};phase.self=self;phase.cmd=cmd;phase.player=object(self,0x18);phase.world=object(self,0x20);
 std::uint8_t local=0;if(!phase.world||phase.world!=object(phase.player,offsets::field::mfc::kWorld)||!field(self,0x28,local)||local!=1||!field(cmd,0x10,phase.token)||!ed::writable(reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(cmd)+0x32),3))return false;
 phase.lookValid=field(cmd,0x1c,phase.look)&&phase.look.has<=1;g_phaseLease=phase;return true;
}
inline void finishCommandPhase(void* self){
 if(!g_phaseLease.cmd||self!=g_phaseLease.self||!ed::isUnityMainCallback())return;
 auto phase=g_phaseLease;std::uint32_t token=0;
 if(named(phase.cmd,"owv")&&field(phase.cmd,0x10,token)&&token==phase.token&&object(self,0x18)==phase.player&&object(self,0x20)==phase.world){
  releaseOwnedCommand();
  if(phase.lookOwned&&phase.lookValid&&ed::writable(reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(phase.cmd)+0x1c),12))std::memcpy(reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(phase.cmd)+0x1c),&phase.look,12);
  debug::commandCompleted.fetch_add(1);
 }
 discardCommandPhase();
}
inline void autoShotObserved(void* self,void* ctx,Ray* rays,int count){if(!ed::isUnityMainCallback()||!g_autoCycle.active()||g_autoCycle.stage!=automation::Stage::WaitingShot||!g_ownedAttack||g_autoShotSerial>g_autoStartSerial||!stackSpan(rays,count))return;auto* p=localPlayer();void* world=nullptr;ed::PlayerSnapshot local{};std::uint64_t owner=0,id=0;if(localAlive(p,local,world)&&reinterpret_cast<std::uintptr_t>(p)==g_autoCycle.player&&reinterpret_cast<std::uintptr_t>(world)==g_autoCycle.world&&reinterpret_cast<std::uintptr_t>(local.activeWeapon)==g_autoCycle.weapon&&object(self,0x18)==world&&field(ctx,0,owner)&&field(p,0x10,id)&&id==owner&&object(ctx,0x48)==local.activeWeapon){++g_autoShotSerial;debug::autoShots.fetch_add(1);auto restores=debug::autoRestores.load();restoreAutoView();if(debug::autoRestores.load()>restores)g_cameraLeaseUntil=autoNow()+std::min(configuration().autoFire.intervalMs,120.f);}}
inline void processAutomation(void* self,void* cmd){
 if(!ed::isUnityMainCallback()||object(self,0x18)!=localPlayer())return;
 auto c=configuration();auto options=c.autoFire;automation::Observation o{};automation::Actions actions{};ed::PlayerSnapshot local{};void* world=nullptr;auto* p=object(self,0x18);std::uint8_t raw=255,scope=255,attack=0,alt=0,held=0,ammo=0;
 bool identity=p==localPlayer()&&named(self,"ltc")&&localAlive(p,local,world)&&object(self,0x20)==world&&object(self,0x50)==local.weaponPart&&named(cmd,"owv");
 bool data=identity&&field(local.weaponPart,0x80,raw)&&field(local.weaponPart,0x8c,scope)&&field(local.activeWeapon,0x52,ammo)&&field(cmd,0x32,attack)&&field(cmd,0x33,alt)&&field(cmd,0x34,held)&&attack<=1&&alt<=1&&held<=1;
 auto* camera=identity?component(p,"ios"):nullptr;int kind=data?autoGunKind(local.activeWeapon):0;options.autoScope=options.autoScope&&g_autoScopeReady.load()&&kind==2;debug::autoSafetyMask.store((identity?1:0)|(data?2:0)|(kind?4:0)|(ammo?8:0)|(g_visibilityReady.load()?16:0)|(g_autoScopeReady.load()?32:0));
 bool safe=data&&g_functionsValid&&g_inputHookReady.load()&&g_silentHookReady.load()&&g_visibilityReady.load()&&!g_menuOpen.load()&&c.normal.bodies&&(ammo>0||g_autoShotSerial>g_autoStartSerial)&&kind&&(!options.autoScope||(g_scopeNone<16&&(scope==g_scopeNone||scope==g_scopeIn||scope==g_scopeSwitch||scope==g_scopeReload)))&&(raw<=1)&&autoCameraValid(camera,p,world);
 o.safe=safe;o.manual=attack||alt||held;o.player=reinterpret_cast<std::uintptr_t>(p);o.world=reinterpret_cast<std::uintptr_t>(world);o.weapon=reinterpret_cast<std::uintptr_t>(local.activeWeapon);o.scopeSupported=kind==2;o.scoped=scope==g_scopeIn;o.shot=g_autoCycle.active()&&g_autoShotSerial>g_autoStartSerial;
 Selection target{};Vec3 origin{},forward{};std::uintptr_t lock=0;bool pose=safe&&transformPose(object(local.weaponPart,offsets::field::nrk::kHitTransform),origin,forward);
 if(pose){auto settings=c.normal;settings.visibleCheck=true;settings.prediction=false;settings.enabled=true;settings.lockTarget=g_autoCycle.active();if(g_autoCycle.active())lock=g_autoCycle.target;
 if(!options.returnCamera){settings.fov=std::min(settings.fov,5.f);}auto base=options.returnCamera&&g_savedAutoView.valid?forwardFor(g_savedAutoView.angle):forward;target=select(settings,world,local.team,origin,base,lock);o.target=target.valid;o.targetId=target.player;o.aligned=autoAligned(target,world,local.team,origin,forward,settings.maxDistance);}
 else o.safe=false;
 bool closeRequested=false;auto now=autoNow();
 if(g_scopeCleanup.pending){if(!identity||o.player!=g_scopeCleanup.player||o.world!=g_scopeCleanup.world||o.weapon!=g_scopeCleanup.weapon||o.manual||now>g_scopeCleanup.expires||scope==g_scopeNone)g_scopeCleanup={};else{if(scope==g_scopeIn&&!g_scopeCleanup.sent){closeRequested=true;}options.enabled=false;}}
 bool wasActive=g_autoCycle.active();actions=g_autoCycle.step(now,options,o);
 if(actions.save&&pose){g_savedAutoView={true,p,world,anglesFor(forward)};g_autoStartSerial=g_autoShotSerial;}
 else if(!wasActive&&g_autoCycle.active())g_autoStartSerial=g_autoShotSerial;
 if((actions.completed||(wasActive&&!g_autoCycle.active()))&&g_autoCycle.scopeOpened&&!g_autoCycle.originalScoped&&data&&o.weapon==g_autoCycle.weapon&&o.world==g_autoCycle.world&&scope!=g_scopeNone&&!o.manual)g_scopeCleanup={true,false,o.player,o.world,o.weapon,now+2000};
 if(actions.restore){restoreAutoView();if(data&&o.weapon==g_autoCycle.weapon&&g_autoCycle.scopeOpened&&!g_autoCycle.originalScoped&&o.scoped&&!alt&&!held)actions.scopePulse=true;}
 if(actions.aim&&target.valid&&pose){auto desired=anglesFor(sub(target.point,origin));Angles recoil{};if(c.normal.rcs&&c.normal.rcsStrength>0&&readRecoil(local.weaponPart,recoil))desired=assists::compensate(desired,recoil,c.normal.rcsStrength);desired=g_clamp(camera,desired,nullptr);if(!autoApply(camera,desired)){actions.attack=false;restoreAutoView();g_autoCycle.cancel(now,options);}else{g_cameraLeasePlayer=o.player;g_cameraLeaseWorld=o.world;g_cameraLeaseUntil=now+80;if(g_commandLook&&g_phaseLease.cmd==cmd&&g_phaseLease.lookValid&&ed::writable(reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(cmd)+0x1c),12)){g_commandLook(cmd,CommandLook{1,{0,0,0},desired.pitch,desired.yaw},nullptr);g_phaseLease.lookOwned=true;}}}
 if(actions.attack){Vec3 freshOrigin{},freshForward{};if(!transformPose(object(local.weaponPart,offsets::field::nrk::kHitTransform),freshOrigin,freshForward)||!autoAligned(target,world,local.team,freshOrigin,freshForward,c.normal.maxDistance))actions.attack=false;}
 actions.scopePulse=actions.scopePulse||closeRequested;
 bool holdScope=g_autoCycle.scopeOpened&&g_autoCycle.active()&&!actions.completed;
 if(data&&!o.manual&&(actions.attack||actions.scopePulse||holdScope)&&ed::writable(reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(cmd)+0x32),3)){
 g_ownedCommand=cmd;g_previousAttack=attack;g_previousScope=alt;g_previousScopeHold=held;
 if(actions.attack&&safe){g_commandAttack(cmd,true,nullptr);g_ownedAttack=true;debug::autoRequests.fetch_add(1);}
 if(actions.scopePulse&&kind==2){g_commandScope(cmd,true,nullptr);g_ownedScope=true;debug::autoScopePulses.fetch_add(1);if(g_scopeCleanup.pending)g_scopeCleanup.sent=true;}
 if(holdScope&&kind==2&&safe){g_commandScopeHold(cmd,true,nullptr);g_ownedScopeHold=true;}
 }
 if(!safe||o.manual||!c.autoFire.enabled||(wasActive&&!g_autoCycle.active()&&!actions.completed))g_cameraLeaseUntil=0;
 if(actions.timedOut)debug::autoTimeouts.fetch_add(1);g_autoStage.store(int(g_autoCycle.stage));debug::autoReason.store(!options.enabled?0:(g_menuOpen.load()?1:(!identity?3:(!safe?4:(actions.timedOut?10:(o.manual?11:(!o.target?6:7)))))));
}
#include "features/aim/command_phase_runtime.hpp"
