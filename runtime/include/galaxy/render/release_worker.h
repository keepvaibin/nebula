// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// GPU objects the GPU is done with, released on a worker thread. Freeing a GPU allocation goes through the driver and
// the kernel (free its virtual address, destroy the allocation): 1 to 2 ms for an 8 MB upload ring's old buffer or a
// stage's textures, a whole refresh at 240 Hz and most of one at 60 (R-009). The render thread hands over what its
// fence has passed and goes on; the worker releases in the order handed over.

#include <functional>
#include <memory>
#include <utility>

namespace galaxy::render {

// Runs `release` on the release worker, after every release handed over before it.
void release_later(std::function<void()> release);

// Every release handed over so far is done when this returns: before the device or the context the objects belong to
// goes (a renderer's end, a device lost), and from the worker itself it returns at once.
void release_wait() noexcept;

// The container's elements released on the worker; the container is left empty.
template <class Container>
void release_all_later(Container& owned) {
    if (owned.empty()) {
        return;
    }
    auto held = std::make_shared<Container>(std::move(owned));
    owned.clear();
    release_later([held = std::move(held)] { held->clear(); });
}

}  // namespace galaxy::render
