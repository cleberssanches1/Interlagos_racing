#include <cstdint>
#include <cstdio>
#include <vector>
#include <fstream>
#include <cmath>
#include <set>

static uint32_t be32(const uint8_t*p){return (uint32_t(p[0])<<24)|(uint32_t(p[1])<<16)|(uint32_t(p[2])<<8)|uint32_t(p[3]);}
static uint16_t be16(const uint8_t*p){return (uint16_t(p[0])<<8)|uint16_t(p[1]);}

struct V3 { double x,y,z; };
static double triArea2(const V3&a,const V3&b,const V3&c){
    double abx=b.x-a.x, aby=b.y-a.y, abz=b.z-a.z;
    double acx=c.x-a.x, acy=c.y-a.y, acz=c.z-a.z;
    double cx=aby*acz-abz*acy;
    double cy=abz*acx-abx*acz;
    double cz=abx*acy-aby*acx;
    return std::sqrt(cx*cx+cy*cy+cz*cz);
}

int main(int argc,char**argv){
    const char* path = (argc>1)?argv[1]:"cd/data/SEG_001.NYA";
    std::ifstream f(path,std::ios::binary);
    if(!f){ puts("open fail"); return 1; }
    f.seekg(0,std::ios::end); size_t sz=(size_t)f.tellg(); f.seekg(0);
    std::vector<uint8_t>b(sz); f.read((char*)b.data(),sz);
    if(sz<12){ puts("small"); return 1; }

    uint32_t type=be32(&b[0]), meshCount=be32(&b[4]), texCount=be32(&b[8]);
    size_t off=12;
    printf("file=%s type=%u mesh=%u tex=%u size=%zu\n", path, type, meshCount, texCount, sz);

    const double EPS = 1e-8;
    int degCount=0;
    std::set<uint32_t> degVertIds;

    for(uint32_t mi=0; mi<meshCount; ++mi){
        if(off+8>sz){ printf("mesh hdr oob\n"); return 2; }
        uint32_t pts=be32(&b[off]), pol=be32(&b[off+4]); off+=8;
        size_t vertsOff=off;
        size_t polysOff=vertsOff + size_t(pts)*12;
        size_t attrsOff=polysOff + size_t(pol)*20;
        size_t normsOff=attrsOff + size_t(pol)*8;
        size_t meshEnd = normsOff + (type==1? size_t(pts)*12 : 0);
        if(meshEnd>sz){ printf("mesh block oob\n"); return 3; }

        std::vector<V3> verts(pts);
        for(uint32_t i=0;i<pts;i++){
            int32_t rx=(int32_t)be32(&b[vertsOff + i*12 + 0]);
            int32_t ry=(int32_t)be32(&b[vertsOff + i*12 + 4]);
            int32_t rz=(int32_t)be32(&b[vertsOff + i*12 + 8]);
            verts[i].x = double(rx) / 65536.0;
            verts[i].y = double(ry) / 65536.0;
            verts[i].z = double(rz) / 65536.0;
        }

        for(uint32_t fi=0; fi<pol; ++fi){
            const uint8_t* p=&b[polysOff + fi*20];
            uint16_t i0=be16(p+12), i1=be16(p+14), i2=be16(p+16), i3=be16(p+18);
            if(i0>=pts||i1>=pts||i2>=pts||i3>=pts) continue;
            const V3 &a=verts[i0], &c=verts[i2];
            double area2=0.0;
            bool isTri = (i2==i3) || (i1==i2) || (i0==i1);
            if(isTri){
                const V3 &b1=verts[i1];
                area2 = triArea2(a,b1,c);
            } else {
                const V3 &b1=verts[i1], &d=verts[i3];
                area2 = triArea2(a,b1,c) + triArea2(a,c,d);
            }
            if(area2 < EPS){
                degCount++;
                degVertIds.insert(i0); degVertIds.insert(i1); degVertIds.insert(i2); degVertIds.insert(i3);
                printf("DEGENERATE mesh=%u face=%u idx=[%u,%u,%u,%u]\n",mi,fi,i0,i1,i2,i3);
                printf("  v%u=(%.6f, %.6f, %.6f)\n", i0, verts[i0].x, verts[i0].y, verts[i0].z);
                printf("  v%u=(%.6f, %.6f, %.6f)\n", i1, verts[i1].x, verts[i1].y, verts[i1].z);
                printf("  v%u=(%.6f, %.6f, %.6f)\n", i2, verts[i2].x, verts[i2].y, verts[i2].z);
                printf("  v%u=(%.6f, %.6f, %.6f)\n", i3, verts[i3].x, verts[i3].y, verts[i3].z);
            }
        }

        off = meshEnd;
    }

    printf("degenerate_faces=%d unique_vertices=%zu\n",degCount,degVertIds.size());
    if(!degVertIds.empty()){
        printf("degenerate_vertex_indices_zero_based:");
        for(auto id:degVertIds) printf(" %u",id);
        printf("\n");
    }
    return 0;
}
