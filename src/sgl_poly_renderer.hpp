#pragma once
#include <srl.hpp>
#include <sgl.h>
#include <vector>
#include "modelObject.hpp"

// Helper para desenhar um mesh (faces planar) diretamente via SGL/VDP1.
// Espera verts/faces em RAM (ja desserializados) e aplica offset/escala.
namespace SglPoly
{
    inline void DrawMesh(const SRL::Math::Types::Vector3D* verts, size_t vertCount,
                         const SRL::Types::Polygon* faces, size_t faceCount,
                         uint16_t color, const SRL::Math::Types::Vector3D& offset,
                         SRL::Math::Types::Fxp scale)
    {
        if (!verts || !faces || vertCount == 0 || faceCount == 0) return;

        // Converte vertices (Fxp 16.16) para POINT (FIXED 16.16 do SGL)
        std::vector<POINT> pnt(vertCount);
        for (size_t i = 0; i < vertCount; ++i)
        {
            auto v = verts[i] * scale + offset;
            pnt[i][0] = (FIXED)v.X.RawValue();
            pnt[i][1] = (FIXED)v.Y.RawValue();
            pnt[i][2] = (FIXED)v.Z.RawValue();
        }

        // Copia faces e aplica atributos simples (flat amarelo)
        std::vector<POLYGON> polys(faceCount);
        std::vector<ATTR>    attrs(faceCount);
        for (size_t f = 0; f < faceCount; ++f)
        {
            const auto& src = faces[f];
            auto& dst = polys[f];
            dst.Vertices[0] = src.Vertices[0];
            dst.Vertices[1] = src.Vertices[1];
            dst.Vertices[2] = src.Vertices[2];
            dst.Vertices[3] = src.Vertices[3];

            ATTR a{};
            a.flag  = Dual_Plane;      // dupla face
            a.sort  = UseLight;        // habilita luz basica
            a.texno = No_Texture;      // sem textura
            a.atrb  = sprPolygon | CL32KRGB; // forca modo RGB 15bpp
            a.colno = color & 0x7FFF;  // cor solida
            a.gstb  = 0;               // sem Gouraud
            a.dir   = UseLight;        // mantem iluminacao
            attrs[f] = a;
        }

        PDATA pdata{
            pnt.data(),
            static_cast<uint32_t>(vertCount),
            polys.data(),
            static_cast<uint32_t>(faceCount),
            attrs.data()
        };

        // Enfileira poligonos diretamente no VDP1 via SGL
        SRL::Debug::Print(1, 29, "VDP1 put mesh faces:%u verts:%u", (unsigned)faceCount, (unsigned)vertCount);
        slPutPolygon(&pdata);
    }
}

