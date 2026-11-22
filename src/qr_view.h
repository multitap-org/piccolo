#pragma once

// A text as a QR code, drawn with ImGui: dark modules on white with the
// quiet zone scanners want (4 modules), each module a whole number of pixels.
//
// Nothing shows one since pairing went the other way (the client shows a
// code, 2026-11-22). Kept for links to come (Andrés: a QR or a click to join
// a game from the lobby), as piccoletto keeps its link intent.

#include <cstdint>
#include <string>

constexpr int32_t  QR_VIEW_MAX_VERSION = 10;   // 57 x 57 modules: a link of ~150 characters
constexpr uint32_t QR_VIEW_BUFFER = ((QR_VIEW_MAX_VERSION * 4 + 17) * (QR_VIEW_MAX_VERSION * 4 + 17) + 7) / 8 + 1;

struct QrView
{
    std::string text;               // what `modules` encodes ("" = nothing yet)
    bool        ok = false;         // encoded (false: too long for QR_VIEW_MAX_VERSION)
    uint8_t     modules[QR_VIEW_BUFFER] = {};
};

// Encodes `text`, only when it changed. False if it doesn't fit.
bool qr_view_set_text(QrView* view, const char* text);

// At the cursor, the module about a third of the font size (2 px at least).
// Nothing if it isn't encoded.
void qr_view_draw(const QrView* view);
