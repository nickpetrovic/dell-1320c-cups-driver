#pragma once

#include <memory>

namespace dell1320c {

template <auto Release> struct CallRelease {
    template <typename Resource> void operator()(Resource* resource) const noexcept {
        Release(resource);
    }
};

template <typename Resource, auto Release> using UniqueCPointer = std::unique_ptr<Resource, CallRelease<Release>>;

}
