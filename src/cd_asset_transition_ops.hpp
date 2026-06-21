#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "cd_asset_parse_assembler.hpp"
#include "cd_asset_request_assembler.hpp"
#include "cd_asset_system.hpp"

// Operacoes passivas externas para futura reintroducao de `CdAssetSystem`.
// Nao devem ser integradas ao bootstrap critico nesta fase.

namespace CdAssetDomain
{

inline CdAssetCandidateSet BuildCandidateSet(const char* const* paths,
                                             size_t count)
{
    CdAssetCandidateSet set{};
    SeedCandidateSet(paths, count, set);
    return set;
}

inline CdAssetRequestPacket BuildAssetRequest(AssetKind assetKind,
                                              const char* logicalName,
                                              const CdAssetCandidateSet& candidates,
                                              bool requiresFullRead,
                                              bool requiresTextTerminator,
                                              bool requiresLightParse,
                                              uint32_t chunkBytes = 2048u)
{
    CdAssetRequestPacket packet{};
    SeedAssetRequest(assetKind,
                     logicalName,
                     candidates,
                     requiresFullRead,
                     requiresTextTerminator,
                     requiresLightParse,
                     chunkBytes,
                     packet);
    return packet;
}

inline CdAssetReadPacket BuildResolvedReadPacket(const CdAssetRequestPacket& request,
                                                 const char* resolvedPath)
{
    CdAssetReadPacket packet{};
    SeedResolvedReadPacket(request, resolvedPath, packet);
    return packet;
}

inline CdAssetParsePacket BuildParsePacket(const CdAssetRequestPacket& request)
{
    CdAssetParsePacket packet{};
    SeedParsePacket(request, packet);
    return packet;
}

inline CarAnchorParseResult BuildCarAnchorParseResult(
    const Game::CdAssetSystem::CarAnchorPoints& anchors)
{
    CarAnchorParseResult result{};
    SeedCarAnchorParseResult(anchors, result);
    return result;
}

inline CdAssetTelemetry BuildCdAssetTelemetry(const CdAssetRequestPacket& request,
                                              const CdAssetReadPacket& readPacket,
                                              int32_t resolvedCandidateIndex,
                                              size_t bytesRead,
                                              bool readSucceeded,
                                              bool parseSucceeded)
{
    CdAssetTelemetry telemetry{};
    SeedCdAssetTelemetry(request,
                         readPacket,
                         resolvedCandidateIndex,
                         bytesRead,
                         readSucceeded,
                         parseSucceeded,
                         telemetry);
    return telemetry;
}

inline const char* const* SbaShadowModelCandidates(size_t& outCount)
{
    static const char* const kCandidates[] = {
        "CD/DATA/SBA.NYA;1",
        "CD/DATA/SBA.NYA",
        "DATA/SBA.NYA;1",
        "DATA/SBA.NYA",
        "SBA.NYA;1",
        "SBA.NYA",
        "sba.nya;1",
        "sba.nya"
    };
    outCount = sizeof(kCandidates) / sizeof(kCandidates[0]);
    return kCandidates;
}

inline const char* const* CarAnchorCandidates(size_t& outCount)
{
    static const char* const kCandidates[] = {
        "CD/DATA/CAR1_ANCHORS.JSON;1",
        "CD/DATA/CAR1_ANCHORS.JSON",
        "DATA/CAR1_ANCHORS.JSON;1",
        "DATA/CAR1_ANCHORS.JSON",
        "CAR1_ANCHORS.JSON;1",
        "CAR1_ANCHORS.JSON",
        "cd/data/CAR1_ANCHORS.JSON",
        "cd/data/CAR1_ANCHORS.json",
        "data/CAR1_ANCHORS.JSON",
        "data/CAR1_ANCHORS.json",
        "CAR1_ANCHORS.json"
    };
    outCount = sizeof(kCandidates) / sizeof(kCandidates[0]);
    return kCandidates;
}

inline CdAssetRequestPacket BuildSbaShadowModelRequest()
{
    size_t candidateCount = 0u;
    const char* const* candidates = SbaShadowModelCandidates(candidateCount);
    return BuildAssetRequest(AssetKind::SbaShadowModel,
                             "SBA.NYA",
                             BuildCandidateSet(candidates, candidateCount),
                             true,
                             false,
                             false);
}

inline CdAssetRequestPacket BuildCarAnchorRequest()
{
    size_t candidateCount = 0u;
    const char* const* candidates = CarAnchorCandidates(candidateCount);
    return BuildAssetRequest(AssetKind::CarAnchorPoints,
                             "CAR1_ANCHORS.JSON",
                             BuildCandidateSet(candidates, candidateCount),
                             true,
                             true,
                             true);
}

inline const char* ResolveExistingPath(const char* const* paths,
                                       size_t count)
{
    return Game::CdAssetSystem::FindExistingPath(paths, count);
}

inline bool ReadBinaryAsset(const char* path,
                            std::vector<uint8_t>& outBytes)
{
    return Game::CdAssetSystem::ReadBinaryFileSimple(path, outBytes);
}

inline Game::CdAssetSystem::CarAnchorPoints LoadCarAnchorPointsAsset()
{
    return Game::CdAssetSystem::LoadCarAnchorPoints();
}

inline const char* ResolveSbaShadowModelPath()
{
    return Game::CdAssetSystem::FindSbaShadowModelPath();
}

} // namespace CdAssetDomain
