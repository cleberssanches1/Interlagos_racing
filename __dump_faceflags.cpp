#include <cstdint>
#include <cstdio>
#include <vector>
#include <fstream>
static uint32_t be32(const uint8_t*p){return (uint32_t(p[0])<<24)|(uint32_t(p[1])<<16)|(uint32_t(p[2])<<8)|uint32_t(p[3]);}
int main(int argc,char**argv){if(argc<2)return 1; std::ifstream f(argv[1],std::ios::binary); if(!f)return 1; f.seekg(0,std::ios::end); size_t sz=(size_t)f.tellg(); f.seekg(0); std::vector<uint8_t>b(sz); f.read((char*)b.data(),sz);
 uint32_t t=be32(&b[0]),mc=be32(&b[4]); size_t off=12; printf("%s\n",argv[1]);
 for(uint32_t mi=0;mi<mc;mi++){
  uint32_t pts=be32(&b[off]),pol=be32(&b[off+4]); off+=8; size_t ao=off+size_t(pts)*12+size_t(pol)*20;
  for(uint32_t i=0;i<pol;i++){
   uint8_t fl=b[ao+i*8], fl2=b[ao+i*8+1]; int32_t tid=(int32_t)be32(&b[ao+i*8+4]);
   if(fl&0x80){
    int trans=(fl&0x10)?1:0, half=(fl&0x04)?1:0, dbl=(fl&0x20)?1:0, mesh=(fl&0x40)?1:0, flat=(fl&0x08)?1:0, wire=(fl2&0x80)?1:0;
    printf("mesh%u face%u flags=%02X f2=%02X tid=%d tex=1 trans=%d half=%d dbl=%d mesh=%d flat=%d wire=%d\n",mi,i,fl,fl2,tid,trans,half,dbl,mesh,flat,wire);
   }
  }
  off += size_t(pts)*12+size_t(pol)*20+size_t(pol)*8+(t==1?size_t(pts)*12:0);
 }
}
