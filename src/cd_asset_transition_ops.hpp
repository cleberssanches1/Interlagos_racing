#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "cd_asset_system.hpp"

// Operacoes passivas externas para futura reintroducao de `CdAssetSystem`.
// Nao devem ser integradas ao bootstrap critico nesta fase.

namespace CdAssetDomain
{

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
