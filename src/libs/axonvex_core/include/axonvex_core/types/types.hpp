#pragma once

#include <chrono>

// Aggregator for axonvex::types
#include <axonvex_core/types/primitives/uuid.hpp>
#include <axonvex_core/types/geometry/point2d.hpp>
#include <axonvex_core/types/geometry/point3d.hpp>
#include <axonvex_core/types/geometry/quaternion.hpp>

namespace axonvex::types {
    using RealTime = std::chrono::nanoseconds;
    using Duration = std::chrono::microseconds;
    using Frequency = double;
}
