#pragma once

#include <chrono>

// Aggregator for axonvex::types
#include <axonvex/types/primitives/uuid.hpp>
#include <axonvex/types/geometry/point2d.hpp>
#include <axonvex/types/geometry/point3d.hpp>
#include <axonvex/types/geometry/quaternion.hpp>

namespace axonvex::types {
    using RealTime = std::chrono::nanoseconds;
    using Duration = std::chrono::microseconds;
    using Frequency = double;
}
