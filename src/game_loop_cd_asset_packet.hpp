#pragma once

#include "cd_asset_contracts.hpp"

namespace GameLoopRuntime
{

struct CdAssetFramePacket
{
    CdAssetDomain::CdAssetRequestPacket request{};
    CdAssetDomain::CdAssetReadPacket read{};
    CdAssetDomain::CdAssetParsePacket parse{};
    CdAssetDomain::CdAssetTelemetry telemetry{};
    CdAssetDomain::CarAnchorParseResult carAnchors{};

    bool Valid() const
    {
        return request.valid;
    }
};

} // namespace GameLoopRuntime
