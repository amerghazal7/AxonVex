#include <axonvex/types/geometry/geometry.hpp>

namespace axonvex::types::geometry {

// Point2D scalar multiplication (friend function)
Point2D operator*(double scalar, const Point2D& point) {
    return point * scalar;
}

// Point3D scalar multiplication (friend function)
Point3D operator*(double scalar, const Point3D& point) {
    return point * scalar;
}

// Additional utility functions for geometry

/**
 * @brief Calculate angle between two 2D vectors
 */
double angleBetween(const Point2D& a, const Point2D& b) {
    double dot = a.dot(b);
    double mag = a.magnitude() * b.magnitude();
    if (mag > 0.0) {
        return std::acos(std::clamp(dot / mag, -1.0, 1.0));
    }
    return 0.0;
}

/**
 * @brief Calculate angle between two 3D vectors
 */
double angleBetween(const Point3D& a, const Point3D& b) {
    double dot = a.dot(b);
    double mag = a.magnitude() * b.magnitude();
    if (mag > 0.0) {
        return std::acos(std::clamp(dot / mag, -1.0, 1.0));
    }
    return 0.0;
}

/**
 * @brief Linear interpolation between two 2D points
 */
Point2D lerp(const Point2D& a, const Point2D& b, double t) {
    return a + (b - a) * t;
}

/**
 * @brief Linear interpolation between two 3D points
 */
Point3D lerp(const Point3D& a, const Point3D& b, double t) {
    return a + (b - a) * t;
}

/**
 * @brief Spherical linear interpolation between two quaternions
 */
Quaternion slerp(const Quaternion& q1, const Quaternion& q2, double t) {
    double dot = q1.w() * q2.w() + q1.x() * q2.x() + q1.y() * q2.y() + q1.z() * q2.z();
    
    // If dot product is negative, negate one quaternion to take shorter path
    Quaternion q2_corrected = q2;
    if (dot < 0.0) {
        q2_corrected = Quaternion(-q2.w(), -q2.x(), -q2.y(), -q2.z());
        dot = -dot;
    }
    
    const double DOT_THRESHOLD = 0.9995;
    if (dot > DOT_THRESHOLD) {
        // Use linear interpolation for very close quaternions
        Quaternion result(
            q1.w() + t * (q2_corrected.w() - q1.w()),
            q1.x() + t * (q2_corrected.x() - q1.x()),
            q1.y() + t * (q2_corrected.y() - q1.y()),
            q1.z() + t * (q2_corrected.z() - q1.z())
        );
        return result.normalize();
    }
    
    // Use spherical interpolation
    double theta = std::acos(std::abs(dot));
    double sinTheta = std::sin(theta);
    double w1 = std::sin((1.0 - t) * theta) / sinTheta;
    double w2 = std::sin(t * theta) / sinTheta;
    
    return Quaternion(
        w1 * q1.w() + w2 * q2_corrected.w(),
        w1 * q1.x() + w2 * q2_corrected.x(),
        w1 * q1.y() + w2 * q2_corrected.y(),
        w1 * q1.z() + w2 * q2_corrected.z()
    );
}

/**
 * @brief Check if a point is inside a triangle (2D)
 */
bool pointInTriangle(const Point2D& p, const Point2D& a, const Point2D& b, const Point2D& c) {
    // Barycentric coordinate method
    Point2D v0 = c - a;
    Point2D v1 = b - a;
    Point2D v2 = p - a;
    
    double dot00 = v0.dot(v0);
    double dot01 = v0.dot(v1);
    double dot02 = v0.dot(v2);
    double dot11 = v1.dot(v1);
    double dot12 = v1.dot(v2);
    
    double invDenom = 1.0 / (dot00 * dot11 - dot01 * dot01);
    double u = (dot11 * dot02 - dot01 * dot12) * invDenom;
    double v = (dot00 * dot12 - dot01 * dot02) * invDenom;
    
    return (u >= 0) && (v >= 0) && (u + v <= 1);
}

/**
 * @brief Calculate distance from point to line segment (2D)
 */
double distanceToLineSegment(const Point2D& point, const Point2D& lineStart, const Point2D& lineEnd) {
    Point2D lineVec = lineEnd - lineStart;
    Point2D pointVec = point - lineStart;
    
    double lineLength = lineVec.magnitude();
    if (lineLength < 1e-9) {
        return point.distance(lineStart);
    }
    
    double t = pointVec.dot(lineVec) / (lineLength * lineLength);
    t = std::clamp(t, 0.0, 1.0);
    
    Point2D projection = lineStart + lineVec * t;
    return point.distance(projection);
}

/**
 * @brief Calculate distance from point to plane (3D)
 */
double distanceToPlane(const Point3D& point, const Point3D& planePoint, const Point3D& planeNormal) {
    Point3D normalizedNormal = planeNormal.normalize();
    Point3D pointToPlane = point - planePoint;
    return std::abs(pointToPlane.dot(normalizedNormal));
}

} // namespace axonvex::types::geometry