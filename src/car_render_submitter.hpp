#pragma once

#include "car_render_contracts.hpp"

// Submitter passivo do render do carro.
// Descreve a futura interface de submissão/telemetria sem integrar ao runtime.

namespace CarRenderDomain
{

inline void SeedSubmitPacket(const CarRenderPacket& renderPacket,
                             const CarShadowPacket& shadowPacket,
                             CarSubmitPacket& outSubmitPacket)
{
    outSubmitPacket.valid = renderPacket.valid;
    outSubmitPacket.renderPosition = renderPacket.renderPosition;
    outSubmitPacket.renderYawDeg = renderPacket.renderYawDeg;
    outSubmitPacket.drawShadowBlob = shadowPacket.drawBlob;
    outSubmitPacket.drawShadowModel = shadowPacket.drawModel;
    outSubmitPacket.shadowPosition = shadowPacket.shadowPosition;
    outSubmitPacket.shadowYawDeg = shadowPacket.shadowYawDeg;
}

inline void MarkSubmitIssued(CarRenderTelemetry& ioTelemetry)
{
    ioTelemetry.submitted = true;
}

inline void RecordRenderedFaceCount(uint16_t renderedFaceCount,
                                    CarRenderTelemetry& ioTelemetry)
{
    ioTelemetry.renderedFaceCount = renderedFaceCount;
}

} // namespace CarRenderDomain
