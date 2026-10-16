// Included in the runtime namespace after processAutomation.
using CommandGateFn=bool(*)(void*,void*,const void*);
using CommandEndFn=void(*)(void*,const void*);
inline std::atomic<CommandGateFn> g_commandGateOriginal{nullptr};
inline std::atomic<CommandEndFn> g_commandEndOriginal{nullptr};
inline std::array<void*,2> g_commandRelays{};
inline bool commandGate(void* self,void* cmd,const void* method){
 bool ours=ed::isUnityMainCallback()&&named(self,"ltc")&&object(self,0x18)==localPlayer();
 if(ours&&g_inputHookReady.load()){
  std::uint32_t token=0;field(cmd,0x10,token);
  if(g_phaseLease.cmd){if(g_phaseLease.self==self&&g_phaseLease.cmd==cmd&&g_phaseLease.token==token)finishCommandPhase(self);else discardCommandPhase();}
  if(beginCommandPhase(self,cmd)){debug::inputCalls.fetch_add(1);processAutomation(self,cmd);}
 }
 auto original=g_commandGateOriginal.load(std::memory_order_acquire);
 bool accepted=original?original(self,cmd,method):false;
 if(ours&&!accepted&&g_ownedAttack)debug::commandRejected.fetch_add(1);
 return accepted;
}
inline void commandEnd(void* self,const void* method){
 auto original=g_commandEndOriginal.load(std::memory_order_acquire);
 if(original)original(self,method);
 if(ed::isUnityMainCallback())finishCommandPhase(self);
}
inline bool installInput(void* p){
 if(g_inputHookReady.load()){g_inputInstallReason.store(input_init::Ready);return true;}
 auto attempt=debug::inputAttempts.fetch_add(1)+1;
 auto result=[&](int code){int previous=g_inputInstallReason.exchange(code);if(previous!=code||attempt<=3||attempt%10==0)debug::log("input install attempt=%llu backend=ltc-command-phase reason=%s local=%p",(unsigned long long)attempt,input_init::reasonText(code),p);return code==input_init::Ready;};
 if(!ed::isUnityMainCallback())return result(input_init::WrongThread);
 if(!p||p!=localPlayer())return result(input_init::NoLocal);
 if(!ed::g_runtime.ready||!g_functionsValid)return result(input_init::RuntimeNotReady);
#if defined(__aarch64__)
 ed::PlayerSnapshot local{};void* world=nullptr;if(!localAlive(p,local,world))return result(input_init::InvalidLocal);
 auto b=ed::g_runtime.unityBase;
 if(!bytesMatch(b+aim_offsets::kCommandGate,{0xa9be57fe,0xa9014ff4,0xf0018755,0xaa0103f4})||!bytesMatch(b+aim_offsets::kCommandEnd,{0xa9be57fe,0xa9014ff4,0xb0018773,0xaa0003f4})||!bytesMatch(b+aim_offsets::kCommandGateSite-8,{0xaa1303e0,0xaa1403e1,0x97fff58f,0x360000c0})||!bytesMatch(b+aim_offsets::kCommandEndSite-4,{0xaa1303e0,0x97ffe0af,0xa9464ff4})||!bytesMatch(b+aim_offsets::kCommandAttack,{0x3900c801,0xd65f03c0})||!bytesMatch(b+aim_offsets::kCommandScope,{0x3900cc01,0xd65f03c0})||!bytesMatch(b+aim_offsets::kCommandScopeHold,{0x3900d001,0xd65f03c0})||!bytesMatch(b+aim_offsets::kCommandLook,{0xd10103ff,0xa90257fe,0xa9034ff4,0x90018a35}))return result(input_init::SignatureRejected);
 if(!scopeEnumProfile()&&(attempt<=3||attempt%10==0))debug::log("scope enum unavailable: only Auto scope paused; Triggerbot and Back camera remain independent");
 long raw=sysconf(_SC_PAGESIZE);if(raw<=0||!hook_support::pageSizeValid(std::uintptr_t(raw)))return result(input_init::TextProtectionFailed);
 auto pageSize=std::uintptr_t(raw);
 std::array<std::uintptr_t,2> sites{b+aim_offsets::kCommandGateSite,b+aim_offsets::kCommandEndSite};
 std::array<std::uint32_t,2> old{0x97fff58f,0x97ffe0af},branches{};
 std::array<std::uintptr_t,2> destinations{reinterpret_cast<std::uintptr_t>(&commandGate),reinterpret_cast<std::uintptr_t>(&commandEnd)};
 std::array<void*,2> relays{};
 auto drop=[&](){for(auto* r:relays)if(r&&r!=MAP_FAILED)munmap(r,pageSize);};
 for(unsigned i=0;i<2;++i){hook_support::AllocationStats stats{};relays[i]=hook_support::allocateNear(sites[i],pageSize,stats);if(relays[i]==MAP_FAILED){drop();return result(input_init::RelayAllocationFailed);}if(!hook_support::branchWord(sites[i],reinterpret_cast<std::uintptr_t>(relays[i]),branches[i])){drop();return result(input_init::RelayAllocationFailed);}std::uint32_t instructions[2]{0x58000051,0xd61f0220};std::memcpy(relays[i],instructions,8);std::memcpy(static_cast<char*>(relays[i])+8,&destinations[i],8);__builtin___clear_cache(static_cast<char*>(relays[i]),static_cast<char*>(relays[i])+16);if(mprotect(relays[i],pageSize,PROT_READ|PROT_EXEC)){drop();return result(input_init::RelayProtectionFailed);}}
 g_commandAttack=reinterpret_cast<CommandBoolFn>(b+aim_offsets::kCommandAttack);g_commandScope=reinterpret_cast<CommandBoolFn>(b+aim_offsets::kCommandScope);g_commandScopeHold=reinterpret_cast<CommandBoolFn>(b+aim_offsets::kCommandScopeHold);g_commandLook=reinterpret_cast<CommandLookFn>(b+aim_offsets::kCommandLook);
 g_commandGateOriginal.store(reinterpret_cast<CommandGateFn>(b+aim_offsets::kCommandGate),std::memory_order_release);g_commandEndOriginal.store(reinterpret_cast<CommandEndFn>(b+aim_offsets::kCommandEnd),std::memory_order_release);
 struct Ops {bool read(std::uintptr_t p,std::uint32_t& v){return ed::readValue(reinterpret_cast<void*>(p),v);}bool rwx(std::uintptr_t p,std::uintptr_t n){return mprotect(reinterpret_cast<void*>(p),n,PROT_READ|PROT_WRITE|PROT_EXEC)==0;}bool rx(std::uintptr_t p,std::uintptr_t n){return mprotect(reinterpret_cast<void*>(p),n,PROT_READ|PROT_EXEC)==0;}void write(std::uintptr_t p,std::uint32_t v){__atomic_store_n(reinterpret_cast<std::uint32_t*>(p),v,__ATOMIC_RELEASE);}void flush(std::uintptr_t p){__builtin___clear_cache(reinterpret_cast<char*>(p),reinterpret_cast<char*>(p+4));}} ops;
 auto installed=native_pair::apply(ops,sites,old,branches,pageSize);
 if(installed!=native_pair::Ready){
  std::uint32_t a=0,z=0;bool untouched=ops.read(sites[0],a)&&ops.read(sites[1],z)&&a==old[0]&&z==old[1];
  if(untouched)drop();else{g_commandRelays=relays;debug::log("input pair rollback incomplete; relays retained, automation fail-closed");}
  return result(installed==native_pair::Changed?input_init::CallsiteRejected:(installed==native_pair::WritableDenied?input_init::TextProtectionFailed:(installed==native_pair::RestoreDenied?input_init::RestoreProtectionFailed:input_init::PairReadbackFailed)));
 }
 g_commandRelays=relays;g_inputHookReady.store(true,std::memory_order_release);debug::log("input hook ready backend=ltc-command-phase: before attack gate / after late WeaponAction; exact ownership retained");return result(input_init::Ready);
#else
 return result(input_init::UnsupportedArchitecture);
#endif
}
