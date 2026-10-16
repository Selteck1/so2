#pragma once
#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <limits>
#include <string>
#include <vector>
#include <utility>
#include <sys/mman.h>
namespace lemming::aim::hook_support {
using Address=std::uintptr_t;
struct Mapping{Address begin,end;};
struct AllocationStats{unsigned candidates=0,attempts=0,relocated=0;int lastError=0;};
inline bool cameraSlotMatches(Address fn,Address expected,Address metadata,bool metadataReadable,Address textBegin,Address textEnd){
 // The live adjacent metadata is not a plaintext MethodInfo. The already
 // signature-validated native target, class, readable entry and write probe
 // establish the hook; do not reinterpret metadata fields as function pointers.
 return expected&&!(expected&3)&&textBegin<=expected&&expected<textEnd&&fn==expected&&metadata&&metadataReadable;
}
inline bool pageSizeValid(Address size){return size>=4096&&size<=65536&&(size&(size-1))==0;}
inline bool branchFits(Address site,Address destination){
 if(!site||!destination||(site&3)||(destination&3))return false;
 return destination>=site?destination-site<0x8000000u:site-destination<=0x8000000u;
}
inline bool branchWord(Address site,Address destination,std::uint32_t& word){
 if(!branchFits(site,destination))return false;
 std::intptr_t delta=destination>=site?std::intptr_t(destination-site):-std::intptr_t(site-destination);
 word=0x94000000u|(std::uint32_t(delta/4)&0x03ffffffu);return true;
}
inline std::vector<Address> relayCandidates(Address site,Address size,std::vector<Mapping> maps){
 std::vector<Address> result;if(!pageSizeValid(size)||(site&3)||maps.empty())return result;
 constexpr Address span=0x8000000u;const Address max=std::numeric_limits<Address>::max();
 Address low=std::max(size,site>=span?site-span:Address(0));Address high=site>max-span?max:site+span;
 if(high<=low||high-low<size)return result;
 std::sort(maps.begin(),maps.end(),[](const Mapping&a,const Mapping&b){return a.begin<b.begin;});
 auto addGap=[&](Address begin,Address end){
  if(end<=begin||end-begin<size||begin>max-(size-1))return;
  Address first=(begin+size-1)&~(size-1),last=(end-size)&~(size-1);
  if(first>last)return;
  Address nearest=std::clamp(site&~(size-1),first,last);
  for(Address candidate:{nearest,first,last})if(branchFits(site,candidate))result.push_back(candidate);
 };
 Address cursor=low;
 for(const auto& m:maps){
  if(m.end<=m.begin||m.end<=low)continue;
  if(m.begin>=high)break;
  if(m.begin>cursor)addGap(cursor,std::min(m.begin,high));
  cursor=std::max(cursor,std::min(m.end,high));if(cursor==high)break;
 }
 if(cursor<high)addGap(cursor,high);
 std::sort(result.begin(),result.end());result.erase(std::unique(result.begin(),result.end()),result.end());
 auto distance=[&](Address a){return a>=site?a-site:site-a;};
 std::sort(result.begin(),result.end(),[&](Address a,Address b){auto da=distance(a),db=distance(b);return da==db?a<b:da<db;});
 if(result.size()>384)result.resize(384);
 return result;
}
inline std::vector<Mapping> selfMappings(){
 std::ifstream f("/proc/self/maps");std::vector<Mapping> maps;std::string line;
 while(std::getline(f,line)){unsigned long long begin=0,end=0;if(std::sscanf(line.c_str(),"%llx-%llx",&begin,&end)==2&&begin<end)maps.push_back({Address(begin),Address(end)});}
 if(f.bad())maps.clear();
 return maps;
}
inline void* allocateNear(Address site,Address size,AllocationStats& stats){
 auto maps=selfMappings();if(maps.empty()){stats.lastError=EIO;return MAP_FAILED;}
 auto candidates=relayCandidates(site,size,std::move(maps));stats.candidates=unsigned(candidates.size());
 // Linux MAP_FIXED_NOREPLACE never replaces an existing mapping. Do NOT use
 // MAP_FIXED. Older kernels may ignore this flag; validate every returned address.
 constexpr int noReplace=0x100000;
 for(Address candidate:candidates){
  ++stats.attempts;errno=0;
  void* p=mmap(reinterpret_cast<void*>(candidate),size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|noReplace,-1,0);
  if(p==MAP_FAILED&&errno==EINVAL){++stats.attempts;p=mmap(reinterpret_cast<void*>(candidate),size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);}
  if(p==MAP_FAILED){stats.lastError=errno;continue;}
  auto address=reinterpret_cast<Address>(p);
  if(branchFits(site,address)){stats.lastError=0;return p;}
  ++stats.relocated;munmap(p,size);
 }
 return MAP_FAILED;
}
}
