#pragma once
#include <array>
#include <cstdint>
namespace lemming::aim::native_pair {
enum Result { Ready, Changed, WritableDenied, VerifyFailed, RestoreDenied, RollbackFailed };
template<class Ops> Result apply(Ops& ops,const std::array<std::uintptr_t,2>& sites,const std::array<std::uint32_t,2>& old,const std::array<std::uint32_t,2>& replacement,std::uintptr_t pageSize){
 std::array<std::uintptr_t,2> pages{sites[0]&~(pageSize-1),sites[1]&~(pageSize-1)};
 unsigned n=pages[0]==pages[1]?1:2;
 auto exact=[&](const auto& values){for(unsigned i=0;i<2;++i){std::uint32_t v=0;if(!ops.read(sites[i],v)||v!=values[i])return false;}return true;};
 auto rx=[&](){bool ok=true;for(unsigned i=0;i<n;++i)ok=ops.rx(pages[i],pageSize)&&ok;return ok;};
 if(!exact(old))return Changed;
 for(unsigned i=0;i<n;++i)if(!ops.rwx(pages[i],pageSize))return rx()?WritableDenied:RestoreDenied;
 if(!exact(old)){return rx()?Changed:RestoreDenied;}
 for(unsigned i=0;i<2;++i){ops.write(sites[i],replacement[i]);ops.flush(sites[i]);}
 bool verified=exact(replacement);bool protectedAgain=rx();
 if(verified&&protectedAgain)return Ready;
 bool canRestore=true;for(unsigned i=0;i<n;++i)canRestore=ops.rwx(pages[i],pageSize)&&canRestore;
 if(!canRestore){rx();return RollbackFailed;}
 for(unsigned i=0;i<2;++i){ops.write(sites[i],old[i]);ops.flush(sites[i]);}
 bool restored=exact(old);bool protectedOld=rx();
 if(!restored||!protectedOld)return RollbackFailed;
 return verified?RestoreDenied:VerifyFailed;
}
}
