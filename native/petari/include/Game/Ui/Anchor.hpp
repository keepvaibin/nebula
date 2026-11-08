#pragma once

#include "Game/Ui/UiTypes.hpp"

namespace Ui {
    class Pane;

    // How an anchored pane follows a canvas other than the authored 608x456 (Layout::SetAnchors). Offsets move the
    // pane's branch on screen; scales and extends change the pane's own screen matrix (its children keep the
    // unscaled one); sizes give the pane a draw size (its authored or animated size stays as it is).
    enum AnchorFit {
        AnchorLeft = 1 << 0,  // the branch moves with the left edge
        AnchorRight = 1 << 1,
        AnchorTop = 1 << 2,
        AnchorBottom = 1 << 3,
        AnchorWidth = 1 << 4,         // the picture scales to the canvas width
        AnchorHeight = 1 << 5,        // the picture scales to the canvas height
        AnchorCover = 1 << 6,         // artwork scales uniformly until it covers the canvas
        AnchorFrame = 1 << 7,         // a window's draw size grows so its border keeps its distance from the edge
        AnchorExtendUp = 1 << 8,      // a bar grows upward to the canvas top
        AnchorExtendDown = 1 << 9,    // a bar grows downward to the canvas bottom
        AnchorSlide = 1 << 10,        // slides further in proportion; mWidth = its 16:9 off-screen x
        AnchorFlipSlide = 1 << 11,    // the pane's animated vertical slide runs the other way, at every canvas
        AnchorFill = 1 << 12,         // a full-screen backing: its draw size is the canvas (wide canvas only)
        AnchorCinemaUpper = 1 << 13,  // a cinema bar at the canvas top (wide canvas only)
        AnchorCinemaLower = 1 << 14,  // a cinema bar at the canvas bottom (wide canvas only)
    };

    struct Anchor {
        Pane* mpPane;
        u32 mFit;
        f32 mWidth;   // AnchorSlide: the off-screen x; AnchorFrame: the authored window width
        f32 mHeight;  // AnchorFrame: the authored window height
    };

    // The canvas a layout is placed on this pass.
    struct Canvas {
        f32 mWidth;
        f32 mHeight;
        f32 mHorizontalDelta;  // how much further each side edge lies than at the authored width
        f32 mVerticalDelta;
        f32 mScaleX;  // mWidth over the authored width
        f32 mScaleY;
        bool mWide;  // a 16:9 picture on a wide display: the backings and cinema bars follow it
    };
}  // namespace Ui
