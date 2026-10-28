#pragma once

#include <cstdint>

// Fase 0: count 3D puts. Reset once per displayed frame from RenderFrame.
namespace NpcDrawDiag
{
    inline uint32_t g_carPutCalls = 0u;
    inline uint32_t g_carPutFaces = 0u;
    inline uint32_t g_scene3dPuts = 0u;

    inline void ResetFrame()
    {
        g_carPutCalls = 0u;
        g_carPutFaces = 0u;
        g_scene3dPuts = 0u;
    }

    inline void NoteMeshPut(uint32_t faces)
    {
        ++g_carPutCalls;
        g_carPutFaces += faces;
    }

    inline void NoteScene3dPut()
    {
        ++g_scene3dPuts;
    }
}
