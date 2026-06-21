#pragma once

#include "cd_asset_transition_ops.hpp"
#include "game_loop_cd_asset_packet.hpp"

namespace GameLoopRuntime
{

inline void SeedCdAssetFramePacket(
    const CdAssetDomain::CdAssetRequestPacket& request,
    const CdAssetDomain::CdAssetReadPacket& read,
    const CdAssetDomain::CdAssetParsePacket& parse,
    const CdAssetDomain::CdAssetTelemetry& telemetry,
    const CdAssetDomain::CarAnchorParseResult& carAnchors,
    CdAssetFramePacket& outPacket)
{
    outPacket.request = request;
    outPacket.read = read;
    outPacket.parse = parse;
    outPacket.telemetry = telemetry;
    outPacket.carAnchors = carAnchors;
}

inline CdAssetFramePacket BuildCdAssetFramePacket(
    const CdAssetDomain::CdAssetRequestPacket& request,
    const CdAssetDomain::CdAssetReadPacket& read,
    const CdAssetDomain::CdAssetParsePacket& parse,
    const CdAssetDomain::CdAssetTelemetry& telemetry,
    const CdAssetDomain::CarAnchorParseResult& carAnchors)
{
    CdAssetFramePacket packet{};
    SeedCdAssetFramePacket(request, read, parse, telemetry, carAnchors, packet);
    return packet;
}

} // namespace GameLoopRuntime
