#pragma once

#include <srl.hpp>
#include "modelObject.hpp"
#include "sgl_poly_renderer.hpp"
#include "track_sgl_renderer.hpp"
#include "track_serialized.hpp"
#include <vector>
#include <cstdint>
#include <algorithm>

struct ModelBounds
{
    SRL::Math::Types::Vector3D min{SRL::Math::Types::Fxp::Convert(32767),
                                   SRL::Math::Types::Fxp::Convert(32767),
                                   SRL::Math::Types::Fxp::Convert(32767)};
    SRL::Math::Types::Vector3D max{SRL::Math::Types::Fxp::Convert(-32768),
                                   SRL::Math::Types::Fxp::Convert(-32768),
                                   SRL::Math::Types::Fxp::Convert(-32768)};
};

class TrackRenderer
{
public:
    struct MemoryStats
    {
        uint32_t bytes = 0;
        uint32_t verts = 0;
        uint32_t faces = 0;
    };

    static constexpr size_t kVDP1FaceCostBytes = 64;
    static constexpr size_t kVDP1BudgetBytes   = 512 * 1024;
    static constexpr size_t kMaxDrawMeshes     = 4;  // renderiza poucos segmentos por padrão

    // Load track file from a list of candidate paths into cart RAM and prepare caches.
    bool Load(const char* const* candidates, size_t count, size_t maxMeshes, bool /*loadAllSegments*/ = false)
    {
        Reset();
        hasTrack_ = false;
        path_ = nullptr;
        startMeshIdx_ = 0;

        // Procura e carrega o primeiro arquivo vÃ¡lido direto na DRAM do cartucho
        for (size_t i = 0; i < count; ++i)
        {
            SRL::Cd::File f(candidates[i]);
            if (!f.Exists() || f.Size.Bytes <= 0) continue;
            path_ = candidates[i];
            trackObj_ = new ModelObject(path_, 0, false, maxMeshes, false, true, false);
            if (trackObj_ && trackObj_->GetMeshCount() > 0)
            {
                hasTrack_ = true;
                break;
            }
            if (trackObj_) { delete trackObj_; trackObj_ = nullptr; }
        }

        if (!hasTrack_ || !trackObj_) return false;
        return InitializeFromModelObject(trackObj_, maxMeshes);
    }

    bool LoadFromSerialized(const TrackSerializedCopy& serialized, size_t maxMeshes)
    {
        Reset();
        if (!serialized.Valid()) return false;
        auto* obj = new ModelObject();
        if (!obj->LoadFromMemory(serialized.cartPtr, serialized.size, 0, false, maxMeshes, false, true))
        {
            delete obj;
            return false;
        }
        if (!InitializeFromModelObject(obj, maxMeshes))
        {
            delete obj;
            trackObj_ = nullptr;
            return false;
        }
        return true;
    }

    bool InitializeFromModelObject(ModelObject* obj, size_t maxMeshes)
    {
        if (!obj) return false;
        trackObj_ = obj;
        isSmooth_ = trackObj_->IsSmooth();
        size_t loadedMeshes = trackObj_->GetMeshCount();
        meshCount_ = (maxMeshes > 0 && maxMeshes < loadedMeshes) ? maxMeshes : loadedMeshes;
        if (startMeshIdx_ >= meshCount_) startMeshIdx_ = 0;
        if (meshCount_ == 0)
        {
            delete trackObj_;
            trackObj_ = nullptr;
            hasTrack_ = false;
            return false;
        }
        faceCount_ = trackObj_->GetFaceCount();
        vertexCount_ = trackObj_->GetVertexCount();
        bounds_ = ComputeBounds(*trackObj_, isSmooth_);

        memStats_.verts = (uint32_t)vertexCount_;
        memStats_.faces = (uint32_t)faceCount_;
        memStats_.bytes = (uint32_t)(vertexCount_ * sizeof(SRL::Math::Types::Vector3D));
        memStats_.bytes += (uint32_t)(faceCount_ * (sizeof(SRL::Types::Polygon) + sizeof(SRL::Types::Attribute)));
        if (isSmooth_) memStats_.bytes += (uint32_t)(vertexCount_ * sizeof(SRL::Math::Types::Vector3D));

        meshCenters_.assign(meshCount_, {});
        meshMap_.assign(meshCount_, 0);
        meshBytes_.assign(meshCount_, 0);

        for (size_t m = 0; m < meshCount_; ++m)
        {
            SRL::Math::Types::Vector3D minv(32767, 32767, 32767);
            SRL::Math::Types::Vector3D maxv(-32768, -32768, -32768);

            if (isSmooth_)
            {
                auto* mesh = trackObj_->GetMesh<SRL::Types::SmoothMesh>(m);
                if (!mesh) continue;
                meshMap_[m] = reinterpret_cast<uintptr_t>(mesh);
                meshBytes_[m] = mesh->VertexCount * sizeof(SRL::Math::Types::Vector3D)
                              + mesh->FaceCount * (sizeof(SRL::Types::Polygon) + sizeof(SRL::Types::Attribute));
                for (size_t v = 0; v < mesh->VertexCount; ++v)
                {
                    const auto& p = mesh->Vertices[v];
                    minv.X = SRL::Math::Min(minv.X, p.X);
                    minv.Y = SRL::Math::Min(minv.Y, p.Y);
                    minv.Z = SRL::Math::Min(minv.Z, p.Z);
                    maxv.X = SRL::Math::Max(maxv.X, p.X);
                    maxv.Y = SRL::Math::Max(maxv.Y, p.Y);
                    maxv.Z = SRL::Math::Max(maxv.Z, p.Z);
                }
            }
            else
            {
                auto* mesh = trackObj_->GetMesh<SRL::Types::Mesh>(m);
                if (!mesh) continue;
                meshMap_[m] = reinterpret_cast<uintptr_t>(mesh);
                meshBytes_[m] = mesh->VertexCount * sizeof(SRL::Math::Types::Vector3D)
                              + mesh->FaceCount * (sizeof(SRL::Types::Polygon) + sizeof(SRL::Types::Attribute));
                for (size_t v = 0; v < mesh->VertexCount; ++v)
                {
                    const auto& p = mesh->Vertices[v];
                    minv.X = SRL::Math::Min(minv.X, p.X);
                    minv.Y = SRL::Math::Min(minv.Y, p.Y);
                    minv.Z = SRL::Math::Min(minv.Z, p.Z);
                    maxv.X = SRL::Math::Max(maxv.X, p.X);
                    maxv.Y = SRL::Math::Max(maxv.Y, p.Y);
                    maxv.Z = SRL::Math::Max(maxv.Z, p.Z);
                }
            }

            meshCenters_[m] = (minv + maxv) / SRL::Math::Types::Fxp::Convert(2);
        }

        if (isSmooth_)
        {
            smoothCache_.assign(meshCount_, {});
        }
        else
        {
            flatCache_.assign(meshCount_, {});
        }

        trackOffset_ = {};

        hasTrack_ = true;
        return true;
    }

    // Draw a limited set of meshes using one of the rendering backends (original, SGL direct or 2D debug).
    void Render(SRL::Math::Types::Vector3D light, const SRL::Math::Types::Vector3D& /*cameraPos*/)
    {
        if (!hasTrack_ || !trackObj_ || meshCount_ == 0) return;
        if (startMeshIdx_ >= meshCount_) startMeshIdx_ = 0;

        SRL::Debug::Print(1, 17, "TR ren offset:%d %d %d start:%u drawLimit:%u",
                          trackOffset_.X.As<int16_t>(), trackOffset_.Y.As<int16_t>(), trackOffset_.Z.As<int16_t>(),
                          (unsigned)startMeshIdx_, (unsigned)drawLimit_);

        SRL::Debug::Print(1, 10, "TR ren begin m:%u start:%u limit:%u off:%d,%d,%d flags SGL:%d orig:%d direct2d:%d",
                          (unsigned)meshCount_, (unsigned)startMeshIdx_, (unsigned)drawLimit_,
                          trackOffset_.X.As<int16_t>(), trackOffset_.Y.As<int16_t>(), trackOffset_.Z.As<int16_t>(),
                          useSglDirect_ ? 1 : 0, useOriginal_ ? 1 : 0, useDirect2D_ ? 1 : 0);

        size_t drawn = 0;
        uint32_t drawnFaces = 0;
        auto lightCopy = light;

        // Se modo direto estiver ativo, desenha um quad de teste 2D para validar VDP1
        if (useDirect2D_)
        {
            SRL::Math::Types::Vector2D ptsTest[4] = {
                SRL::Math::Types::Vector2D(-20, -20),
                SRL::Math::Types::Vector2D( 20, -20),
                SRL::Math::Types::Vector2D( 20,  20),
                SRL::Math::Types::Vector2D(-20,  20)
            };
            SRL::Scene2D::DrawPolygon(ptsTest, true, SRL::Types::HighColor::FromRGB555(31,31,0), 0);
        }

        for (size_t i = startMeshIdx_; i < meshCount_ && drawn < drawLimit_; ++i)
        {
            EnsureCached(i);

            // Loga projeção do primeiro face do primeiro mesh para saber onde cai na tela
            if (drawn == 0)
            {
                if (isSmooth_ && i < smoothCache_.size() && smoothCache_[i].valid && !smoothCache_[i].faces.empty())
                {
                    SRL::Math::Types::Vector2D p2d[3];
                    for (int vi = 0; vi < 3; ++vi)
                    {
                        uint16_t idx = smoothCache_[i].faces[0].Vertices[vi];
                        if (idx >= smoothCache_[i].verts.size()) break;
                        auto v = smoothCache_[i].verts[idx] * trackScale_ + trackOffset_;
                        SRL::Scene3D::ProjectToScreen(v, &p2d[vi]);
                    }
                    SRL::Debug::Print(1, 30, "Proj2D p0:%d,%d p1:%d,%d p2:%d,%d",
                                      p2d[0].X.As<int16_t>(), p2d[0].Y.As<int16_t>(),
                                      p2d[1].X.As<int16_t>(), p2d[1].Y.As<int16_t>(),
                                      p2d[2].X.As<int16_t>(), p2d[2].Y.As<int16_t>());
                }
                else if (!isSmooth_ && i < flatCache_.size() && flatCache_[i].valid && !flatCache_[i].faces.empty())
                {
                    SRL::Math::Types::Vector2D p2d[3];
                    for (int vi = 0; vi < 3; ++vi)
                    {
                        uint16_t idx = flatCache_[i].faces[0].Vertices[vi];
                        if (idx >= flatCache_[i].verts.size()) break;
                        auto v = flatCache_[i].verts[idx] * trackScale_ + trackOffset_;
                        SRL::Scene3D::ProjectToScreen(v, &p2d[vi]);
                    }
                    SRL::Debug::Print(1, 30, "Proj2D p0:%d,%d p1:%d,%d p2:%d,%d",
                                      p2d[0].X.As<int16_t>(), p2d[0].Y.As<int16_t>(),
                                      p2d[1].X.As<int16_t>(), p2d[1].Y.As<int16_t>(),
                                      p2d[2].X.As<int16_t>(), p2d[2].Y.As<int16_t>());
                }
            }

            auto ValidateCache = [&](const auto& cache) {
                if (cache.verts.empty() || cache.faces.empty())
                {
                    SRL::Debug::Print(1, 61, "Track cache empty mesh%zu verts:%zu faces:%zu", (unsigned)i, cache.verts.size(), cache.faces.size());
                    return false;
                }
                for (const auto& face : cache.faces)
                {
                    for (int vi = 0; vi < 4; ++vi)
                    {
                        if (face.Vertices[vi] >= cache.verts.size())
                        {
                            SRL::Debug::Print(1, 60, "Track cache invalid mesh%zu face idx%u vert%u/%zu",
                                              (unsigned)i, (unsigned)&face - (unsigned)cache.faces.data(), (unsigned)face.Vertices[vi], (unsigned)cache.verts.size());
                            return false;
                        }
                    }
                }
                return true;
            };

            auto ValidateModelMesh = [&](const SRL::Types::Mesh* mesh) {
                if (!mesh) return false;
                return mesh->VertexCount > 0 && mesh->FaceCount > 0 && mesh->Vertices != nullptr && mesh->Faces != nullptr;
            };
            auto ValidateModelMeshSmooth = [&](const SRL::Types::SmoothMesh* mesh) {
                if (!mesh) return false;
                return mesh->VertexCount > 0 && mesh->FaceCount > 0 && mesh->Vertices != nullptr && mesh->Faces != nullptr;
            };

            if (useSglDirect_)
            {
                // Usa caminho SGL puro (slDispPolygon)
            if (isSmooth_)
            {
                if (i >= smoothCache_.size() || !smoothCache_[i].valid) continue;
                const auto& cache = smoothCache_[i];
                if (!ValidateCache(cache)) continue;
                if (drawn == 0) SRL::Debug::Print(1, 11, "Track mesh%u faces:%u verts:%u", (unsigned)i, (unsigned)cache.faces.size(), (unsigned)cache.verts.size());
                if (drawn == 0 && !cache.verts.empty())
                {
                    SRL::Math::Types::Vector2D p2d[3];
                    for (int vi = 0; vi < 3; ++vi)
                    {
                        uint16_t idx = cache.faces[0].Vertices[vi];
                        auto v = cache.verts[idx] * trackScale_ + trackOffset_;
                        SRL::Scene3D::ProjectToScreen(v, &p2d[vi]);
                    }
                    SRL::Debug::Print(1, 30, "Proj2D p0:%d,%d p1:%d,%d p2:%d,%d",
                                      p2d[0].X.As<int16_t>(), p2d[0].Y.As<int16_t>(),
                                      p2d[1].X.As<int16_t>(), p2d[1].Y.As<int16_t>(),
                                      p2d[2].X.As<int16_t>(), p2d[2].Y.As<int16_t>());
                }
                    TrackSglRenderer::DrawMesh(cache.verts.data(), cache.verts.size(),
                                                cache.faces.data(), cache.faces.size(),
                                                0x83FF, trackOffset_, trackScale_, drawn == 0);
                drawnFaces += (uint32_t)cache.faces.size();
            }
        else
            {
                if (i >= flatCache_.size() || !flatCache_[i].valid) continue;
                const auto& cache = flatCache_[i];
                if (!ValidateCache(cache)) continue;
                if (drawn == 0) SRL::Debug::Print(1, 11, "Track mesh%u faces:%u verts:%u", (unsigned)i, (unsigned)cache.faces.size(), (unsigned)cache.verts.size());
                if (drawn == 0 && !cache.verts.empty())
                    {
                        SRL::Math::Types::Vector2D p2d[3];
                        for (int vi = 0; vi < 3; ++vi)
                        {
                            uint16_t idx = cache.faces[0].Vertices[vi];
                            auto v = cache.verts[idx] * trackScale_ + trackOffset_;
                            SRL::Scene3D::ProjectToScreen(v, &p2d[vi]);
                        }
                        SRL::Debug::Print(1, 30, "Proj2D p0:%d,%d p1:%d,%d p2:%d,%d",
                            p2d[0].X.As<int16_t>(), p2d[0].Y.As<int16_t>(),
                            p2d[1].X.As<int16_t>(), p2d[1].Y.As<int16_t>(),
                            p2d[2].X.As<int16_t>(), p2d[2].Y.As<int16_t>());
                    }
                    TrackSglRenderer::DrawMesh(cache.verts.data(), cache.verts.size(),
                                                cache.faces.data(), cache.faces.size(),
                                                0x83FF, trackOffset_, trackScale_, drawn == 0);
                    drawnFaces += (uint32_t)cache.faces.size();
                }
                ++drawn;
            }
            else if (useDirect2D_)
            {
                // Desenha faces projetadas diretamente em 2D usando Scene2D::DrawPolygon
                if (isSmooth_)
                {
            if (i >= smoothCache_.size() || !smoothCache_[i].valid) continue;
            const auto& cache = smoothCache_[i];
            if (drawn == 0) SRL::Debug::Print(1, 11, "Track mesh%u faces:%u verts:%u", (unsigned)i, (unsigned)cache.faces.size(), (unsigned)cache.verts.size());
            for (size_t f = 0; f < cache.faces.size(); ++f)
            {
                SRL::Math::Types::Vector2D pts[4];
                const auto& face = cache.faces[f];
                for (int vi = 0; vi < 4; ++vi)
                {
                    uint16_t idx = face.Vertices[vi];
                    if (idx >= cache.verts.size()) goto skip_smooth_face;
                    auto v = cache.verts[idx] * trackScale_ + trackOffset_;
                    SRL::Scene3D::ProjectToScreen(v, &pts[vi]);
                }
                if (drawn == 0 && f == 0)
                {
                    SRL::Debug::Print(1, 30, "Proj2D p0:%d,%d p1:%d,%d p2:%d,%d",
                                      pts[0].X.As<int16_t>(), pts[0].Y.As<int16_t>(),
                                      pts[1].X.As<int16_t>(), pts[1].Y.As<int16_t>(),
                                      pts[2].X.As<int16_t>(), pts[2].Y.As<int16_t>());
                }
                SRL::Scene2D::DrawPolygon(pts, true, SRL::Types::HighColor::FromRGB555(31,31,0), 0);
                ++drawnFaces;
            skip_smooth_face: ;
            }
        }
                else
                {
            if (i >= flatCache_.size() || !flatCache_[i].valid) continue;
            const auto& cache = flatCache_[i];
            if (drawn == 0) SRL::Debug::Print(1, 11, "Track mesh%u faces:%u verts:%u", (unsigned)i, (unsigned)cache.faces.size(), (unsigned)cache.verts.size());
            for (size_t f = 0; f < cache.faces.size(); ++f)
            {
                SRL::Math::Types::Vector2D pts[4];
                const auto& face = cache.faces[f];
                for (int vi = 0; vi < 4; ++vi)
                {
                    uint16_t idx = face.Vertices[vi];
                    if (idx >= cache.verts.size()) goto skip_flat_face;
                    auto v = cache.verts[idx] * trackScale_ + trackOffset_;
                    SRL::Scene3D::ProjectToScreen(v, &pts[vi]);
                }
                if (drawn == 0 && f == 0)
                {
                    SRL::Debug::Print(1, 30, "Proj2D p0:%d,%d p1:%d,%d p2:%d,%d",
                                      pts[0].X.As<int16_t>(), pts[0].Y.As<int16_t>(),
                                      pts[1].X.As<int16_t>(), pts[1].Y.As<int16_t>(),
                                      pts[2].X.As<int16_t>(), pts[2].Y.As<int16_t>());
                }
                SRL::Scene2D::DrawPolygon(pts, true, SRL::Types::HighColor::FromRGB555(31,31,0), 0);
                ++drawnFaces;
            skip_flat_face: ;
            }
        }
                ++drawn;
            }
            else
            {
                // Caminho "original" usando os meshes tal como vieram do ModelObject (sem forçar atributos)
                SRL::Scene3D::PushMatrix();
                SRL::Scene3D::Translate(trackOffset_);
                SRL::Scene3D::Scale(trackScale_);
                if (useOriginal_ && trackObj_)
                {
                    if (isSmooth_)
                    {
                        auto* mesh = trackObj_->GetMesh<SRL::Types::SmoothMesh>(i);
                        if (!ValidateModelMeshSmooth(mesh)) { SRL::Scene3D::PopMatrix(); continue; }
                        if (drawn == 0)
                        {
                            SRL::Debug::Print(1, 11, "Track mesh%u faces:%u verts:%u",
                                              (unsigned)i, (unsigned)mesh->FaceCount, (unsigned)mesh->VertexCount);
                            // Log de projeção do primeiro triângulo
                            if (mesh->FaceCount > 0)
                            {
                                const auto& f0 = mesh->Faces[0];
                                SRL::Debug::Print(1, 31, "Orig attr tex:%d", (mesh->Attributes ? (int)mesh->Attributes[0].Texture : -1));
                                SRL::Math::Types::Vector2D p2d[3];
                                for (int vi = 0; vi < 3; ++vi)
                                {
                                    uint16_t idx = mesh->Faces[0].Vertices[vi];
                                    if (idx >= mesh->VertexCount) break;
                                    auto v = mesh->Vertices[idx] * trackScale_ + trackOffset_;
                                    SRL::Scene3D::ProjectToScreen(v, &p2d[vi]);
                                }
                                SRL::Debug::Print(1, 30, "Proj2D p0:%d,%d p1:%d,%d p2:%d,%d",
                                                  p2d[0].X.As<int16_t>(), p2d[0].Y.As<int16_t>(),
                                                  p2d[1].X.As<int16_t>(), p2d[1].Y.As<int16_t>(),
                                                  p2d[2].X.As<int16_t>(), p2d[2].Y.As<int16_t>());
                                SRL::Debug::Print(1, 32, "Orig v0:%d,%d,%d v1:%d,%d,%d v2:%d,%d,%d",
                                                  mesh->Vertices[f0.Vertices[0]].X.As<int16_t>(), mesh->Vertices[f0.Vertices[0]].Y.As<int16_t>(), mesh->Vertices[f0.Vertices[0]].Z.As<int16_t>(),
                                                  mesh->Vertices[f0.Vertices[1]].X.As<int16_t>(), mesh->Vertices[f0.Vertices[1]].Y.As<int16_t>(), mesh->Vertices[f0.Vertices[1]].Z.As<int16_t>(),
                                                  mesh->Vertices[f0.Vertices[2]].X.As<int16_t>(), mesh->Vertices[f0.Vertices[2]].Y.As<int16_t>(), mesh->Vertices[f0.Vertices[2]].Z.As<int16_t>());
                            }
                        }
                        drawnFaces += (uint32_t)mesh->FaceCount;
                        SRL::Scene3D::DrawSmoothMesh(*mesh, lightCopy);
                    }
                    else
                    {
                        auto* mesh = trackObj_->GetMesh<SRL::Types::Mesh>(i);
                        if (!ValidateModelMesh(mesh)) { SRL::Scene3D::PopMatrix(); continue; }
                        if (drawn == 0)
                        {
                            SRL::Debug::Print(1, 11, "Track mesh%u faces:%u verts:%u",
                                              (unsigned)i, (unsigned)mesh->FaceCount, (unsigned)mesh->VertexCount);
                            if (mesh->FaceCount > 0)
                            {
                                const auto& f0 = mesh->Faces[0];
                                SRL::Debug::Print(1, 31, "Orig attr tex:%d", (mesh->Attributes ? (int)mesh->Attributes[0].Texture : -1));
                                SRL::Math::Types::Vector2D p2d[3];
                                for (int vi = 0; vi < 3; ++vi)
                                {
                                    uint16_t idx = mesh->Faces[0].Vertices[vi];
                                    auto v = mesh->Vertices[idx] * trackScale_ + trackOffset_;
                                    SRL::Scene3D::ProjectToScreen(v, &p2d[vi]);
                                }
                                SRL::Debug::Print(1, 30, "Proj2D p0:%d,%d p1:%d,%d p2:%d,%d",
                                                  p2d[0].X.As<int16_t>(), p2d[0].Y.As<int16_t>(),
                                                  p2d[1].X.As<int16_t>(), p2d[1].Y.As<int16_t>(),
                                                  p2d[2].X.As<int16_t>(), p2d[2].Y.As<int16_t>());
                                SRL::Debug::Print(1, 32, "Orig v0:%d,%d,%d v1:%d,%d,%d v2:%d,%d,%d",
                                                  mesh->Vertices[f0.Vertices[0]].X.As<int16_t>(), mesh->Vertices[f0.Vertices[0]].Y.As<int16_t>(), mesh->Vertices[f0.Vertices[0]].Z.As<int16_t>(),
                                                  mesh->Vertices[f0.Vertices[1]].X.As<int16_t>(), mesh->Vertices[f0.Vertices[1]].Y.As<int16_t>(), mesh->Vertices[f0.Vertices[1]].Z.As<int16_t>(),
                                                  mesh->Vertices[f0.Vertices[2]].X.As<int16_t>(), mesh->Vertices[f0.Vertices[2]].Y.As<int16_t>(), mesh->Vertices[f0.Vertices[2]].Z.As<int16_t>());
                            }
                        }
                        drawnFaces += (uint32_t)mesh->FaceCount;
                        SRL::Scene3D::DrawMesh(*mesh);
                    }
                }
                else
                {
                    if (isSmooth_)
                    {
                        if (i >= smoothCache_.size() || !smoothCache_[i].valid) { SRL::Scene3D::PopMatrix(); continue; }
                        const auto& cache = smoothCache_[i];
                        SRL::Types::SmoothMesh tmp;
                        tmp.Vertices    = const_cast<SRL::Math::Types::Vector3D*>(cache.verts.data());
                        tmp.VertexCount = cache.verts.size();
                        tmp.Faces       = const_cast<SRL::Types::Polygon*>(cache.faces.data());
                        tmp.FaceCount   = cache.faces.size();
                        tmp.Attributes  = const_cast<SRL::Types::Attribute*>(cache.attrs.data());
                        tmp.Normals     = const_cast<SRL::Math::Types::Vector3D*>(cache.normals.data());

                    if (drawn == 0)
                    {
                        SRL::Debug::Print(1, 11, "Track mesh%u faces:%u verts:%u",
                                          (unsigned)i, (unsigned)tmp.FaceCount, (unsigned)tmp.VertexCount);
                        if (!cache.faces.empty())
                        {
                            const auto& f0 = cache.faces[0];
                            SRL::Debug::Print(1, 31, "Cache attr tex:%d", (int)cache.attrs[0].Texture);
                            SRL::Debug::Print(1, 32, "Cache v0:%d,%d,%d v1:%d,%d,%d v2:%d,%d,%d",
                                              cache.verts[f0.Vertices[0]].X.As<int16_t>(), cache.verts[f0.Vertices[0]].Y.As<int16_t>(), cache.verts[f0.Vertices[0]].Z.As<int16_t>(),
                                              cache.verts[f0.Vertices[1]].X.As<int16_t>(), cache.verts[f0.Vertices[1]].Y.As<int16_t>(), cache.verts[f0.Vertices[1]].Z.As<int16_t>(),
                                              cache.verts[f0.Vertices[2]].X.As<int16_t>(), cache.verts[f0.Vertices[2]].Y.As<int16_t>(), cache.verts[f0.Vertices[2]].Z.As<int16_t>());
                        }
                    }
                    drawnFaces += (uint32_t)tmp.FaceCount;
                    SRL::Scene3D::DrawSmoothMesh(tmp, lightCopy);
                }
                else
                    {
                        if (i >= flatCache_.size() || !flatCache_[i].valid) { SRL::Scene3D::PopMatrix(); continue; }
                        const auto& cache = flatCache_[i];
                        SRL::Types::Mesh tmp;
                        tmp.Vertices    = const_cast<SRL::Math::Types::Vector3D*>(cache.verts.data());
                        tmp.VertexCount = cache.verts.size();
                        tmp.Faces       = const_cast<SRL::Types::Polygon*>(cache.faces.data());
                        tmp.FaceCount   = cache.faces.size();
                        tmp.Attributes  = const_cast<SRL::Types::Attribute*>(cache.attrs.data());

                    if (drawn == 0)
                    {
                        SRL::Debug::Print(1, 11, "Track mesh%u faces:%u verts:%u",
                                          (unsigned)i, (unsigned)tmp.FaceCount, (unsigned)tmp.VertexCount);
                        if (!cache.faces.empty())
                        {
                            const auto& f0 = cache.faces[0];
                            SRL::Debug::Print(1, 31, "Cache attr tex:%d", (int)cache.attrs[0].Texture);
                            SRL::Debug::Print(1, 32, "Cache v0:%d,%d,%d v1:%d,%d,%d v2:%d,%d,%d",
                                              cache.verts[f0.Vertices[0]].X.As<int16_t>(), cache.verts[f0.Vertices[0]].Y.As<int16_t>(), cache.verts[f0.Vertices[0]].Z.As<int16_t>(),
                                              cache.verts[f0.Vertices[1]].X.As<int16_t>(), cache.verts[f0.Vertices[1]].Y.As<int16_t>(), cache.verts[f0.Vertices[1]].Z.As<int16_t>(),
                                              cache.verts[f0.Vertices[2]].X.As<int16_t>(), cache.verts[f0.Vertices[2]].Y.As<int16_t>(), cache.verts[f0.Vertices[2]].Z.As<int16_t>());
                        }
                    }
                    drawnFaces += (uint32_t)tmp.FaceCount;
                    SRL::Scene3D::DrawMesh(tmp);
                }
                }
                SRL::Scene3D::PopMatrix();
                ++drawn;
            }
        }

        if (drawn > 0)
        {
            SRL::Debug::Print(1, 22, "Track drawn meshes:%u faces:%u freeHWR:%d",
                              (unsigned)drawn, (unsigned)drawnFaces, SRL::Memory::HighWorkRam::GetFreeSpace());
        }
        else if (useDirect2D_ && drawnFaces == 0)
        {
            // fallback: desenha um quad de teste no centro para validar VDP1
            SRL::Math::Types::Vector2D pts[4] = {
                SRL::Math::Types::Vector2D(-10, -10),
                SRL::Math::Types::Vector2D( 10, -10),
                SRL::Math::Types::Vector2D( 10,  10),
                SRL::Math::Types::Vector2D(-10,  10)
            };
            SRL::Scene2D::DrawPolygon(pts, true, SRL::Types::HighColor::FromRGB555(31,31,0), 0);
            SRL::Debug::Print(1, 22, "Track direct2D fallback quad drawn");
        }
        lastDrawnMeshes_ = (uint32_t)drawn;
        lastDrawnFaces_ = drawnFaces;
    }

    void SetDrawLimit(size_t limit)
    {
        drawLimit_ = (limit == 0) ? 1 : limit;
    }

    size_t DrawLimit() const { return drawLimit_; }

    // Accessors for loaded model information.
    bool HasTrack() const { return hasTrack_; }
    bool IsSmooth() const { return isSmooth_; }
    uint32_t FaceCount() const { return faceCount_; }
    uint32_t VertexCount() const { return vertexCount_; }
    size_t MeshCount() const { return meshCount_; }
    const ModelBounds& Bounds() const { return bounds_; }
    SRL::Math::Types::Vector3D Offset() const { return trackOffset_; }
    void SetOffset(const SRL::Math::Types::Vector3D& offset) { trackOffset_ = offset; }
    MemoryStats MemStats() const { return memStats_; }
    uint32_t LastDrawnFaces() const { return lastDrawnFaces_; }
    uint32_t LastDrawnMeshes() const { return lastDrawnMeshes_; }
    SRL::Math::Types::Vector3D StartMeshCenter() const
    {
        if (meshCenters_.empty()) return SRL::Math::Types::Vector3D(SRL::Math::Types::Fxp::Convert(0),
                                                                    SRL::Math::Types::Fxp::Convert(0),
                                                                    SRL::Math::Types::Fxp::Convert(0));
        size_t idx = startMeshIdx_ < meshCenters_.size() ? startMeshIdx_ : 0;
        return meshCenters_[idx];
    }
    // Rendering configuration helpers.
    const std::vector<size_t>& MeshBytes() const { return meshBytes_; }
    void SetScale(const SRL::Math::Types::Fxp& s) { trackScale_ = s; }
    void SetStartMesh(size_t idx) { startMeshIdx_ = (idx < meshCount_) ? idx : 0; }
    void SetDirect2D(bool v) { useDirect2D_ = v; }
    void SetSglDirect(bool v) { useSglDirect_ = v; }
    void SetUseOriginal(bool v) { useOriginal_ = v; }
    const std::vector<SRL::Math::Types::Vector3D>& MeshCenters() const { return meshCenters_; }
    bool GetMeshStats(size_t idx, uint32_t& faces, uint32_t& verts) const
    {
        if (!trackObj_ || idx >= meshCount_) return false;
        if (isSmooth_)
        {
            auto* mesh = trackObj_->GetMesh<SRL::Types::SmoothMesh>(idx);
            if (!mesh) return false;
            faces = mesh->FaceCount;
            verts = mesh->VertexCount;
            return true;
        }
        else
        {
            auto* mesh = trackObj_->GetMesh<SRL::Types::Mesh>(idx);
            if (!mesh) return false;
            faces = mesh->FaceCount;
            verts = mesh->VertexCount;
            return true;
        }
    }

    ~TrackRenderer()
    {
        Reset();
    }

private:
    // Compute global bounds of all meshes.
    ModelBounds ComputeBounds(ModelObject& model, bool isSmooth)
    {
        ModelBounds bounds{};
        size_t meshCount = model.GetMeshCount();
        auto accumulate = [&](auto* mesh)
        {
            if (!mesh) return;
            for (size_t v = 0; v < mesh->VertexCount; ++v)
            {
                const auto& p = mesh->Vertices[v];
                bounds.min.X = SRL::Math::Min(bounds.min.X, p.X);
                bounds.min.Y = SRL::Math::Min(bounds.min.Y, p.Y);
                bounds.min.Z = SRL::Math::Min(bounds.min.Z, p.Z);
                bounds.max.X = SRL::Math::Max(bounds.max.X, p.X);
                bounds.max.Y = SRL::Math::Max(bounds.max.Y, p.Y);
                bounds.max.Z = SRL::Math::Max(bounds.max.Z, p.Z);
            }
        };

        if (isSmooth)
        {
            for (size_t m = 0; m < meshCount; ++m) accumulate(model.GetMesh<SRL::Types::SmoothMesh>(m));
        }
        else
        {
            for (size_t m = 0; m < meshCount; ++m) accumulate(model.GetMesh<SRL::Types::Mesh>(m));
        }

        return bounds;
    }

    // Release all cached state and delete the loaded model.
    void Reset()
    {
        if (trackObj_) { delete trackObj_; trackObj_ = nullptr; }
        meshCenters_.clear();
        meshBytes_.clear();
        meshMap_.clear();
        meshCount_ = 0;
        faceCount_ = 0;
        vertexCount_ = 0;
        bounds_ = ModelBounds{};
        memStats_ = {};
        trackOffset_ = {};
        hasTrack_ = false;
        path_ = nullptr;
        startMeshIdx_ = 0;
        lastDrawnFaces_ = 0;
        lastDrawnMeshes_ = 0;
        smoothCache_.clear();
        flatCache_.clear();
    }
    // Ensure a mesh has a cached copy with forced attributes for flat lighting.
    void EnsureCached(size_t idx)
    {
        if (isSmooth_)
        {
            if (idx >= smoothCache_.size()) return;
            auto& c = smoothCache_[idx];
            if (c.valid) return;
            auto* mesh = trackObj_ ? trackObj_->GetMesh<SRL::Types::SmoothMesh>(idx) : nullptr;
            if (!mesh) return;
            c.valid = true;
            c.verts.assign(mesh->Vertices, mesh->Vertices + mesh->VertexCount);
            c.faces.assign(mesh->Faces, mesh->Faces + mesh->FaceCount);
            c.attrs.resize(mesh->FaceCount);
            if (mesh->Normals)
                c.normals.assign(mesh->Normals, mesh->Normals + mesh->VertexCount);
            else
                c.normals.assign(mesh->VertexCount, SRL::Math::Types::Vector3D());
            for (size_t f = 0; f < mesh->FaceCount; ++f)
            {
                c.attrs[f] = SRL::Types::Attribute(
                    SRL::Types::Attribute::FaceVisibility::DoubleSided,
                    SRL::Types::Attribute::SortMode::Center,
                    No_Texture,
                    0x83FF,
                    CL32KRGB,
                    CL32KRGB,
                    sprPolygon,
                    UseLight);
            }
            SRL::Debug::Print(1, 50, "Track cache ready smooth mesh%zu verts:%zu faces:%zu lastDrawn:%u", idx, c.verts.size(), c.faces.size(), (unsigned)lastDrawnFaces_);
        }
        else
        {
            if (idx >= flatCache_.size()) return;
            auto& c = flatCache_[idx];
            if (c.valid) return;
            auto* mesh = trackObj_ ? trackObj_->GetMesh<SRL::Types::Mesh>(idx) : nullptr;
            if (!mesh) return;
            c.valid = true;
            c.verts.assign(mesh->Vertices, mesh->Vertices + mesh->VertexCount);
            c.faces.assign(mesh->Faces, mesh->Faces + mesh->FaceCount);
            c.attrs.resize(mesh->FaceCount);
            for (size_t f = 0; f < mesh->FaceCount; ++f)
            {
                c.attrs[f] = SRL::Types::Attribute(
                    SRL::Types::Attribute::FaceVisibility::DoubleSided,
                    SRL::Types::Attribute::SortMode::Center,
                    No_Texture,
                    0x83FF,
                    CL32KRGB,
                    CL32KRGB,
                    sprPolygon,
                    UseLight);
            }
            SRL::Debug::Print(1, 51, "Track cache ready flat mesh%zu verts:%zu faces:%zu lastDrawn:%u", idx, c.verts.size(), c.faces.size(), (unsigned)lastDrawnFaces_);
        }
    }

    bool hasTrack_ = false;
    bool isSmooth_ = false;
    size_t meshCount_ = 0;
    uint32_t faceCount_ = 0;
    uint32_t vertexCount_ = 0;
    SRL::Math::Types::Vector3D trackOffset_;
    ModelBounds bounds_{};
    const char* path_ = nullptr;
    std::vector<SRL::Math::Types::Vector3D> meshCenters_;
    std::vector<uintptr_t> meshMap_;
    std::vector<size_t> meshBytes_;
    MemoryStats memStats_{};
    bool devMode_ = true;
    uint32_t lastDrawnFaces_ = 0;
    uint32_t lastDrawnMeshes_ = 0;
    SRL::Math::Types::Fxp trackScale_{ SRL::Math::Types::Fxp::Convert(1.0f) };
    size_t drawLimit_ = kMaxDrawMeshes;

    ModelObject* trackObj_ = nullptr;
    size_t startMeshIdx_ = 0;
    bool useDirect2D_ = false;
    bool useSglDirect_ = false;
    bool useOriginal_ = true;

    struct SmoothCache {
        bool valid = false;
        std::vector<SRL::Math::Types::Vector3D> verts;
        std::vector<SRL::Types::Polygon> faces;
        std::vector<SRL::Types::Attribute> attrs;
        std::vector<SRL::Math::Types::Vector3D> normals;
    };
    struct FlatCache {
        bool valid = false;
        std::vector<SRL::Math::Types::Vector3D> verts;
        std::vector<SRL::Types::Polygon> faces;
        std::vector<SRL::Types::Attribute> attrs;
    };
    std::vector<SmoothCache> smoothCache_;
    std::vector<FlatCache>   flatCache_;
};
