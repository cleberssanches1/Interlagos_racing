#include <cstdint>
#include <cstdio>
#include <vector>
#include <fstream>
static uint32_t be32(const uint8_t*p){return (uint32_t(p[0])<<24)|(uint32_t(p[1])<<16)|(uint32_t(p[2])<<8)|uint32_t(p[3]);}
int main(){std::ifstream f("cd/data/SEG_001.NYA",std::ios::binary);if(!f){puts("open fail");return 1;}f.seekg(0,std::ios::end);size_t sz=(size_t)f.tellg();f.seekg(0);std::vector<uint8_t>b(sz);f.read((char*)b.data(),sz);uint32_t t=be32(&b[0]);size_t off=12;uint32_t pts=be32(&b[off]),pol=be32(&b[off+4]);off+=8;size_t ao=off + size_t(pts)*12 + size_t(pol)*20;printf("type=%u pts=%u pol=%u\n",t,pts,pol);for(uint32_t i=0;i<((pol<10)?pol:10);++i){uint8_t fl=b[ao+i*8],fl2=b[ao+i*8+1];int32_t tid=(int32_t)be32(&b[ao+i*8+4]);printf("f%u flags=%02X flags2=%02X tid=%d\n",i,fl,fl2,tid);}return 0;}
