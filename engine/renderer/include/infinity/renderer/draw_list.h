// infinity/renderer/draw_list.h
//
// Draw-list vocabulary (F4.2). A frame is described as a flat array of
// vertices consumed three at a time: every three consecutive vertices form one
// triangle (implicit triangle list, no index buffer yet). Vertex positions are
// screen-space pixel coordinates, origin top-left, +y downward, matching the
// framebuffer row order; the viewport transform from world to screen is the
// caller's job (F4.10 cameras land later). z is reserved for future depth
// testing and ignored by the software backend today.
#pragma once

#include "infinity/renderer/color.h"

#include <cstddef>
#include <span>

namespace infinity::renderer {

// One screen-space vertex: position in pixels plus a linear RGBA color.
struct Vertex {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    Color color{};
};

// A draw command: the vertices span, interpreted as an implicit triangle list.
// The span is borrowed, not copied: the caller keeps the data alive until
// draw() returns (the backend copies what it needs into its own frame buffer).
struct DrawList {
    std::span<const Vertex> vertices;
};

} // namespace infinity::renderer
