#pragma once

#include <cstddef>
#include <cstdint>

#include "cd_asset_system.hpp"

namespace CdAssetDomain
{

enum class Stage : uint8_t
{
    RequestAssembly = 0,
    CandidateResolve,
    ReadAssembly,
    ParseAssembly,
    TelemetryAssembly
};

enum class AssetKind : uint8_t
{
    GenericBinary = 0,
    GenericText,
    CarAnchorPoints,
    SbaShadowModel,
    AutoLapGuide
};

struct CdAssetCandidateSet
{
    const char* const* paths = nullptr;
    size_t count = 0u;
};

struct CdAssetRequestPacket
{
    bool valid = false;
    AssetKind assetKind = AssetKind::GenericBinary;
    const char* logicalName = nullptr;
    CdAssetCandidateSet candidates{};
    bool requiresFullRead = true;
    bool requiresTextTerminator = false;
    bool requiresLightParse = false;
    uint32_t chunkBytes = 2048u;
};

struct CdAssetReadPacket
{
    bool valid = false;
    const char* resolvedPath = nullptr;
    uint32_t chunkBytes = 2048u;
    bool requiresFullRead = true;
    bool requiresTextTerminator = false;
};

struct CdAssetParsePacket
{
    bool valid = false;
    AssetKind assetKind = AssetKind::GenericBinary;
    bool parseCarAnchors = false;
    bool requiresTextTerminator = false;
};

struct CdAssetTelemetry
{
    uint32_t candidateCount = 0u;
    uint32_t chunkBytes = 0u;
    size_t bytesRead = 0u;
    int32_t resolvedCandidateIndex = -1;
    bool requestValid = false;
    bool pathResolved = false;
    bool readSucceeded = false;
    bool parseSucceeded = false;
};

struct CarAnchorParseResult
{
    Game::CdAssetSystem::CarAnchorPoints anchors{};
    bool parseSucceeded = false;
};

} // namespace CdAssetDomain
