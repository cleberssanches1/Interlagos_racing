#include <cstdint>
#include <cstdio>
#include <vector>
#include <fstream>
#include <string>
static uint32_t be32(const uint8_t*p){return (uint32_t(p[0])<<24)|(uint32_t(p[1])<<16)|(uint32_t(p[2])<<8)|uint32_t(p[3]);}
static uint16_t be16(const uint8_t*p){return (uint16_t(p[0])<<8)|uint16_t(p[1]);}
int main(int argc,char**argv){if(argc<2){puts("usage");return 1;}std::ifstream f(argv[1],std::ios::binary);if(!f){puts("open fail");return 1;}f.seekg(0,std::ios::end);size_t sz=(size_t)f.tellg();f.seekg(0);std::vector<uint8_t>b(sz);f.read((char*)b.data(),sz);if(sz<12)return 1;uint32_t type=be32(&b[0]),mc=be32(&b[4]),tc=be32(&b[8]);printf("%s type=%u mesh=%u tex=%u size=%zu\n",argv[1],type,mc,tc,sz);size_t off=12;for(uint32_t mi=0;mi<mc;mi++){uint32_t pts=be32(&b[off]),pol=be32(&b[off+4]);off+=8;size_t verts=size_t(pts)*12;size_t polys=size_t(pol)*20;size_t attrs=size_t(pol)*8;size_t norms=(type==1)?size_t(pts)*12:0; if(mi==0){int bad=0; for(uint32_t i=0;i<pol;i++){size_t ao=off+verts+polys+i*8; int32_t tid=(int32_t)be32(&b[ao+4]); uint8_t fl=b[ao]; if((fl&0x80) && (tid<0 || tid>=(int32_t)tc)) bad++; if(i<6) printf(" face%u flags=%02X tid=%d\n",i,fl,tid);} printf(" first mesh bad textured-face ids=%d\n",bad);} off += verts+polys+attrs+norms; }
int badw=0,badh=0; for(uint32_t ti=0; ti<tc; ++ti){ if(off+4>sz){printf("tex hdr oob at %zu\n",off); break;} uint16_t w=be16(&b[off]), h=be16(&b[off+2]); if((w%8)!=0) badw++; if(h==0) badh++; if(ti<8) printf(" tex%u %ux%u\n",ti,w,h); size_t bytes=size_t(w)*size_t(h)*2; off += 4+bytes; }
printf(" bad width%%8:%d bad height0:%d finalOff:%zu\n",badw,badh,off);
return 0; }
