#pragma once

#include <srl.hpp>
#include "modelObject.hpp"
#include "sgl_poly_renderer.hpp"
#include "track_sgl_renderer.hpp"
#include "track_vdp1_renderer.hpp"
#include "track_serialized.hpp"
#include "track_zone_alloc.hpp"
#include <vector>
#include <cstdint>
#include <algorithm>
#include <limits>
#include <type_traits>
#include <utility>

#ifndef TRACK_RENDERER_ENABLE_VERBOSE_LOGS
#define TRACK_RENDERER_ENABLE_VERBOSE_LOGS 0
#endif

#if TRACK_RENDERER_ENABLE_VERBOSE_LOGS
#define TRK_REN_LOG(...) SRL::Debug::Print(__VA_ARGS__)
#else
#define TRK_REN_LOG(...) ((void)0)
#endif

template <typename T>
using TrackRendererLowWorkVector =
    TrackLowWorkVectorBase<T>;

struct ModelBounds
{
    SRL::Math::Types::Vector3D min{SRL::Math::Types::Fxp::BuildRaw(32767 << 16),
                                   SRL::Math::Types::Fxp::BuildRaw(32767 << 16),
                                   SRL::Math::Types::Fxp::BuildRaw(32767 << 16)};
    SRL::Math::Types::Vector3D max{SRL::Math::Types::Fxp::BuildRaw(-32768 << 16),
                                   SRL::Math::Types::Fxp::BuildRaw(-32768 << 16),
                                   SRL::Math::Types::Fxp::BuildRaw(-32768 << 16)};
};

class TrackRenderer
{
public:
    using MeshCenterVector = TrackRendererLowWorkVector<SRL::Math::Types::Vector3D>;
    using MeshMapVector = TrackRendererLowWorkVector<uintptr_t>;
    using MeshByteVector = TrackRendererLowWorkVector<size_t>;
    using ComponentVertVector = TrackRendererLowWorkVector<SRL::Math::Types::Vector3D>;
    using ComponentFaceVector = TrackRendererLowWorkVector<SRL::Types::Polygon>;
    using ComponentAttrVector = TrackRendererLowWorkVector<SRL::Types::Attribute>;

    struct MemoryStats
    {
        uint32_t bytes = 0;
        uint32_t verts = 0;
        uint32_t faces = 0;
    };

    static constexpr size_t kVDP1FaceCostBytes = 64;
    static constexpr size_t kVDP1BudgetBytes   = 512 * 1024;
    static constexpr size_t kMaxDrawMeshes     = 2;  // conservative limit to protect VDP1 command list
    // Keep palette index 0 transparent (SPenb) but disable end-code transparency (ECdis).
    static constexpr uint32_t kTrackTexturedDir =
        static_cast<uint32_t>(sprNoflip) |
        (static_cast<uint32_t>(ECdis) << 24);

    // Load track file from a list of candidate paths into cart RAM and prepare caches.
    bool Load(const char* const* candidates, size_t count, size_t maxMeshes, bool /*loadAllSegments*/ = false)
    {
        Reset();
        hasTrack_ = false;
        path_ = nullptr;
        startMeshIdx_ = 0;

        // Procura e carrega o primeiro arquivo vlido direto na DRAM do cartucho
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

        // SORT_MAX: large asphalt quads that straddle the near plane / leave the
        // camera get a sort key from their farthest vertex, so clipped near verts
        // no longer pull average-Z in front of the car (VDP1 has no Z-buffer).
        trackObj_->ForceSortMode(SRL::Types::Attribute::SortMode::Maximum);
        // Drop real-time gouraud on track meshes so segment visibility changes do
        // not thrash the shared gouraud work table (was re-shading the car).
        trackObj_->ForceStableFlatLightingKeepTextures();
        for (auto& a : componentAttrs_)
        {
            a.Sort = static_cast<uint8_t>(
                (a.Sort & ~static_cast<uint8_t>(0x03)) |
                (static_cast<uint8_t>(SRL::Types::Attribute::SortMode::Maximum) & 0x03u));
            a.Display = static_cast<uint16_t>(a.Display & ~static_cast<uint16_t>(CL_Gouraud));
            a.Gouraud = No_Gouraud;
        }

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

            meshCenters_[m] = (minv + maxv) / SRL::Math::Types::Fxp::BuildRaw(2 << 16);
        }

        if (isSmooth_)
        {
            // Inactive type: just invalidate, keep inner-vector capacity
            for (auto& e : flatCache_) e.valid = false;
            // Active type: grow-only resize (never shrink), then invalidate
            if (meshCount_ > smoothCache_.size()) smoothCache_.resize(meshCount_);
            for (size_t i = 0; i < meshCount_ && i < smoothCache_.size(); ++i)
                smoothCache_[i].valid = false;
        }
        else
        {
            for (auto& e : smoothCache_) e.valid = false;
            if (meshCount_ > flatCache_.size()) flatCache_.resize(meshCount_);
            for (size_t i = 0; i < meshCount_ && i < flatCache_.size(); ++i)
                flatCache_[i].valid = false;
        }

        trackOffset_ = {};

        hasTrack_ = true;
        return true;
    }

    bool InitializeFromComponentData(const std::vector<SRL::Math::Types::Vector3D>& verts,
                                     const std::vector<SRL::Types::Polygon>& faces,
                                     const std::vector<SRL::Types::Attribute>& attrs)
    {
        // Keep compatibility for existing callers and route through move-based path.
        std::vector<SRL::Math::Types::Vector3D> vertsCopy = verts;
        std::vector<SRL::Types::Polygon> facesCopy = faces;
        std::vector<SRL::Types::Attribute> attrsCopy = attrs;
        return InitializeFromComponentData(std::move(vertsCopy), std::move(facesCopy), std::move(attrsCopy));
    }

    bool InitializeFromComponentData(std::vector<SRL::Math::Types::Vector3D>&& verts,
                                     std::vector<SRL::Types::Polygon>&& faces,
                                     std::vector<SRL::Types::Attribute>&& attrs)
    {
        Reset();
        if (verts.empty() || faces.empty() || attrs.size() != faces.size()) return false;

        componentMode_ = true;
        ComponentVertVector vertsMoved(
            std::make_move_iterator(verts.begin()),
            std::make_move_iterator(verts.end()));
        ComponentFaceVector facesMoved(
            std::make_move_iterator(faces.begin()),
            std::make_move_iterator(faces.end()));
        ComponentAttrVector attrsMoved(
            std::make_move_iterator(attrs.begin()),
            std::make_move_iterator(attrs.end()));
        componentVerts_.swap(vertsMoved);
        componentFaces_.swap(facesMoved);
        componentAttrs_.swap(attrsMoved);
        hasTrack_ = true;
        isSmooth_ = false;
        meshCount_ = 1;
        faceCount_ = static_cast<uint32_t>(componentFaces_.size());
        vertexCount_ = static_cast<uint32_t>(componentVerts_.size());
        memStats_.verts = vertexCount_;
        memStats_.faces = faceCount_;
        memStats_.bytes = static_cast<uint32_t>(componentVerts_.size() * sizeof(SRL::Math::Types::Vector3D) +
                                                componentFaces_.size() * sizeof(SRL::Types::Polygon) +
                                                componentAttrs_.size() * sizeof(SRL::Types::Attribute));

        SRL::Math::Types::Vector3D minv(32767, 32767, 32767);
        SRL::Math::Types::Vector3D maxv(-32768, -32768, -32768);
        for (const auto& v : componentVerts_)
        {
            minv.X = SRL::Math::Min(minv.X, v.X);
            minv.Y = SRL::Math::Min(minv.Y, v.Y);
            minv.Z = SRL::Math::Min(minv.Z, v.Z);
            maxv.X = SRL::Math::Max(maxv.X, v.X);
            maxv.Y = SRL::Math::Max(maxv.Y, v.Y);
            maxv.Z = SRL::Math::Max(maxv.Z, v.Z);
        }
        bounds_.min = minv;
        bounds_.max = maxv;
        meshCenters_.assign(1, (minv + maxv) / SRL::Math::Types::Fxp::BuildRaw(2 << 16));
        meshBytes_.assign(1, memStats_.bytes);
        meshMap_.assign(1, 0);
        trackOffset_ = {};
        return true;
    }

    // Rebuild component mesh by transferring caller-owned buffers when possible.
    // For matching std::vector allocators we swap in-place; for other vector
    // types (for example TrackLowWorkVector) we move elements into fresh
    // std::vector storage and clear the source to release scratch RAM.
    template <typename VertVecT, typename FaceVecT, typename AttrVecT>
    bool InitializeFromComponentDataRecycled(VertVecT& verts,
                                             FaceVecT& faces,
                                             AttrVecT& attrs)
    {
        if (verts.empty() || faces.empty() || attrs.size() != faces.size()) return false;

        if (trackObj_)
        {
            delete trackObj_;
            trackObj_ = nullptr;
        }

        componentMode_ = true;
        hasTrack_ = true;
        isSmooth_ = false;
        path_ = nullptr;
        startMeshIdx_ = 0;
        trackOffset_ = {};
        lastDrawnFaces_ = 0;
        lastDrawnMeshes_ = 0;
        smoothCache_.clear();
        flatCache_.clear();

        const bool reuseReservedStorage =
            componentVertCapacityFloor_ > 0u || componentFaceCapacityFloor_ > 0u;

        if (reuseReservedStorage)
        {
            componentVerts_.clear();
            componentFaces_.clear();
            componentAttrs_.clear();
            if (componentVerts_.capacity() < std::max(componentVertCapacityFloor_,
                                                      static_cast<size_t>(verts.size())))
            {
                componentVerts_.reserve(std::max(componentVertCapacityFloor_,
                                                 static_cast<size_t>(verts.size())));
            }
            if (componentFaces_.capacity() < std::max(componentFaceCapacityFloor_,
                                                      static_cast<size_t>(faces.size())))
            {
                componentFaces_.reserve(std::max(componentFaceCapacityFloor_,
                                                 static_cast<size_t>(faces.size())));
            }
            if (componentAttrs_.capacity() < std::max(componentFaceCapacityFloor_,
                                                      static_cast<size_t>(attrs.size())))
            {
                componentAttrs_.reserve(std::max(componentFaceCapacityFloor_,
                                                 static_cast<size_t>(attrs.size())));
            }
            componentVerts_.insert(componentVerts_.end(),
                                   std::make_move_iterator(verts.begin()),
                                   std::make_move_iterator(verts.end()));
            componentFaces_.insert(componentFaces_.end(),
                                   std::make_move_iterator(faces.begin()),
                                   std::make_move_iterator(faces.end()));
            componentAttrs_.insert(componentAttrs_.end(),
                                   std::make_move_iterator(attrs.begin()),
                                   std::make_move_iterator(attrs.end()));
            for (auto& a : componentAttrs_)
            {
                a.Sort = static_cast<uint8_t>(
                    (a.Sort & ~static_cast<uint8_t>(0x03)) |
                    (static_cast<uint8_t>(SRL::Types::Attribute::SortMode::Maximum) & 0x03u));
            }
            verts.clear();
            faces.clear();
            attrs.clear();
            // Preserve caller scratch capacities to avoid per-segment alloc/free
            // churn in the streaming pipeline.
        }
        else
        {
            if constexpr (std::is_same_v<VertVecT, decltype(componentVerts_)>)
            {
                componentVerts_.swap(verts);
            }
            else
            {
                ComponentVertVector vertsMoved(
                    std::make_move_iterator(verts.begin()),
                    std::make_move_iterator(verts.end()));
                componentVerts_.swap(vertsMoved);
                verts.clear();
            }

            if constexpr (std::is_same_v<FaceVecT, decltype(componentFaces_)>)
            {
                componentFaces_.swap(faces);
            }
            else
            {
                ComponentFaceVector facesMoved(
                    std::make_move_iterator(faces.begin()),
                    std::make_move_iterator(faces.end()));
                componentFaces_.swap(facesMoved);
                faces.clear();
            }

            if constexpr (std::is_same_v<AttrVecT, decltype(componentAttrs_)>)
            {
                componentAttrs_.swap(attrs);
            }
            else
            {
                ComponentAttrVector attrsMoved(
                    std::make_move_iterator(attrs.begin()),
                    std::make_move_iterator(attrs.end()));
                componentAttrs_.swap(attrsMoved);
                attrs.clear();
            }
        }

        meshCount_ = 1;
        faceCount_ = static_cast<uint32_t>(componentFaces_.size());
        vertexCount_ = static_cast<uint32_t>(componentVerts_.size());
        memStats_.verts = vertexCount_;
        memStats_.faces = faceCount_;
        memStats_.bytes = static_cast<uint32_t>(componentVerts_.size() * sizeof(SRL::Math::Types::Vector3D) +
                                                componentFaces_.size() * sizeof(SRL::Types::Polygon) +
                                                componentAttrs_.size() * sizeof(SRL::Types::Attribute));

        SRL::Math::Types::Vector3D minv(32767, 32767, 32767);
        SRL::Math::Types::Vector3D maxv(-32768, -32768, -32768);
        for (const auto& v : componentVerts_)
        {
            minv.X = SRL::Math::Min(minv.X, v.X);
            minv.Y = SRL::Math::Min(minv.Y, v.Y);
            minv.Z = SRL::Math::Min(minv.Z, v.Z);
            maxv.X = SRL::Math::Max(maxv.X, v.X);
            maxv.Y = SRL::Math::Max(maxv.Y, v.Y);
            maxv.Z = SRL::Math::Max(maxv.Z, v.Z);
        }
        bounds_.min = minv;
        bounds_.max = maxv;
        meshCenters_.assign(1, (minv + maxv) / SRL::Math::Types::Fxp::BuildRaw(2 << 16));
        meshBytes_.assign(1, memStats_.bytes);
        meshMap_.assign(1, 0);
        return true;
    }

    template <typename VertVecT, typename FaceVecT, typename AttrVecT>
    bool InitializeFromComponentDataCopied(const VertVecT& verts,
                                           const FaceVecT& faces,
                                           const AttrVecT& attrs)
    {
        if (verts.empty() || faces.empty() || attrs.size() != faces.size()) return false;

        if (trackObj_)
        {
            delete trackObj_;
            trackObj_ = nullptr;
        }

        componentMode_ = true;
        hasTrack_ = true;
        isSmooth_ = false;
        path_ = nullptr;
        startMeshIdx_ = 0;
        trackOffset_ = {};
        lastDrawnFaces_ = 0;
        lastDrawnMeshes_ = 0;
        smoothCache_.clear();
        flatCache_.clear();

        const bool reuseReservedStorage =
            componentVertCapacityFloor_ > 0u || componentFaceCapacityFloor_ > 0u;
        if (reuseReservedStorage)
        {
            componentVerts_.clear();
            componentFaces_.clear();
            componentAttrs_.clear();
            if (componentVerts_.capacity() < std::max(componentVertCapacityFloor_,
                                                      static_cast<size_t>(verts.size())))
            {
                componentVerts_.reserve(std::max(componentVertCapacityFloor_,
                                                 static_cast<size_t>(verts.size())));
            }
            if (componentFaces_.capacity() < std::max(componentFaceCapacityFloor_,
                                                      static_cast<size_t>(faces.size())))
            {
                componentFaces_.reserve(std::max(componentFaceCapacityFloor_,
                                                 static_cast<size_t>(faces.size())));
            }
            if (componentAttrs_.capacity() < std::max(componentFaceCapacityFloor_,
                                                      static_cast<size_t>(attrs.size())))
            {
                componentAttrs_.reserve(std::max(componentFaceCapacityFloor_,
                                                 static_cast<size_t>(attrs.size())));
            }
            componentVerts_.insert(componentVerts_.end(), verts.begin(), verts.end());
            componentFaces_.insert(componentFaces_.end(), faces.begin(), faces.end());
            componentAttrs_.insert(componentAttrs_.end(), attrs.begin(), attrs.end());
        }
        else
        {
            ComponentVertVector vertsCopy(verts.begin(), verts.end());
            ComponentFaceVector facesCopy(faces.begin(), faces.end());
            ComponentAttrVector attrsCopy(attrs.begin(), attrs.end());
            componentVerts_.swap(vertsCopy);
            componentFaces_.swap(facesCopy);
            componentAttrs_.swap(attrsCopy);
        }

        meshCount_ = 1;
        faceCount_ = static_cast<uint32_t>(componentFaces_.size());
        vertexCount_ = static_cast<uint32_t>(componentVerts_.size());
        memStats_.verts = vertexCount_;
        memStats_.faces = faceCount_;
        memStats_.bytes = static_cast<uint32_t>(componentVerts_.size() * sizeof(SRL::Math::Types::Vector3D) +
                                                componentFaces_.size() * sizeof(SRL::Types::Polygon) +
                                                componentAttrs_.size() * sizeof(SRL::Types::Attribute));

        SRL::Math::Types::Vector3D minv(32767, 32767, 32767);
        SRL::Math::Types::Vector3D maxv(-32768, -32768, -32768);
        for (const auto& v : componentVerts_)
        {
            minv.X = SRL::Math::Min(minv.X, v.X);
            minv.Y = SRL::Math::Min(minv.Y, v.Y);
            minv.Z = SRL::Math::Min(minv.Z, v.Z);
            maxv.X = SRL::Math::Max(maxv.X, v.X);
            maxv.Y = SRL::Math::Max(maxv.Y, v.Y);
            maxv.Z = SRL::Math::Max(maxv.Z, v.Z);
        }
        bounds_.min = minv;
        bounds_.max = maxv;
        meshCenters_.assign(1, (minv + maxv) / SRL::Math::Types::Fxp::BuildRaw(2 << 16));
        meshBytes_.assign(1, memStats_.bytes);
        meshMap_.assign(1, 0);
        return true;
    }

    // Draw a limited set of meshes using one of the rendering backends (original, SGL direct or 2D debug).
    void Render(SRL::Math::Types::Vector3D light, const SRL::Math::Types::Vector3D& /*cameraPos*/)
    {
        if (!hasTrack_ || meshCount_ == 0) return;
        if (componentMode_)
        {
            if (componentVerts_.empty() || componentFaces_.empty() || componentAttrs_.size() != componentFaces_.size()) return;
            if (useVdp1Commands_)
            {
                const size_t submitted = TrackVdp1Renderer::DrawMesh(componentVerts_.data(),
                                                                     componentVerts_.size(),
                                                                     componentFaces_.data(),
                                                                     componentFaces_.size(),
                                                                     componentAttrs_.data(),
                                                                     trackOffset_,
                                                                     trackScale_,
                                                                     0);
                lastDrawnMeshes_ = (submitted > 0) ? 1u : 0u;
                lastDrawnFaces_ = static_cast<uint32_t>(submitted);
                return;
            }
            if (useSglDirect_)
            {
                TrackSglRenderer::DrawMesh(componentVerts_.data(),
                                           componentVerts_.size(),
                                           componentFaces_.data(),
                                           componentFaces_.size(),
                                           componentAttrs_.data(),
                                           0x83FF,
                                           trackOffset_,
                                           trackScale_,
                                           false);
                lastDrawnMeshes_ = 1;
                lastDrawnFaces_ = static_cast<uint32_t>(componentFaces_.size());
                return;
            }
            SRL::Scene3D::PushMatrix();
            SRL::Scene3D::Translate(trackOffset_);
            SRL::Scene3D::Scale(trackScale_);

            SRL::Types::Mesh tmp{};
            tmp.Vertices = componentVerts_.data();
            tmp.VertexCount = componentVerts_.size();
            tmp.Faces = const_cast<SRL::Types::Polygon*>(componentFaces_.data());
            tmp.FaceCount = componentFaces_.size();
            tmp.Attributes = const_cast<SRL::Types::Attribute*>(componentAttrs_.data());
            if (forceDoubleSided_)
            {
                ComponentAttrVector attrs = componentAttrs_;
                for (auto& a : attrs) a.Visibility = SRL::Types::Attribute::FaceVisibility::DoubleSided;
                tmp.Attributes = attrs.data();
                SRL::Scene3D::DrawMesh(tmp);
                // Prevent the temporary mesh wrapper from deleting vector-owned memory.
                tmp.Vertices = nullptr;
                tmp.Faces = nullptr;
                tmp.Attributes = nullptr;
            }
            else
            {
                SRL::Scene3D::DrawMesh(tmp);
                // Prevent the temporary mesh wrapper from deleting vector-owned memory.
                tmp.Vertices = nullptr;
                tmp.Faces = nullptr;
                tmp.Attributes = nullptr;
            }

            SRL::Scene3D::PopMatrix();
            lastDrawnMeshes_ = 1;
            lastDrawnFaces_ = static_cast<uint32_t>(componentFaces_.size());
            return;
        }
        if (!trackObj_) return;
        if (startMeshIdx_ >= meshCount_) startMeshIdx_ = 0;

        // Debug log disabled to keep overlay clean during texture LOD validation.

        TRK_REN_LOG(1, 10, "TR ren begin m:%u start:%u limit:%u off:%d,%d,%d flags SGL:%d orig:%d direct2d:%d",
                          (unsigned)meshCount_, (unsigned)startMeshIdx_, (unsigned)drawLimit_,
                          trackOffset_.X.As<int16_t>(), trackOffset_.Y.As<int16_t>(), trackOffset_.Z.As<int16_t>(),
                          useSglDirect_ ? 1 : 0, useOriginal_ ? 1 : 0, useDirect2D_ ? 1 : 0);

        size_t drawn = 0;
        uint32_t drawnFaces = 0;
        // Track is drawn flat/unlit — light argument unused (kept for API compatibility).
        (void)light;
        // Cache is only required for custom direct paths.
        const bool needsCache = useSglDirect_ || useDirect2D_ || !useOriginal_;

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
            if (needsCache)
            {
                EnsureCached(i);
            }

            // Loga projeo do primeiro face do primeiro mesh para saber onde cai na tela
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
                    TRK_REN_LOG(1, 30, "Proj2D p0:%d,%d p1:%d,%d p2:%d,%d",
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
                    TRK_REN_LOG(1, 30, "Proj2D p0:%d,%d p1:%d,%d p2:%d,%d",
                                      p2d[0].X.As<int16_t>(), p2d[0].Y.As<int16_t>(),
                                      p2d[1].X.As<int16_t>(), p2d[1].Y.As<int16_t>(),
                                      p2d[2].X.As<int16_t>(), p2d[2].Y.As<int16_t>());
                }
            }

            auto ValidateCache = [&](const auto& cache) {
                if (cache.verts.empty() || cache.faces.empty())
                {
                    TRK_REN_LOG(1, 61, "Track cache empty mesh%lu verts:%lu faces:%lu",
                                      (unsigned long)i,
                                      (unsigned long)cache.verts.size(),
                                      (unsigned long)cache.faces.size());
                    return false;
                }
                for (const auto& face : cache.faces)
                {
                    for (int vi = 0; vi < 4; ++vi)
                    {
                        if (face.Vertices[vi] >= cache.verts.size())
                        {
                            TRK_REN_LOG(1, 60, "Track cache invalid mesh%lu face idx%u vert%u/%lu",
                                              (unsigned long)i,
                                              (unsigned)&face - (unsigned)cache.faces.data(),
                                              (unsigned)face.Vertices[vi],
                                              (unsigned long)cache.verts.size());
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
                if (drawn == 0) TRK_REN_LOG(1, 11, "Track mesh%u faces:%u verts:%u", (unsigned)i, (unsigned)cache.faces.size(), (unsigned)cache.verts.size());
                if (drawn == 0 && !cache.verts.empty())
                {
                    SRL::Math::Types::Vector2D p2d[3];
                    for (int vi = 0; vi < 3; ++vi)
                    {
                        uint16_t idx = cache.faces[0].Vertices[vi];
                        auto v = cache.verts[idx] * trackScale_ + trackOffset_;
                        SRL::Scene3D::ProjectToScreen(v, &p2d[vi]);
                    }
                    TRK_REN_LOG(1, 30, "Proj2D p0:%d,%d p1:%d,%d p2:%d,%d",
                                      p2d[0].X.As<int16_t>(), p2d[0].Y.As<int16_t>(),
                                      p2d[1].X.As<int16_t>(), p2d[1].Y.As<int16_t>(),
                                      p2d[2].X.As<int16_t>(), p2d[2].Y.As<int16_t>());
                }
                    TrackSglRenderer::DrawMesh(cache.verts.data(), cache.verts.size(),
                                                cache.faces.data(), cache.faces.size(),
                                                cache.attrs.data(),
                                                0x83FF, trackOffset_, trackScale_, drawn == 0);
                drawnFaces += (uint32_t)cache.faces.size();
            }
        else
            {
                if (i >= flatCache_.size() || !flatCache_[i].valid) continue;
                const auto& cache = flatCache_[i];
                if (!ValidateCache(cache)) continue;
                if (drawn == 0) TRK_REN_LOG(1, 11, "Track mesh%u faces:%u verts:%u", (unsigned)i, (unsigned)cache.faces.size(), (unsigned)cache.verts.size());
                if (drawn == 0 && !cache.verts.empty())
                    {
                        SRL::Math::Types::Vector2D p2d[3];
                        for (int vi = 0; vi < 3; ++vi)
                        {
                            uint16_t idx = cache.faces[0].Vertices[vi];
                            auto v = cache.verts[idx] * trackScale_ + trackOffset_;
                            SRL::Scene3D::ProjectToScreen(v, &p2d[vi]);
                        }
                        TRK_REN_LOG(1, 30, "Proj2D p0:%d,%d p1:%d,%d p2:%d,%d",
                            p2d[0].X.As<int16_t>(), p2d[0].Y.As<int16_t>(),
                            p2d[1].X.As<int16_t>(), p2d[1].Y.As<int16_t>(),
                            p2d[2].X.As<int16_t>(), p2d[2].Y.As<int16_t>());
                    }
                    TrackSglRenderer::DrawMesh(cache.verts.data(), cache.verts.size(),
                                                cache.faces.data(), cache.faces.size(),
                                                cache.attrs.data(),
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
            if (drawn == 0) TRK_REN_LOG(1, 11, "Track mesh%u faces:%u verts:%u", (unsigned)i, (unsigned)cache.faces.size(), (unsigned)cache.verts.size());
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
                    TRK_REN_LOG(1, 30, "Proj2D p0:%d,%d p1:%d,%d p2:%d,%d",
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
            if (drawn == 0) TRK_REN_LOG(1, 11, "Track mesh%u faces:%u verts:%u", (unsigned)i, (unsigned)cache.faces.size(), (unsigned)cache.verts.size());
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
                    TRK_REN_LOG(1, 30, "Proj2D p0:%d,%d p1:%d,%d p2:%d,%d",
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
                // Caminho "original" usando os meshes tal como vieram do ModelObject (sem forar atributos)
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
                            TRK_REN_LOG(1, 11, "Track mesh%u faces:%u verts:%u",
                                              (unsigned)i, (unsigned)mesh->FaceCount, (unsigned)mesh->VertexCount);
                            // Log de projeo do primeiro tringulo
                            if (mesh->FaceCount > 0)
                            {
                                const auto& f0 = mesh->Faces[0];
                                TRK_REN_LOG(1, 31, "Orig attr tex:%d", (mesh->Attributes ? (int)mesh->Attributes[0].Texture : -1));
                                SRL::Math::Types::Vector2D p2d[3];
                                for (int vi = 0; vi < 3; ++vi)
                                {
                                    uint16_t idx = mesh->Faces[0].Vertices[vi];
                                    if (idx >= mesh->VertexCount) break;
                                    auto v = mesh->Vertices[idx] * trackScale_ + trackOffset_;
                                    SRL::Scene3D::ProjectToScreen(v, &p2d[vi]);
                                }
                                TRK_REN_LOG(1, 30, "Proj2D p0:%d,%d p1:%d,%d p2:%d,%d",
                                                  p2d[0].X.As<int16_t>(), p2d[0].Y.As<int16_t>(),
                                                  p2d[1].X.As<int16_t>(), p2d[1].Y.As<int16_t>(),
                                                  p2d[2].X.As<int16_t>(), p2d[2].Y.As<int16_t>());
                                TRK_REN_LOG(1, 32, "Orig v0:%d,%d,%d v1:%d,%d,%d v2:%d,%d,%d",
                                                  mesh->Vertices[f0.Vertices[0]].X.As<int16_t>(), mesh->Vertices[f0.Vertices[0]].Y.As<int16_t>(), mesh->Vertices[f0.Vertices[0]].Z.As<int16_t>(),
                                                  mesh->Vertices[f0.Vertices[1]].X.As<int16_t>(), mesh->Vertices[f0.Vertices[1]].Y.As<int16_t>(), mesh->Vertices[f0.Vertices[1]].Z.As<int16_t>(),
                                                  mesh->Vertices[f0.Vertices[2]].X.As<int16_t>(), mesh->Vertices[f0.Vertices[2]].Y.As<int16_t>(), mesh->Vertices[f0.Vertices[2]].Z.As<int16_t>());
                            }
                        }
                        drawnFaces += (uint32_t)mesh->FaceCount;
                        if (forceDoubleSided_ && mesh->Attributes)
                        {
                            std::vector<SRL::Types::Attribute> attrs(mesh->Attributes, mesh->Attributes + mesh->FaceCount);
                            for (auto& attr : attrs)
                            {
                                attr.Visibility = SRL::Types::Attribute::FaceVisibility::DoubleSided;
                            }
                            // Flat PDATA path only — never slPutPolygonX (shared gouraud thrash).
                            SRL::Types::Mesh flatTmp{};
                            flatTmp.Vertices = mesh->Vertices;
                            flatTmp.VertexCount = mesh->VertexCount;
                            flatTmp.Faces = mesh->Faces;
                            flatTmp.FaceCount = mesh->FaceCount;
                            flatTmp.Attributes = attrs.data();
                            SRL::Scene3D::DrawMesh(flatTmp);
                            flatTmp.Vertices = nullptr;
                            flatTmp.Faces = nullptr;
                            flatTmp.Attributes = nullptr;
                        }
                        else
                        {
                            SRL::Types::Mesh flatTmp{};
                            flatTmp.Vertices = mesh->Vertices;
                            flatTmp.VertexCount = mesh->VertexCount;
                            flatTmp.Faces = mesh->Faces;
                            flatTmp.FaceCount = mesh->FaceCount;
                            flatTmp.Attributes = mesh->Attributes;
                            SRL::Scene3D::DrawMesh(flatTmp);
                            flatTmp.Vertices = nullptr;
                            flatTmp.Faces = nullptr;
                            flatTmp.Attributes = nullptr;
                        }
                    }
                    else
                    {
                        auto* mesh = trackObj_->GetMesh<SRL::Types::Mesh>(i);
                        if (!ValidateModelMesh(mesh)) { SRL::Scene3D::PopMatrix(); continue; }
                        if (drawn == 0)
                        {
                            TRK_REN_LOG(1, 11, "Track mesh%u faces:%u verts:%u",
                                              (unsigned)i, (unsigned)mesh->FaceCount, (unsigned)mesh->VertexCount);
                            if (mesh->FaceCount > 0)
                            {
                                const auto& f0 = mesh->Faces[0];
                                TRK_REN_LOG(1, 31, "Orig attr tex:%d", (mesh->Attributes ? (int)mesh->Attributes[0].Texture : -1));
                                SRL::Math::Types::Vector2D p2d[3];
                                for (int vi = 0; vi < 3; ++vi)
                                {
                                    uint16_t idx = mesh->Faces[0].Vertices[vi];
                                    auto v = mesh->Vertices[idx] * trackScale_ + trackOffset_;
                                    SRL::Scene3D::ProjectToScreen(v, &p2d[vi]);
                                }
                                TRK_REN_LOG(1, 30, "Proj2D p0:%d,%d p1:%d,%d p2:%d,%d",
                                                  p2d[0].X.As<int16_t>(), p2d[0].Y.As<int16_t>(),
                                                  p2d[1].X.As<int16_t>(), p2d[1].Y.As<int16_t>(),
                                                  p2d[2].X.As<int16_t>(), p2d[2].Y.As<int16_t>());
                                TRK_REN_LOG(1, 32, "Orig v0:%d,%d,%d v1:%d,%d,%d v2:%d,%d,%d",
                                                  mesh->Vertices[f0.Vertices[0]].X.As<int16_t>(), mesh->Vertices[f0.Vertices[0]].Y.As<int16_t>(), mesh->Vertices[f0.Vertices[0]].Z.As<int16_t>(),
                                                  mesh->Vertices[f0.Vertices[1]].X.As<int16_t>(), mesh->Vertices[f0.Vertices[1]].Y.As<int16_t>(), mesh->Vertices[f0.Vertices[1]].Z.As<int16_t>(),
                                                  mesh->Vertices[f0.Vertices[2]].X.As<int16_t>(), mesh->Vertices[f0.Vertices[2]].Y.As<int16_t>(), mesh->Vertices[f0.Vertices[2]].Z.As<int16_t>());
                            }
                        }
                        drawnFaces += (uint32_t)mesh->FaceCount;
                        if (forceDoubleSided_ && mesh->Attributes)
                        {
                            std::vector<SRL::Types::Attribute> attrs(mesh->Attributes, mesh->Attributes + mesh->FaceCount);
                            for (auto& attr : attrs)
                            {
                                attr.Visibility = SRL::Types::Attribute::FaceVisibility::DoubleSided;
                            }
                    SRL::Types::Mesh tmp{};
                            tmp.Vertices = mesh->Vertices;
                            tmp.VertexCount = mesh->VertexCount;
                            tmp.Faces = mesh->Faces;
                            tmp.FaceCount = mesh->FaceCount;
                            tmp.Attributes = attrs.data();
                            SRL::Scene3D::DrawMesh(tmp);
                            // Prevent temporary wrapper from freeing foreign pointers.
                            tmp.Vertices = nullptr;
                            tmp.Faces = nullptr;
                            tmp.Attributes = nullptr;
                        }
                        else
                        {
                            SRL::Scene3D::DrawMesh(*mesh);
                        }
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
                        TRK_REN_LOG(1, 11, "Track mesh%u faces:%u verts:%u",
                                          (unsigned)i, (unsigned)tmp.FaceCount, (unsigned)tmp.VertexCount);
                        if (!cache.faces.empty())
                        {
                            const auto& f0 = cache.faces[0];
                            TRK_REN_LOG(1, 31, "Cache attr tex:%d", (int)cache.attrs[0].Texture);
                            TRK_REN_LOG(1, 32, "Cache v0:%d,%d,%d v1:%d,%d,%d v2:%d,%d,%d",
                                              cache.verts[f0.Vertices[0]].X.As<int16_t>(), cache.verts[f0.Vertices[0]].Y.As<int16_t>(), cache.verts[f0.Vertices[0]].Z.As<int16_t>(),
                                              cache.verts[f0.Vertices[1]].X.As<int16_t>(), cache.verts[f0.Vertices[1]].Y.As<int16_t>(), cache.verts[f0.Vertices[1]].Z.As<int16_t>(),
                                              cache.verts[f0.Vertices[2]].X.As<int16_t>(), cache.verts[f0.Vertices[2]].Y.As<int16_t>(), cache.verts[f0.Vertices[2]].Z.As<int16_t>());
                        }
                    }
                    drawnFaces += (uint32_t)tmp.FaceCount;
                    {
                        // Flat draw of smooth-cache data (skip gouraud light path).
                        SRL::Types::Mesh flatTmp{};
                        flatTmp.Vertices = tmp.Vertices;
                        flatTmp.VertexCount = tmp.VertexCount;
                        flatTmp.Faces = tmp.Faces;
                        flatTmp.FaceCount = tmp.FaceCount;
                        flatTmp.Attributes = tmp.Attributes;
                        SRL::Scene3D::DrawMesh(flatTmp);
                        flatTmp.Vertices = nullptr;
                        flatTmp.Faces = nullptr;
                        flatTmp.Attributes = nullptr;
                    }
                    // Prevent temporary wrapper from freeing vector-owned pointers.
                    tmp.Vertices = nullptr;
                    tmp.Faces = nullptr;
                    tmp.Attributes = nullptr;
                    tmp.Normals = nullptr;
                }
                else
                    {
                        if (i >= flatCache_.size() || !flatCache_[i].valid) { SRL::Scene3D::PopMatrix(); continue; }
                        const auto& cache = flatCache_[i];
                    SRL::Types::Mesh tmp{};
                        tmp.Vertices    = const_cast<SRL::Math::Types::Vector3D*>(cache.verts.data());
                        tmp.VertexCount = cache.verts.size();
                        tmp.Faces       = const_cast<SRL::Types::Polygon*>(cache.faces.data());
                        tmp.FaceCount   = cache.faces.size();
                        tmp.Attributes  = const_cast<SRL::Types::Attribute*>(cache.attrs.data());

                    if (drawn == 0)
                    {
                        TRK_REN_LOG(1, 11, "Track mesh%u faces:%u verts:%u",
                                          (unsigned)i, (unsigned)tmp.FaceCount, (unsigned)tmp.VertexCount);
                        if (!cache.faces.empty())
                        {
                            const auto& f0 = cache.faces[0];
                            TRK_REN_LOG(1, 31, "Cache attr tex:%d", (int)cache.attrs[0].Texture);
                            TRK_REN_LOG(1, 32, "Cache v0:%d,%d,%d v1:%d,%d,%d v2:%d,%d,%d",
                                              cache.verts[f0.Vertices[0]].X.As<int16_t>(), cache.verts[f0.Vertices[0]].Y.As<int16_t>(), cache.verts[f0.Vertices[0]].Z.As<int16_t>(),
                                              cache.verts[f0.Vertices[1]].X.As<int16_t>(), cache.verts[f0.Vertices[1]].Y.As<int16_t>(), cache.verts[f0.Vertices[1]].Z.As<int16_t>(),
                                              cache.verts[f0.Vertices[2]].X.As<int16_t>(), cache.verts[f0.Vertices[2]].Y.As<int16_t>(), cache.verts[f0.Vertices[2]].Z.As<int16_t>());
                        }
                    }
                    drawnFaces += (uint32_t)tmp.FaceCount;
                    SRL::Scene3D::DrawMesh(tmp);
                    // Prevent temporary wrapper from freeing vector-owned pointers.
                    tmp.Vertices = nullptr;
                    tmp.Faces = nullptr;
                    tmp.Attributes = nullptr;
                }
                }
                SRL::Scene3D::PopMatrix();
                ++drawn;
            }
        }

        if (drawn > 0)
        {
            // Keep line 22 reserved for frame telemetry ("TRK seg...").
            // Avoid writing another status string there to prevent alternating debug text.
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
            TRK_REN_LOG(1, 23, "Track direct2D fallback quad drawn");
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
    uint32_t RetainedBytes() const
    {
        uint64_t bytes = 0;
        bytes += CapacityBytes(componentVerts_);
        bytes += CapacityBytes(componentFaces_);
        bytes += CapacityBytes(componentAttrs_);
        bytes += CapacityBytes(meshCenters_);
        bytes += CapacityBytes(meshMap_);
        bytes += CapacityBytes(meshBytes_);
        bytes += CapacityBytes(smoothCache_);
        bytes += CapacityBytes(flatCache_);
        for (const auto& c : smoothCache_)
        {
            bytes += CapacityBytes(c.verts);
            bytes += CapacityBytes(c.faces);
            bytes += CapacityBytes(c.attrs);
            bytes += CapacityBytes(c.normals);
        }
        for (const auto& c : flatCache_)
        {
            bytes += CapacityBytes(c.verts);
            bytes += CapacityBytes(c.faces);
            bytes += CapacityBytes(c.attrs);
        }
        return (bytes > static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()))
            ? std::numeric_limits<uint32_t>::max()
            : static_cast<uint32_t>(bytes);
    }
    uint32_t LastDrawnFaces() const { return lastDrawnFaces_; }
    uint32_t LastDrawnMeshes() const { return lastDrawnMeshes_; }

    void SetRuntimeCapacityFloor(size_t vertexFloor, size_t faceFloor)
    {
        componentVertCapacityFloor_ = vertexFloor;
        componentFaceCapacityFloor_ = faceFloor;
        if (!componentMode_) return;
        if (componentVerts_.capacity() < componentVertCapacityFloor_)
        {
            componentVerts_.reserve(componentVertCapacityFloor_);
        }
        if (componentFaces_.capacity() < componentFaceCapacityFloor_)
        {
            componentFaces_.reserve(componentFaceCapacityFloor_);
        }
        if (componentAttrs_.capacity() < componentFaceCapacityFloor_)
        {
            componentAttrs_.reserve(componentFaceCapacityFloor_);
        }
    }

    bool CompactRuntimeState(bool aggressive = false)
    {
        bool compacted = false;
        compacted |= CompactVectorSlack(componentVerts_,
                                        std::max(componentVerts_.size(), componentVertCapacityFloor_),
                                        aggressive);
        compacted |= CompactVectorSlack(componentFaces_,
                                        std::max(componentFaces_.size(), componentFaceCapacityFloor_),
                                        aggressive);
        compacted |= CompactVectorSlack(componentAttrs_,
                                        std::max(componentAttrs_.size(), componentFaceCapacityFloor_),
                                        aggressive);
        compacted |= CompactVectorSlack(meshCenters_, meshCenters_.size(), aggressive);
        compacted |= CompactVectorSlack(meshMap_, meshMap_.size(), aggressive);
        compacted |= CompactVectorSlack(meshBytes_, meshBytes_.size(), aggressive);

        for (auto& c : smoothCache_)
        {
            compacted |= CompactVectorSlack(c.verts, c.verts.size(), aggressive);
            compacted |= CompactVectorSlack(c.faces, c.faces.size(), aggressive);
            compacted |= CompactVectorSlack(c.attrs, c.attrs.size(), aggressive);
            compacted |= CompactVectorSlack(c.normals, c.normals.size(), aggressive);
        }
        for (auto& c : flatCache_)
        {
            compacted |= CompactVectorSlack(c.verts, c.verts.size(), aggressive);
            compacted |= CompactVectorSlack(c.faces, c.faces.size(), aggressive);
            compacted |= CompactVectorSlack(c.attrs, c.attrs.size(), aggressive);
        }
        compacted |= CompactVectorSlack(smoothCache_, smoothCache_.size(), aggressive);
        compacted |= CompactVectorSlack(flatCache_, flatCache_.size(), aggressive);
        return compacted;
    }

    void ReleaseMeshCaches()
    {
        smoothCache_.clear();
        flatCache_.clear();
    }

    // Export per-face texture slot in global face order (mesh0 face0..N, mesh1...).
    // Untextured faces are returned as -1.
    template <typename SlotContainer>
    void CollectFaceTextureSlotsGlobal(SlotContainer& out) const
    {
        using SlotT = typename SlotContainer::value_type;
        static_assert(std::is_integral_v<SlotT>, "SlotT must be integral");
        out.clear();
        if (componentMode_)
        {
            out.reserve(componentAttrs_.size());
            for (const auto& a : componentAttrs_)
            {
                const uint16_t tex = a.Texture;
                out.push_back((tex == No_Texture) ? static_cast<SlotT>(-1) : static_cast<SlotT>(tex));
            }
            return;
        }
        if (!trackObj_) return;
        out.reserve(faceCount_);

        auto collectMesh = [&](auto* mesh)
        {
            if (!mesh) return;
            if (!mesh->Attributes)
            {
                for (size_t fi = 0; fi < mesh->FaceCount; ++fi) out.push_back(static_cast<SlotT>(-1));
                return;
            }
            for (size_t fi = 0; fi < mesh->FaceCount; ++fi)
            {
                const uint16_t tex = mesh->Attributes[fi].Texture;
                out.push_back((tex == No_Texture) ? static_cast<SlotT>(-1) : static_cast<SlotT>(tex));
            }
        };

        if (isSmooth_)
        {
            for (size_t i = 0; i < meshCount_; ++i) collectMesh(trackObj_->GetMesh<SRL::Types::SmoothMesh>(i));
        }
        else
        {
            for (size_t i = 0; i < meshCount_; ++i) collectMesh(trackObj_->GetMesh<SRL::Types::Mesh>(i));
        }
    }

    // Remap textures by global face order (mesh0 face0..N, mesh1 face0..N...).
    // faceTextureSlots[globalFaceIndex] = VDP1 texture slot to assign.
    template <typename SlotContainer>
    size_t ApplyFaceTextureSlotsGlobal(const SlotContainer& faceTextureSlots)
    {
        using SlotT = typename SlotContainer::value_type;
        static_assert(std::is_integral_v<SlotT>, "SlotT must be integral");
        auto applyAttrTexture = [&](SRL::Types::Attribute& attr, uint16_t slot)
        {
            attr.Texture = slot;
            const uint32_t texturedDir = kTrackTexturedDir;

            const auto& meta = SRL::VDP1::Metadata[slot];
            uint16_t colorMode = CL32KRGB;
            uint16_t palette = No_Palet;
            switch (meta.ColorMode)
            {
            case SRL::CRAM::TextureColorMode::Paletted256:
                colorMode = CL256Bnk;
                palette = static_cast<uint16_t>(meta.PaletteId << 8);
                break;
            case SRL::CRAM::TextureColorMode::Paletted128:
                colorMode = CL128Bnk;
                palette = static_cast<uint16_t>(meta.PaletteId << 7);
                break;
            case SRL::CRAM::TextureColorMode::Paletted64:
                colorMode = CL64Bnk;
                palette = static_cast<uint16_t>(meta.PaletteId << 6);
                break;
            case SRL::CRAM::TextureColorMode::Paletted16:
                colorMode = CL16Bnk;
                palette = static_cast<uint16_t>(meta.PaletteId << 4);
                break;
            default:
                colorMode = CL32KRGB;
                palette = No_Palet;
                break;
            }

            // Force SORT_MAX (bits 0-1): far vertex wins when large asphalt leaves the camera.
            // Strip UseGouraud/UseLight options from sort — track textures are unlit so
            // segment enter/leave does not rewrite the shared gouraud pool the car used to share.
            attr.Sort = static_cast<uint8_t>(
                (static_cast<uint8_t>(SRL::Types::Attribute::SortMode::Maximum) & 0x03u) |
                ((texturedDir >> 16) & 0x1Cu));
            attr.Display = (attr.Display & ~(CL32KRGB | CL16Bnk | CL64Bnk | CL128Bnk | CL256Bnk | CL_Gouraud)) | colorMode;
            attr.Display = static_cast<uint16_t>((attr.Display & ~0x00C0u) | ((texturedDir >> 24) & 0x00C0u));
            attr.Gouraud = No_Gouraud;
            attr.ColorMode = palette;
            attr.Direction = static_cast<uint16_t>(texturedDir & 0x003Fu);
        };

        if (componentMode_)
        {
            size_t applied = 0;
            const size_t n = std::min(componentAttrs_.size(), faceTextureSlots.size());
            for (size_t i = 0; i < n; ++i)
            {
                const int32_t slot = static_cast<int32_t>(faceTextureSlots[i]);
                if (slot < 0) continue;
                if (slot >= static_cast<int32_t>(SRL_MAX_TEXTURES)) continue;
                const uint16_t slotU16 = static_cast<uint16_t>(slot);
                if (SRL::VDP1::Metadata[slotU16].Texture == nullptr) continue;
                applyAttrTexture(componentAttrs_[i], slotU16);
                ++applied;
            }
            return applied;
        }
        if (!trackObj_) return 0;
        size_t applied = 0;
        size_t globalFace = 0;
        constexpr uint16_t kNoTexture = No_Texture;

        auto applyMesh = [&](auto* mesh)
        {
            if (!mesh || !mesh->Attributes) { globalFace += mesh ? mesh->FaceCount : 0; return; }
            for (size_t fi = 0; fi < mesh->FaceCount; ++fi, ++globalFace)
            {
                if (globalFace >= faceTextureSlots.size()) continue;
                const int32_t slot = static_cast<int32_t>(faceTextureSlots[globalFace]);
                if (slot < 0) continue;
                if (slot >= static_cast<int32_t>(SRL_MAX_TEXTURES)) continue;
                const uint16_t slotU16 = static_cast<uint16_t>(slot);
                if (SRL::VDP1::Metadata[slotU16].Texture == nullptr) continue;
                applyAttrTexture(mesh->Attributes[fi], slotU16);
                ++applied;
            }
        };

        if (isSmooth_)
        {
            for (size_t i = 0; i < meshCount_; ++i)
            {
                applyMesh(trackObj_->GetMesh<SRL::Types::SmoothMesh>(i));
            }
        }
        else
        {
            for (size_t i = 0; i < meshCount_; ++i)
            {
                applyMesh(trackObj_->GetMesh<SRL::Types::Mesh>(i));
            }
        }

        // Refresh custom caches only when some face texture actually changed.
        // Invalidate without destroying inner vectors so EnsureCached reuses capacity.
        if (applied > 0)
        {
            if (isSmooth_)
                for (auto& e : smoothCache_) e.valid = false;
            else
                for (auto& e : flatCache_) e.valid = false;
        }

        return applied;
    }

    template <typename SlotContainer, typename PrevSlotContainer>
    size_t ApplyFaceTextureSlotsGlobalChanged(const SlotContainer& faceTextureSlots,
                                              const PrevSlotContainer& previousFaceTextureSlots)
    {
        using SlotT = typename SlotContainer::value_type;
        using PrevSlotT = typename PrevSlotContainer::value_type;
        static_assert(std::is_integral_v<SlotT>, "SlotT must be integral");
        static_assert(std::is_integral_v<PrevSlotT>, "PrevSlotT must be integral");
        if (faceTextureSlots.size() != previousFaceTextureSlots.size())
        {
            return ApplyFaceTextureSlotsGlobal(faceTextureSlots);
        }

        auto applyAttrTexture = [&](SRL::Types::Attribute& attr, uint16_t slot)
        {
            attr.Texture = slot;
            const uint32_t texturedDir = kTrackTexturedDir;

            const auto& meta = SRL::VDP1::Metadata[slot];
            uint16_t colorMode = CL32KRGB;
            uint16_t palette = No_Palet;
            switch (meta.ColorMode)
            {
            case SRL::CRAM::TextureColorMode::Paletted256:
                colorMode = CL256Bnk;
                palette = static_cast<uint16_t>(meta.PaletteId << 8);
                break;
            case SRL::CRAM::TextureColorMode::Paletted128:
                colorMode = CL128Bnk;
                palette = static_cast<uint16_t>(meta.PaletteId << 7);
                break;
            case SRL::CRAM::TextureColorMode::Paletted64:
                colorMode = CL64Bnk;
                palette = static_cast<uint16_t>(meta.PaletteId << 6);
                break;
            case SRL::CRAM::TextureColorMode::Paletted16:
                colorMode = CL16Bnk;
                palette = static_cast<uint16_t>(meta.PaletteId << 4);
                break;
            default:
                colorMode = CL32KRGB;
                palette = No_Palet;
                break;
            }

            // Force SORT_MAX (bits 0-1): far vertex wins when large asphalt leaves the camera.
            // Strip UseGouraud/UseLight options from sort — track textures are unlit so
            // segment enter/leave does not rewrite the shared gouraud pool the car used to share.
            attr.Sort = static_cast<uint8_t>(
                (static_cast<uint8_t>(SRL::Types::Attribute::SortMode::Maximum) & 0x03u) |
                ((texturedDir >> 16) & 0x1Cu));
            attr.Display = (attr.Display & ~(CL32KRGB | CL16Bnk | CL64Bnk | CL128Bnk | CL256Bnk | CL_Gouraud)) | colorMode;
            attr.Display = static_cast<uint16_t>((attr.Display & ~0x00C0u) | ((texturedDir >> 24) & 0x00C0u));
            attr.Gouraud = No_Gouraud;
            attr.ColorMode = palette;
            attr.Direction = static_cast<uint16_t>(texturedDir & 0x003Fu);
        };

        auto attrMatchesTexture = [&](const SRL::Types::Attribute& attr, uint16_t slot) -> bool
        {
            if (attr.Texture != slot) return false;

            const uint32_t texturedDir = kTrackTexturedDir;
            const auto& meta = SRL::VDP1::Metadata[slot];
            uint16_t colorMode = CL32KRGB;
            uint16_t palette = No_Palet;
            switch (meta.ColorMode)
            {
            case SRL::CRAM::TextureColorMode::Paletted256:
                colorMode = CL256Bnk;
                palette = static_cast<uint16_t>(meta.PaletteId << 8);
                break;
            case SRL::CRAM::TextureColorMode::Paletted128:
                colorMode = CL128Bnk;
                palette = static_cast<uint16_t>(meta.PaletteId << 7);
                break;
            case SRL::CRAM::TextureColorMode::Paletted64:
                colorMode = CL64Bnk;
                palette = static_cast<uint16_t>(meta.PaletteId << 6);
                break;
            case SRL::CRAM::TextureColorMode::Paletted16:
                colorMode = CL16Bnk;
                palette = static_cast<uint16_t>(meta.PaletteId << 4);
                break;
            default:
                colorMode = CL32KRGB;
                palette = No_Palet;
                break;
            }

            const uint8_t sortBits = static_cast<uint8_t>((texturedDir >> 16) & 0x1Cu);
            const uint16_t displayBits = static_cast<uint16_t>((texturedDir >> 24) & 0x00C0u);
            const uint16_t maskedDisplay =
                static_cast<uint16_t>(attr.Display & (CL32KRGB | CL16Bnk | CL64Bnk | CL128Bnk | CL256Bnk));
            return maskedDisplay == colorMode &&
                   attr.ColorMode == palette &&
                   static_cast<uint16_t>(attr.Direction & 0x003Fu) == static_cast<uint16_t>(texturedDir & 0x003Fu) &&
                   static_cast<uint8_t>(attr.Sort & 0x1Cu) == sortBits &&
                   static_cast<uint16_t>(attr.Display & 0x00C0u) == displayBits;
        };

        if (componentMode_)
        {
            size_t applied = 0;
            const size_t n = std::min(componentAttrs_.size(), faceTextureSlots.size());
            for (size_t i = 0; i < n; ++i)
            {
                const int32_t slot = static_cast<int32_t>(faceTextureSlots[i]);
                if (slot < 0) continue;
                if (slot >= static_cast<int32_t>(SRL_MAX_TEXTURES)) continue;
                const uint16_t slotU16 = static_cast<uint16_t>(slot);
                if (SRL::VDP1::Metadata[slotU16].Texture == nullptr) continue;
                if (static_cast<int32_t>(faceTextureSlots[i]) == static_cast<int32_t>(previousFaceTextureSlots[i]) &&
                    attrMatchesTexture(componentAttrs_[i], slotU16))
                {
                    continue;
                }
                applyAttrTexture(componentAttrs_[i], slotU16);
                ++applied;
            }
            return applied;
        }
        if (!trackObj_) return 0;
        size_t applied = 0;
        size_t globalFace = 0;

        auto applyMesh = [&](auto* mesh)
        {
            if (!mesh || !mesh->Attributes) { globalFace += mesh ? mesh->FaceCount : 0; return; }
            for (size_t fi = 0; fi < mesh->FaceCount; ++fi, ++globalFace)
            {
                if (globalFace >= faceTextureSlots.size()) continue;
                const int32_t slot = static_cast<int32_t>(faceTextureSlots[globalFace]);
                if (slot < 0) continue;
                if (slot >= static_cast<int32_t>(SRL_MAX_TEXTURES)) continue;
                const uint16_t slotU16 = static_cast<uint16_t>(slot);
                if (SRL::VDP1::Metadata[slotU16].Texture == nullptr) continue;
                if (static_cast<int32_t>(faceTextureSlots[globalFace]) ==
                    static_cast<int32_t>(previousFaceTextureSlots[globalFace]) &&
                    attrMatchesTexture(mesh->Attributes[fi], slotU16))
                {
                    continue;
                }
                applyAttrTexture(mesh->Attributes[fi], slotU16);
                ++applied;
            }
        };

        if (isSmooth_)
        {
            for (size_t i = 0; i < meshCount_; ++i)
            {
                applyMesh(trackObj_->GetMesh<SRL::Types::SmoothMesh>(i));
            }
        }
        else
        {
            for (size_t i = 0; i < meshCount_; ++i)
            {
                applyMesh(trackObj_->GetMesh<SRL::Types::Mesh>(i));
            }
        }

        if (applied > 0)
        {
            if (isSmooth_)
                for (auto& e : smoothCache_) e.valid = false;
            else
                for (auto& e : flatCache_) e.valid = false;
        }

        return applied;
    }

    size_t ForceTextureAll(uint16_t slot)
    {
        size_t applied = 0;
        if (slot == No_Texture) return 0;
        if (slot >= SRL_MAX_TEXTURES) return 0;
        if (SRL::VDP1::Metadata[slot].Texture == nullptr) return 0;

        auto applyAttrTexture = [&](SRL::Types::Attribute& attr, uint16_t actualSlot)
        {
            attr.Texture = actualSlot;
            const uint32_t texturedDir = kTrackTexturedDir;

            const auto& meta = SRL::VDP1::Metadata[actualSlot];
            uint16_t colorMode = CL32KRGB;
            uint16_t palette = No_Palet;
            switch (meta.ColorMode)
            {
            case SRL::CRAM::TextureColorMode::Paletted256:
                colorMode = CL256Bnk;
                palette = static_cast<uint16_t>(meta.PaletteId << 8);
                break;
            case SRL::CRAM::TextureColorMode::Paletted128:
                colorMode = CL128Bnk;
                palette = static_cast<uint16_t>(meta.PaletteId << 7);
                break;
            case SRL::CRAM::TextureColorMode::Paletted64:
                colorMode = CL64Bnk;
                palette = static_cast<uint16_t>(meta.PaletteId << 6);
                break;
            case SRL::CRAM::TextureColorMode::Paletted16:
                colorMode = CL16Bnk;
                palette = static_cast<uint16_t>(meta.PaletteId << 4);
                break;
            default:
                colorMode = CL32KRGB;
                palette = No_Palet;
                break;
            }

            // Force SORT_MAX (bits 0-1): far vertex wins when large asphalt leaves the camera.
            // Strip UseGouraud/UseLight options from sort — track textures are unlit so
            // segment enter/leave does not rewrite the shared gouraud pool the car used to share.
            attr.Sort = static_cast<uint8_t>(
                (static_cast<uint8_t>(SRL::Types::Attribute::SortMode::Maximum) & 0x03u) |
                ((texturedDir >> 16) & 0x1Cu));
            attr.Display = (attr.Display & ~(CL32KRGB | CL16Bnk | CL64Bnk | CL128Bnk | CL256Bnk | CL_Gouraud)) | colorMode;
            attr.Display = static_cast<uint16_t>((attr.Display & ~0x00C0u) | ((texturedDir >> 24) & 0x00C0u));
            attr.Gouraud = No_Gouraud;
            attr.ColorMode = palette;
            attr.Direction = static_cast<uint16_t>(texturedDir & 0x003Fu);
        };

        if (componentMode_)
        {
            for (auto& a : componentAttrs_)
            {
                applyAttrTexture(a, slot);
                ++applied;
            }
            return applied;
        }

        if (!trackObj_) return 0;
        auto applyMesh = [&](auto* mesh)
        {
            if (!mesh || !mesh->Attributes) return;
            for (size_t fi = 0; fi < mesh->FaceCount; ++fi)
            {
                applyAttrTexture(mesh->Attributes[fi], slot);
                ++applied;
            }
        };

        if (isSmooth_)
        {
            for (size_t i = 0; i < meshCount_; ++i) applyMesh(trackObj_->GetMesh<SRL::Types::SmoothMesh>(i));
        }
        else
        {
            for (size_t i = 0; i < meshCount_; ++i) applyMesh(trackObj_->GetMesh<SRL::Types::Mesh>(i));
        }

        if (isSmooth_)
            for (auto& e : smoothCache_) e.valid = false;
        else
            for (auto& e : flatCache_) e.valid = false;

        return applied;
    }
    SRL::Math::Types::Vector3D StartMeshCenter() const
    {
        if (meshCenters_.empty()) return SRL::Math::Types::Vector3D(SRL::Math::Types::Fxp::BuildRaw(0),
                                                                    SRL::Math::Types::Fxp::BuildRaw(0),
                                                                    SRL::Math::Types::Fxp::BuildRaw(0));
        size_t idx = startMeshIdx_ < meshCenters_.size() ? startMeshIdx_ : 0;
        return meshCenters_[idx];
    }
    // Rendering configuration helpers.
    const MeshByteVector& MeshBytes() const { return meshBytes_; }
    void SetScale(const SRL::Math::Types::Fxp& s) { trackScale_ = s; }
    void SetStartMesh(size_t idx) { startMeshIdx_ = (idx < meshCount_) ? idx : 0; }
    void SetDirect2D(bool v) { useDirect2D_ = v; }
    void SetSglDirect(bool v) { useSglDirect_ = v; }
    void SetVdp1Commands(bool v) { useVdp1Commands_ = v; }
    void SetUseOriginal(bool v) { useOriginal_ = v; }
    void SetForceDoubleSided(bool v) { forceDoubleSided_ = v; }
    const MeshCenterVector& MeshCenters() const { return meshCenters_; }
    bool GetMeshStats(size_t idx, uint32_t& faces, uint32_t& verts) const
    {
        if (componentMode_)
        {
            if (idx != 0) return false;
            faces = static_cast<uint32_t>(componentFaces_.size());
            verts = static_cast<uint32_t>(componentVerts_.size());
            return true;
        }
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

    // Expose component-mode geometry for runtime systems that need direct
    // spatial queries (for example, placing gameplay objects on road faces).
    bool GetComponentGeometry(const SRL::Math::Types::Vector3D*& outVerts,
                              size_t& outVertCount,
                              const SRL::Types::Polygon*& outFaces,
                              size_t& outFaceCount) const
    {
        outVerts = nullptr;
        outVertCount = 0u;
        outFaces = nullptr;
        outFaceCount = 0u;
        if (!componentMode_) return false;
        if (componentVerts_.empty() || componentFaces_.empty()) return false;
        outVerts = componentVerts_.data();
        outVertCount = componentVerts_.size();
        outFaces = componentFaces_.data();
        outFaceCount = componentFaces_.size();
        return true;
    }

    // Release CPU-side buffers so streamed windows do not accumulate peak sizes.
    // compact=false (default): keeps all vector capacities intact for immediate reuse.
    // compact=true: aggressively shrinks all vectors after a permanent release.
    void RecycleRuntimeState(bool compact = false)
    {
        if (trackObj_) { delete trackObj_; trackObj_ = nullptr; }
        componentMode_ = false;
        componentVerts_.clear();
        componentFaces_.clear();
        componentAttrs_.clear();
        meshCenters_.clear();
        meshBytes_.clear();
        meshMap_.clear();
        if (componentVertCapacityFloor_ > 0u && componentVerts_.capacity() < componentVertCapacityFloor_)
        {
            componentVerts_.reserve(componentVertCapacityFloor_);
        }
        if (componentFaceCapacityFloor_ > 0u)
        {
            if (componentFaces_.capacity() < componentFaceCapacityFloor_)
            {
                componentFaces_.reserve(componentFaceCapacityFloor_);
            }
            if (componentAttrs_.capacity() < componentFaceCapacityFloor_)
            {
                componentAttrs_.reserve(componentFaceCapacityFloor_);
            }
        }
        // If component vectors grew far beyond the floor (outlier segment), compact
        // them back to the floor now. This is a one-time cost paid when the outlier
        // segment is evicted; normal segments stay at or below 2× floor and skip this.
        if (componentFaceCapacityFloor_ > 0u)
        {
            const size_t faceFloor = static_cast<size_t>(componentFaceCapacityFloor_);
            if (componentFaces_.capacity() > faceFloor * 2u)
            {
                { ComponentFaceVector tmp; tmp.reserve(faceFloor); componentFaces_.swap(tmp); }
                { ComponentAttrVector tmp; tmp.reserve(faceFloor); componentAttrs_.swap(tmp); }
            }
        }
        if (componentVertCapacityFloor_ > 0u)
        {
            const size_t vertFloor = static_cast<size_t>(componentVertCapacityFloor_);
            if (componentVerts_.capacity() > vertFloor * 2u)
            {
                ComponentVertVector tmp; tmp.reserve(vertFloor); componentVerts_.swap(tmp);
            }
        }
        if (meshCenters_.capacity() < 1u) meshCenters_.reserve(1u);
        if (meshBytes_.capacity() < 1u) meshBytes_.reserve(1u);
        if (meshMap_.capacity() < 1u) meshMap_.reserve(1u);
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
        // Invalidate without destroying inner vectors — EnsureCached will
        // refill in-place reusing existing capacity, avoiding TLSF churn.
        for (auto& e : smoothCache_) e.valid = false;
        for (auto& e : flatCache_) e.valid = false;
        // Only compact when explicitly requested: avoids free+realloc on
        // persistent scratch renderers that will be refilled immediately.
        if (compact)
            (void)CompactRuntimeState(true);
    }

    ~TrackRenderer()
    {
        Reset();
    }

private:
    template <typename VecT>
    static bool CompactVectorSlack(VecT& v, size_t keepCapacityElements, bool aggressive)
    {
        using T = typename VecT::value_type;
        const size_t desired = std::max(keepCapacityElements, v.size());
        if (v.capacity() <= desired) return false;

        const size_t slackElements = v.capacity() - desired;
        const size_t slackBytes = slackElements * sizeof(T);
        if (!aggressive)
        {
            if (v.capacity() <= (desired * 2u + 8u)) return false;
            if (slackBytes < (2u * 1024u)) return false;
        }

        VecT compact{};
        compact.reserve(desired);
        compact.insert(compact.end(), v.begin(), v.end());
        v.swap(compact);
        return true;
    }

    template <typename VecT>
    static uint64_t CapacityBytes(const VecT& v)
    {
        using T = typename VecT::value_type;
        return static_cast<uint64_t>(v.capacity()) * static_cast<uint64_t>(sizeof(T));
    }

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
        componentMode_ = false;
        componentVertCapacityFloor_ = 0u;
        componentFaceCapacityFloor_ = 0u;
        decltype(componentVerts_)().swap(componentVerts_);
        decltype(componentFaces_)().swap(componentFaces_);
        decltype(componentAttrs_)().swap(componentAttrs_);
        decltype(meshCenters_)().swap(meshCenters_);
        decltype(meshBytes_)().swap(meshBytes_);
        decltype(meshMap_)().swap(meshMap_);
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
            TRK_REN_LOG(1, 50, "Track cache ready smooth mesh%lu verts:%lu faces:%lu lastDrawn:%u",
                              (unsigned long)idx,
                              (unsigned long)c.verts.size(),
                              (unsigned long)c.faces.size(),
                              (unsigned)lastDrawnFaces_);
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
            TRK_REN_LOG(1, 51, "Track cache ready flat mesh%lu verts:%lu faces:%lu lastDrawn:%u",
                              (unsigned long)idx,
                              (unsigned long)c.verts.size(),
                              (unsigned long)c.faces.size(),
                              (unsigned)lastDrawnFaces_);
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
    MeshCenterVector meshCenters_;
    MeshMapVector meshMap_;
    MeshByteVector meshBytes_;
    MemoryStats memStats_{};
    bool devMode_ = true;
    uint32_t lastDrawnFaces_ = 0;
    uint32_t lastDrawnMeshes_ = 0;
    SRL::Math::Types::Fxp trackScale_{ SRL::Math::Types::Fxp::BuildRaw(1 << 16) };
    size_t drawLimit_ = kMaxDrawMeshes;

    ModelObject* trackObj_ = nullptr;
    size_t startMeshIdx_ = 0;
    bool useDirect2D_ = false;
    bool useSglDirect_ = false;
    bool useVdp1Commands_ = false;
    bool useOriginal_ = true;
    bool forceDoubleSided_ = false;
    bool componentMode_ = false;
    size_t componentVertCapacityFloor_ = 0u;
    size_t componentFaceCapacityFloor_ = 0u;
    ComponentVertVector componentVerts_{};
    ComponentFaceVector componentFaces_{};
    ComponentAttrVector componentAttrs_{};

    struct SmoothCache {
        bool valid = false;
        TrackRendererLowWorkVector<SRL::Math::Types::Vector3D> verts{};
        TrackRendererLowWorkVector<SRL::Types::Polygon> faces{};
        TrackRendererLowWorkVector<SRL::Types::Attribute> attrs{};
        TrackRendererLowWorkVector<SRL::Math::Types::Vector3D> normals{};
    };
    struct FlatCache {
        bool valid = false;
        TrackRendererLowWorkVector<SRL::Math::Types::Vector3D> verts{};
        TrackRendererLowWorkVector<SRL::Types::Polygon> faces{};
        TrackRendererLowWorkVector<SRL::Types::Attribute> attrs{};
    };
    TrackRendererLowWorkVector<SmoothCache> smoothCache_{};
    TrackRendererLowWorkVector<FlatCache>   flatCache_{};
};
