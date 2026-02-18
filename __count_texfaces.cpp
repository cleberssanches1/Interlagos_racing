#include <cstdint>
#include <cstdio>
#include <vector>
#include <fstream>
static uint32_t be32(const uint8_t*p){return (uint32_t(p[0])<<24)|(uint32_t(p[1])<<16)|(uint32_t(p[2])<<8)|uint32_t(p[3]);}
int main(){const char*fn="cd/data/SEG_001.NYA";std::ifstream f(fn,std::ios::binary);if(!f)return 1;f.seekg(0,std::ios::end);size_t sz=(size_t)f.tellg();f.seekg(0);std::vector<uint8_t>b(sz);f.read((char*)b.data(),sz);uint32_t t=be32(&b[0]),mc=be32(&b[4]),tc=be32(&b[8]);size_t off=12;uint32_t textured=0,poltot=0;for(uint32_t mi=0;mi<mc;mi++){uint32_t pts=be32(&b[off]),pol=be32(&b[off+4]);off+=8;size_t v=size_t(pts)*12,p=size_t(pol)*20,a=size_t(pol)*8,n=(t==1)?size_t(pts)*12:0;size_t ao=off+v+p;for(uint32_t i=0;i<pol;i++){if(b[ao+i*8]&0x80)textured++;} poltot+=pol; off+=v+p+a+n;} printf("texCount=%u polygons=%u texturedFaces=%u\n",tc,poltot,textured);}
