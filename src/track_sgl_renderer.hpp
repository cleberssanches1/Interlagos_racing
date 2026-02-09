#pragma once

#include <srl.hpp>
#include <vector>
#include <sgl.h>

namespace TrackSglRenderer
{
    inline void DebugAttr(const ATTR& attr, unsigned faceIdx)
    {
        SRL::Debug::Print(1, 70, "TrackAttr face:%u flag:%#x sort:%#x tex:%u color:%#x gouraud:%#x dir:%#x",
                          faceIdx, attr.flag, attr.sort, attr.texno, attr.colno, attr.gstb, attr.dir);
    }

    inline void DrawMesh(const SRL::Math::Types::Vector3D* verts, size_t vertCount,
                         const SRL::Types::Polygon* faces, size_t faceCount,
                         uint16_t color, const SRL::Math::Types::Vector3D& offset,
                         SRL::Math::Types::Fxp scale, bool logAttrs = false)
    {
        if (!verts || !faces || vertCount == 0 || faceCount == 0) return;

        std::vector<POINT> points(vertCount);
        for (size_t i = 0; i < vertCount; ++i)
        {
            auto v = verts[i] * scale + offset;
            points[i][0] = (FIXED)v.X.RawValue();
            points[i][1] = (FIXED)v.Y.RawValue();
            points[i][2] = (FIXED)v.Z.RawValue();
        }

        std::vector<POLYGON> poly(faceCount);
        std::vector<ATTR>   attrs(faceCount);

        for (size_t f = 0; f < faceCount; ++f)
        {
            const auto& src = faces[f];
            auto& dst = poly[f];
            dst.Vertices[0] = src.Vertices[0];
            dst.Vertices[1] = src.Vertices[1];
            dst.Vertices[2] = src.Vertices[2];
            dst.Vertices[3] = src.Vertices[3];

            ATTR a{};
            a.flag  = Dual_Plane;
            a.sort  = UseLight;
            a.texno = No_Texture;
            a.atrb  = static_cast<uint16_t>(static_cast<uint16_t>(sprPolygon) |
                                            static_cast<uint16_t>(CL32KRGB));
            a.colno = color & 0x7FFF;
            a.gstb  = 0;
            a.dir   = UseLight;
            attrs[f] = a;

            if (logAttrs && f == 0)
            {
                DebugAttr(a, (unsigned)f);
            }
        }

        PDATA pdata{
            points.data(),
            static_cast<uint32_t>(vertCount),
            poly.data(),
            static_cast<uint32_t>(faceCount),
            attrs.data()
        };

        SRL::Debug::Print(1, 29, "TrackSgl Ren put mesh faces:%u verts:%u", (unsigned)faceCount, (unsigned)vertCount);
        slPutPolygon(&pdata);
    }
}
