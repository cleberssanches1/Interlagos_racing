#pragma once

#include "cd_asset_contracts.hpp"

// Assemblers passivos do recorte de assets de CD.
// Nao participam do runtime atual; apenas preparam a futura extracao.

namespace CdAssetDomain
{

inline void SeedCandidateSet(const char* const* paths,
                             size_t count,
                             CdAssetCandidateSet& outSet)
{
    outSet.paths = paths;
    outSet.count = count;
}

inline void SeedAssetRequest(AssetKind assetKind,
                             const char* logicalName,
                             const CdAssetCandidateSet& candidates,
                             bool requiresFullRead,
                             bool requiresTextTerminator,
                             bool requiresLightParse,
                             uint32_t chunkBytes,
                             CdAssetRequestPacket& outPacket)
{
    outPacket.valid = (logicalName != nullptr) && (candidates.paths != nullptr) && (candidates.count > 0u);
    outPacket.assetKind = assetKind;
    outPacket.logicalName = logicalName;
    outPacket.candidates = candidates;
    outPacket.requiresFullRead = requiresFullRead;
    outPacket.requiresTextTerminator = requiresTextTerminator;
    outPacket.requiresLightParse = requiresLightParse;
    outPacket.chunkBytes = (chunkBytes == 0u) ? 2048u : chunkBytes;
}

inline void SeedResolvedReadPacket(const CdAssetRequestPacket& request,
                                   const char* resolvedPath,
                                   CdAssetReadPacket& outPacket)
{
    outPacket.valid = request.valid && (resolvedPath != nullptr);
    outPacket.resolvedPath = resolvedPath;
    outPacket.chunkBytes = request.chunkBytes;
    outPacket.requiresFullRead = request.requiresFullRead;
    outPacket.requiresTextTerminator = request.requiresTextTerminator;
}

inline void SeedParsePacket(const CdAssetRequestPacket& request,
                            CdAssetParsePacket& outPacket)
{
    outPacket.valid = request.valid && request.requiresLightParse;
    outPacket.assetKind = request.assetKind;
    outPacket.parseCarAnchors = (request.assetKind == AssetKind::CarAnchorPoints);
    outPacket.requiresTextTerminator = request.requiresTextTerminator;
}

} // namespace CdAssetDomain
