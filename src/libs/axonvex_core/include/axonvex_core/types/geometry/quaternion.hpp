#pragma once

#include <cmath>

namespace axonvex::types::geometry {

class Quaternion {
public:
    double w, x, y, z;

    Quaternion() : w(1.0), x(0.0), y(0.0), z(0.0) {}
    Quaternion(double w_, double x_, double y_, double z_) : w(w_), x(x_), y(y_), z(z_) {}

    static Quaternion identity() { return Quaternion(); }

    double norm() const { return std::sqrt(w*w + x*x + y*y + z*z); }

    void normalize() {
        double n = norm();
        if (n > 0.0) { w/=n; x/=n; y/=n; z/=n; }
    }

    Quaternion normalized() const {
        Quaternion q(*this);
        q.normalize();
        return q;
    }

    Quaternion operator*(const Quaternion& other) const {
        return {
            w*other.w - x*other.x - y*other.y - z*other.z,
            w*other.x + x*other.w + y*other.z - z*other.y,
            w*other.y - x*other.z + y*other.w + z*other.x,
            w*other.z + x*other.y - y*other.x + z*other.w
        };
    }

    bool operator==(const Quaternion& o) const {
        return w==o.w && x==o.x && y==o.y && z==o.z;
    }
};

} // namespace axonvex::types::geometry
