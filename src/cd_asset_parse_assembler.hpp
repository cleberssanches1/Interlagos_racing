#pragma once

#include "cd_asset_contracts.hpp"

// Assembler passivo de parse/telemetria do dominio de CD.
// Mantem o recorte fora de `main.cxx` e `game_loop_system.hpp` ate futura integracao.

namespace CdAssetDomain
{

inline void SeedCarAnchorParseResult(const Game::CdAssetSystem::CarAnchorPoints& anchors,
                                     CarAnchorParseResult& outResult)
{
    outResult.anchors = anchors;
    outResult.parseSucceeded = anchors.valid;
}

inline void SeedCdAssetTelemetry(const CdAssetRequestPacket& request,
                                 const CdAssetReadPacket& readPacket,
                                 int32_t resolvedCandidateIndex,
                                 size_t bytesRead,
                                 bool readSucceeded,
                                 bool parseSucceeded,
                                 CdAssetTelemetry& outTelemetry)
{
    outTelemetry.candidateCount = static_cast<uint32_t>(request.candidates.count);
    outTelemetry.chunkBytes = request.chunkBytes;
    outTelemetry.bytesRead = bytesRead;
    outTelemetry.resolvedCandidateIndex = resolvedCandidateIndex;
    outTelemetry.requestValid = request.valid;
    outTelemetry.pathResolved = (readPacket.resolvedPath != nullptr);
    outTelemetry.readSucceeded = readSucceeded;
    outTelemetry.parseSucceeded = parseSucceeded;
}

} // namespace CdAssetDomain
