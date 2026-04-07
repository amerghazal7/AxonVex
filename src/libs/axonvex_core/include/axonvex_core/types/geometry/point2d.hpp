#pragma once

#include <cmath>

namespace axonvex::types::geometry {

class Point2D {
public:
    Point2D() : x_(0.0), y_(0.0) {}
    Point2D(double x, double y) : x_(x), y_(y) {}

    double x() const { return x_; }
    double y() const { return y_; }

    void setX(double x) { x_ = x; }
    void setY(double y) { y_ = y; }

    double distance(const Point2D& other) const {
        double dx = x_ - other.x_;
        double dy = y_ - other.y_;
        return std::sqrt(dx*dx + dy*dy);
    }

    Point2D operator+(const Point2D& other) const { return {x_ + other.x_, y_ + other.y_}; }
    Point2D operator-(const Point2D& other) const { return {x_ - other.x_, y_ - other.y_}; }
    Point2D operator*(double s) const { return {x_ * s, y_ * s}; }

private:
    double x_;
    double y_;
};

} // namespace axonvex::types::geometry
