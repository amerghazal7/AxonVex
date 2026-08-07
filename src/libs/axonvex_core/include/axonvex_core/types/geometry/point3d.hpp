#pragma once

#include <cmath>

namespace axonvex::types::geometry {

class Point3D {
  public:
    Point3D() : x_(0.0), y_(0.0), z_(0.0) {}
    Point3D(double x, double y, double z) : x_(x), y_(y), z_(z) {}

    double x() const {
        return x_;
    }
    double y() const {
        return y_;
    }
    double z() const {
        return z_;
    }

    void setX(double x) {
        x_ = x;
    }
    void setY(double y) {
        y_ = y;
    }
    void setZ(double z) {
        z_ = z;
    }

    double distance(const Point3D& other) const {
        double dx = x_ - other.x_;
        double dy = y_ - other.y_;
        double dz = z_ - other.z_;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    Point3D operator+(const Point3D& other) const {
        return {x_ + other.x_, y_ + other.y_, z_ + other.z_};
    }
    Point3D operator-(const Point3D& other) const {
        return {x_ - other.x_, y_ - other.y_, z_ - other.z_};
    }
    Point3D operator*(double s) const {
        return {x_ * s, y_ * s, z_ * s};
    }

  private:
    double x_;
    double y_;
    double z_;
};

} // namespace axonvex::types::geometry
