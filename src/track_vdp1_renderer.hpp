#pragma once

#include <srl.hpp>
#include <sgl.h>
#include <cstdint>
#include <vector>

// Minimal VDP1 command builder for track faces.
// This does not write to the hardware command table yet.
// It builds a hardware-oriented packet from a face, ready for a future writer.
namespace TrackVdp1Renderer
{
    struct ScreenPoint
    {
        int16_t x = 0;
        int16_t y = 0;
    };

    enum class CommandType : uint16_t
    {
        TexturedQuad = 0x0002,
        FlatPolygon  = 0x0004
    };

    struct PreparedCommand
    {
        CommandType type = CommandType::FlatPolygon;
        bool transparentPixels = false;

        uint16_t textureSlot = No_Texture;
        uint16_t textureAddr = 0;
        uint16_t textureSize = 0;
        uint16_t colorWord = 0;

        ScreenPoint points[4]{};
    };

    inline uint16_t ResolveColorWordFromTexture(uint16_t textureSlot)
    {
        if (textureSlot == No_Texture || textureSlot >= SRL_MAX_TEXTURES)
        {
            return 0;
        }

        const auto& meta = SRL::VDP1::Metadata[textureSlot];
        switch (meta.ColorMode)
        {
        case SRL::CRAM::TextureColorMode::Paletted256:
            return static_cast<uint16_t>(meta.PaletteId << 8);
        case SRL::CRAM::TextureColorMode::Paletted128:
            return static_cast<uint16_t>(meta.PaletteId << 7);
        case SRL::CRAM::TextureColorMode::Paletted64:
            return static_cast<uint16_t>(meta.PaletteId << 6);
        case SRL::CRAM::TextureColorMode::Paletted16:
            return static_cast<uint16_t>(meta.PaletteId << 4);
        default:
            return 0;
        }
    }

    inline bool TryBuildTexturedQuadCommandFromProjected(const ScreenPoint (&pts)[4],
                                                         uint16_t textureSlot,
                                                         bool transparentPixels,
                                                         PreparedCommand& out)
    {
        if (textureSlot == No_Texture || textureSlot >= SRL_MAX_TEXTURES)
        {
            return false;
        }

        const auto& meta = SRL::VDP1::Metadata[textureSlot];
        if (!meta.Texture)
        {
            return false;
        }

        out = {};
        out.type = CommandType::TexturedQuad;
        out.transparentPixels = transparentPixels;
        out.textureSlot = textureSlot;
        out.textureAddr = meta.Texture->Address;
        out.textureSize = meta.Texture->Size;
        out.colorWord = ResolveColorWordFromTexture(textureSlot);
        for (size_t i = 0; i < 4; ++i)
        {
            out.points[i] = pts[i];
        }

        return true;
    }

    inline void BuildFlatQuadCommandFromProjected(const ScreenPoint (&pts)[4],
                                                  uint16_t colorWord,
                                                  PreparedCommand& out)
    {
        out = {};
        out.type = CommandType::FlatPolygon;
        out.transparentPixels = false;
        out.textureSlot = No_Texture;
        out.textureAddr = 0;
        out.textureSize = 0;
        out.colorWord = colorWord;
        for (size_t i = 0; i < 4; ++i)
        {
            out.points[i] = pts[i];
        }
    }

    inline bool TryProjectFaceToScreen(const SRL::Math::Types::Vector3D* verts,
                                       size_t vertCount,
                                       const SRL::Types::Polygon& face,
                                       const SRL::Math::Types::Vector3D& offset,
                                       SRL::Math::Types::Fxp scale,
                                       ScreenPoint (&outPts)[4])
    {
        if (!verts || vertCount == 0)
        {
            return false;
        }

        for (size_t i = 0; i < 4; ++i)
        {
            const uint16_t idx = face.Vertices[i];
            if (idx >= vertCount)
            {
                return false;
            }

            const auto world = verts[idx] * scale + offset;
            SRL::Math::Types::Vector2D p2d{};
            SRL::Scene3D::ProjectToScreen(world, &p2d);
            outPts[i].x = p2d.X.As<int16_t>();
            outPts[i].y = p2d.Y.As<int16_t>();
        }

        return true;
    }

    inline bool TryBuildCommand(const SRL::Math::Types::Vector3D* verts,
                                size_t vertCount,
                                const SRL::Types::Polygon& face,
                                const SRL::Types::Attribute& attr,
                                const SRL::Math::Types::Vector3D& offset,
                                SRL::Math::Types::Fxp scale,
                                PreparedCommand& out)
    {
        ScreenPoint pts[4]{};
        if (!TryProjectFaceToScreen(verts, vertCount, face, offset, scale, pts))
        {
            return false;
        }

        const bool hasTexture =
            attr.Texture != No_Texture &&
            attr.Texture < SRL_MAX_TEXTURES &&
            SRL::VDP1::Metadata[attr.Texture].Texture != nullptr;

        if (hasTexture)
        {
            return TryBuildTexturedQuadCommandFromProjected(pts, attr.Texture, true, out);
        }

        BuildFlatQuadCommandFromProjected(pts, attr.ColorMode, out);
        return true;
    }

    // Build hardware-oriented commands for a whole mesh.
    // This is the bridge between the current mesh path and a future VDP1 writer.
    inline size_t BuildCommandsForMesh(const SRL::Math::Types::Vector3D* verts,
                                       size_t vertCount,
                                       const SRL::Types::Polygon* faces,
                                       size_t faceCount,
                                       const SRL::Types::Attribute* attrs,
                                       const SRL::Math::Types::Vector3D& offset,
                                       SRL::Math::Types::Fxp scale,
                                       std::vector<PreparedCommand>& out)
    {
        out.clear();
        if (!verts || !faces || !attrs || vertCount == 0 || faceCount == 0)
        {
            return 0;
        }

        out.reserve(faceCount);
        for (size_t i = 0; i < faceCount; ++i)
        {
            PreparedCommand cmd{};
            if (!TryBuildCommand(verts, vertCount, faces[i], attrs[i], offset, scale, cmd))
            {
                continue;
            }
            out.push_back(cmd);
        }

        return out.size();
    }

    // Convert a prepared flat command into a direct SGL sprite command.
    inline void FillSprite(const PreparedCommand& src, SPRITE& out)
    {
        out = {};
        out.CTRL = FUNC_Polygon;
        out.LINK = 0;
        out.PMOD = static_cast<uint16_t>(0x0080 | ((CL32KRGB & 7) << 3));
        out.COLR = src.colorWord;
        out.SRCA = 0;
        out.SIZE = 0;
        out.XA = src.points[0].x;
        out.YA = src.points[0].y;
        out.XB = src.points[1].x;
        out.YB = src.points[1].y;
        out.XC = src.points[2].x;
        out.YC = src.points[2].y;
        out.XD = src.points[3].x;
        out.YD = src.points[3].y;
        out.GRDA = 0;
        out.DMMY = 0;
    }

    inline bool DrawTexturedCommand(const PreparedCommand& src, FIXED z)
    {
        SRL::Math::Types::Vector2D points[4] = {
            SRL::Math::Types::Vector2D(src.points[0].x, src.points[0].y),
            SRL::Math::Types::Vector2D(src.points[1].x, src.points[1].y),
            SRL::Math::Types::Vector2D(src.points[2].x, src.points[2].y),
            SRL::Math::Types::Vector2D(src.points[3].x, src.points[3].y)
        };

        return SRL::Scene2D::DrawSprite(src.textureSlot, points, SRL::Math::Types::Fxp::BuildRaw(z));
    }

    inline bool DrawFlatCommand(const PreparedCommand& src, FIXED z)
    {
        SPRITE spr{};
        FillSprite(src, spr);
        return slSetSprite(&spr, z) != 0;
    }

    // Submit a prepared command list using one sprite command per face.
    inline size_t DrawCommands(const std::vector<PreparedCommand>& commands, FIXED z = 0)
    {
        size_t submitted = 0;
        for (size_t i = 0; i < commands.size(); ++i)
        {
            const bool ok =
                (commands[i].type == CommandType::TexturedQuad)
                    ? DrawTexturedCommand(commands[i], z)
                    : DrawFlatCommand(commands[i], z);
            if (ok)
            {
                ++submitted;
            }
        }
        return submitted;
    }

    // Build and submit direct VDP1 sprite commands for a whole mesh.
    inline size_t DrawMesh(const SRL::Math::Types::Vector3D* verts,
                           size_t vertCount,
                           const SRL::Types::Polygon* faces,
                           size_t faceCount,
                           const SRL::Types::Attribute* attrs,
                           const SRL::Math::Types::Vector3D& offset,
                           SRL::Math::Types::Fxp scale,
                           FIXED z = 0)
    {
        std::vector<PreparedCommand> commands{};
        BuildCommandsForMesh(verts, vertCount, faces, faceCount, attrs, offset, scale, commands);
        return DrawCommands(commands, z);
    }
}
