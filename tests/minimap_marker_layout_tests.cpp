#include <cassert>

#include "minimap_marker_layout.hpp"

int main()
{
    using namespace MinimapMarkerLayout;

    Pose markers[3]{};
    markers[0] = {24u, 32u, 0u, true};
    markers[1] = {25u, 32u, 0u, true};
    assert(CoversTile(markers[0], 3u, 4u));
    assert(CoversTile(markers[1], 5u, 6u));
    assert(OwnerForTile(markers, 3u, 3u, 4u) == 0u);
    assert(OwnerForTile(markers, 3u, 5u, 6u) == 0u);

    // Hiding one marker must leave the other marker's shared cells owned.
    markers[0].visible = false;
    assert(OwnerForTile(markers, 3u, 3u, 4u) == 1u);
    markers[1].visible = false;
    assert(OwnerForTile(markers, 3u, 3u, 4u) == 0xffu);

    // At the far map edge, all nine reserved cells stay inside the art.
    markers[2] = {112u, 112u, 0u, true};
    assert(CellX(markers[2]) == 13u);
    assert(CellY(markers[2]) == 13u);
    assert(CoversTile(markers[2], 15u, 15u));
    assert(!CoversTile(markers[2], 12u, 15u));
    assert(!CoversTile(markers[2], 16u, 15u));
    return 0;
}
