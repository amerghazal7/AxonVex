#pragma once

#include <cmath>
#include <array>
#include <vector>
#include <algorithm>
#include <numeric>

namespace axonvex::types::geometry {

/**
 * @brief 2D point with mathematical operations
 */
class Point2D {
public:
    Point2D() : x_(0.0), y_(0.0) {}
    Point2D(double x, double y) : x_(x), y_(y) {}
    
    // Accessors
    double x() const { return x_; }
    double y() const { return y_; }
    void setX(double x) { x_ = x; }
    void setY(double y) { y_ = y; }
    void set(double x, double y) { x_ = x; y_ = y; }
    
    // Mathematical operations
    double distance(const Point2D& other) const {
        double dx = x_ - other.x_;
        double dy = y_ - other.y_;
        return std::sqrt(dx * dx + dy * dy);
    }
    
    double distanceSquared(const Point2D& other) const {
        double dx = x_ - other.x_;
        double dy = y_ - other.y_;
        return dx * dx + dy * dy;
    }
    
    double magnitude() const {
        return std::sqrt(x_ * x_ + y_ * y_);
    }
    
    Point2D normalize() const {
        double mag = magnitude();
        return (mag > 0.0) ? Point2D(x_ / mag, y_ / mag) : Point2D();
    }
    
    double dot(const Point2D& other) const {
        return x_ * other.x_ + y_ * other.y_;
    }
    
    double cross(const Point2D& other) const {
        return x_ * other.y_ - y_ * other.x_;
    }
    
    // Operators
    Point2D operator+(const Point2D& other) const {
        return Point2D(x_ + other.x_, y_ + other.y_);
    }
    
    Point2D operator-(const Point2D& other) const {
        return Point2D(x_ - other.x_, y_ - other.y_);
    }
    
    Point2D operator*(double scalar) const {
        return Point2D(x_ * scalar, y_ * scalar);
    }
    
    Point2D operator/(double scalar) const {
        return Point2D(x_ / scalar, y_ / scalar);
    }
    
    Point2D& operator+=(const Point2D& other) {
        x_ += other.x_;
        y_ += other.y_;
        return *this;
    }
    
    Point2D& operator-=(const Point2D& other) {
        x_ -= other.x_;
        y_ -= other.y_;
        return *this;
    }
    
    Point2D& operator*=(double scalar) {
        x_ *= scalar;
        y_ *= scalar;
        return *this;
    }
    
    bool operator==(const Point2D& other) const {
        return std::abs(x_ - other.x_) < 1e-9 && std::abs(y_ - other.y_) < 1e-9;
    }
    
    bool operator!=(const Point2D& other) const {
        return !(*this == other);
    }

private:
    double x_, y_;
};

/**
 * @brief 3D point with mathematical operations
 */
class Point3D {
public:
    Point3D() : x_(0.0), y_(0.0), z_(0.0) {}
    Point3D(double x, double y, double z) : x_(x), y_(y), z_(z) {}
    
    // Accessors
    double x() const { return x_; }
    double y() const { return y_; }
    double z() const { return z_; }
    void setX(double x) { x_ = x; }
    void setY(double y) { y_ = y; }
    void setZ(double z) { z_ = z; }
    void set(double x, double y, double z) { x_ = x; y_ = y; z_ = z; }
    
    // Mathematical operations
    double distance(const Point3D& other) const {
        double dx = x_ - other.x_;
        double dy = y_ - other.y_;
        double dz = z_ - other.z_;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }
    
    double distanceSquared(const Point3D& other) const {
        double dx = x_ - other.x_;
        double dy = y_ - other.y_;
        double dz = z_ - other.z_;
        return dx * dx + dy * dy + dz * dz;
    }
    
    double magnitude() const {
        return std::sqrt(x_ * x_ + y_ * y_ + z_ * z_);
    }
    
    Point3D normalize() const {
        double mag = magnitude();
        return (mag > 0.0) ? Point3D(x_ / mag, y_ / mag, z_ / mag) : Point3D();
    }
    
    double dot(const Point3D& other) const {
        return x_ * other.x_ + y_ * other.y_ + z_ * other.z_;
    }
    
    Point3D cross(const Point3D& other) const {
        return Point3D(
            y_ * other.z_ - z_ * other.y_,
            z_ * other.x_ - x_ * other.z_,
            x_ * other.y_ - y_ * other.x_
        );
    }
    
    // Operators
    Point3D operator+(const Point3D& other) const {
        return Point3D(x_ + other.x_, y_ + other.y_, z_ + other.z_);
    }
    
    Point3D operator-(const Point3D& other) const {
        return Point3D(x_ - other.x_, y_ - other.y_, z_ - other.z_);
    }
    
    Point3D operator*(double scalar) const {
        return Point3D(x_ * scalar, y_ * scalar, z_ * scalar);
    }
    
    Point3D operator/(double scalar) const {
        return Point3D(x_ / scalar, y_ / scalar, z_ / scalar);
    }
    
    Point3D& operator+=(const Point3D& other) {
        x_ += other.x_;
        y_ += other.y_;
        z_ += other.z_;
        return *this;
    }
    
    Point3D& operator-=(const Point3D& other) {
        x_ -= other.x_;
        y_ -= other.y_;
        z_ -= other.z_;
        return *this;
    }
    
    Point3D& operator*=(double scalar) {
        x_ *= scalar;
        y_ *= scalar;
        z_ *= scalar;
        return *this;
    }
    
    bool operator==(const Point3D& other) const {
        return std::abs(x_ - other.x_) < 1e-9 && 
               std::abs(y_ - other.y_) < 1e-9 && 
               std::abs(z_ - other.z_) < 1e-9;
    }
    
    bool operator!=(const Point3D& other) const {
        return !(*this == other);
    }

private:
    double x_, y_, z_;
};

/**
 * @brief Quaternion for 3D rotations
 */
class Quaternion {
public:
    Quaternion() : w_(1.0), x_(0.0), y_(0.0), z_(0.0) {}
    Quaternion(double w, double x, double y, double z) : w_(w), x_(x), y_(y), z_(z) {}
    
    // Accessors
    double w() const { return w_; }
    double x() const { return x_; }
    double y() const { return y_; }
    double z() const { return z_; }
    
    void set(double w, double x, double y, double z) {
        w_ = w; x_ = x; y_ = y; z_ = z;
    }
    
    // Factory methods
    static Quaternion identity() {
        return Quaternion(1.0, 0.0, 0.0, 0.0);
    }
    
    static Quaternion fromEuler(double roll, double pitch, double yaw) {
        double cr = std::cos(roll * 0.5);
        double sr = std::sin(roll * 0.5);
        double cp = std::cos(pitch * 0.5);
        double sp = std::sin(pitch * 0.5);
        double cy = std::cos(yaw * 0.5);
        double sy = std::sin(yaw * 0.5);
        
        return Quaternion(
            cr * cp * cy + sr * sp * sy,
            sr * cp * cy - cr * sp * sy,
            cr * sp * cy + sr * cp * sy,
            cr * cp * sy - sr * sp * cy
        );
    }
    
    static Quaternion fromAxisAngle(const Point3D& axis, double angle) {
        Point3D normalizedAxis = axis.normalize();
        double halfAngle = angle * 0.5;
        double sinHalf = std::sin(halfAngle);
        
        return Quaternion(
            std::cos(halfAngle),
            normalizedAxis.x() * sinHalf,
            normalizedAxis.y() * sinHalf,
            normalizedAxis.z() * sinHalf
        );
    }
    
    // Mathematical operations
    double norm() const {
        return std::sqrt(w_ * w_ + x_ * x_ + y_ * y_ + z_ * z_);
    }
    
    Quaternion normalize() const {
        double n = norm();
        return (n > 0.0) ? Quaternion(w_ / n, x_ / n, y_ / n, z_ / n) : identity();
    }
    
    Quaternion conjugate() const {
        return Quaternion(w_, -x_, -y_, -z_);
    }
    
    Quaternion inverse() const {
        double normSquared = w_ * w_ + x_ * x_ + y_ * y_ + z_ * z_;
        if (normSquared > 0.0) {
            Quaternion conj = conjugate();
            return Quaternion(conj.w_ / normSquared, conj.x_ / normSquared, 
                            conj.y_ / normSquared, conj.z_ / normSquared);
        }
        return identity();
    }
    
    // Quaternion multiplication
    Quaternion operator*(const Quaternion& other) const {
        return Quaternion(
            w_ * other.w_ - x_ * other.x_ - y_ * other.y_ - z_ * other.z_,
            w_ * other.x_ + x_ * other.w_ + y_ * other.z_ - z_ * other.y_,
            w_ * other.y_ - x_ * other.z_ + y_ * other.w_ + z_ * other.x_,
            w_ * other.z_ + x_ * other.y_ - y_ * other.x_ + z_ * other.w_
        );
    }
    
    // Rotate a point
    Point3D rotate(const Point3D& point) const {
        Quaternion p(0, point.x(), point.y(), point.z());
        Quaternion result = (*this) * p * conjugate();
        return Point3D(result.x_, result.y_, result.z_);
    }
    
    // Convert to rotation matrix (3x3)
    std::array<double, 9> toRotationMatrix() const {
        double xx = x_ * x_;
        double xy = x_ * y_;
        double xz = x_ * z_;
        double xw = x_ * w_;
        double yy = y_ * y_;
        double yz = y_ * z_;
        double yw = y_ * w_;
        double zz = z_ * z_;
        double zw = z_ * w_;
        
        return {{
            1 - 2 * (yy + zz), 2 * (xy - zw), 2 * (xz + yw),
            2 * (xy + zw), 1 - 2 * (xx + zz), 2 * (yz - xw),
            2 * (xz - yw), 2 * (yz + xw), 1 - 2 * (xx + yy)
        }};
    }
    
    // Convert to Euler angles
    std::array<double, 3> toEuler() const {
        double roll = std::atan2(2 * (w_ * x_ + y_ * z_), 1 - 2 * (x_ * x_ + y_ * y_));
        double pitch = std::asin(2 * (w_ * y_ - z_ * x_));
        double yaw = std::atan2(2 * (w_ * z_ + x_ * y_), 1 - 2 * (y_ * y_ + z_ * z_));
        
        return {{roll, pitch, yaw}};
    }

private:
    double w_, x_, y_, z_;
};

/**
 * @brief 3D transformation matrix (4x4)
 */
class Transform3D {
public:
    Transform3D() {
        setIdentity();
    }
    
    explicit Transform3D(const std::array<double, 16>& matrix) : matrix_(matrix) {}
    
    void setIdentity() {
        matrix_.fill(0.0);
        matrix_[0] = matrix_[5] = matrix_[10] = matrix_[15] = 1.0;
    }
    
    // Accessors
    const std::array<double, 16>& matrix() const { return matrix_; }
    std::array<double, 16>& matrix() { return matrix_; }
    
    double& operator()(int row, int col) {
        return matrix_[row * 4 + col];
    }
    
    const double& operator()(int row, int col) const {
        return matrix_[row * 4 + col];
    }
    
    // Factory methods
    static Transform3D translation(double x, double y, double z) {
        Transform3D result;
        result(0, 3) = x;
        result(1, 3) = y;
        result(2, 3) = z;
        return result;
    }
    
    static Transform3D rotation(const Quaternion& q) {
        Transform3D result;
        auto rotMatrix = q.toRotationMatrix();
        
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; ++j) {
                result(i, j) = rotMatrix[i * 3 + j];
            }
        }
        
        return result;
    }
    
    static Transform3D scale(double sx, double sy, double sz) {
        Transform3D result;
        result(0, 0) = sx;
        result(1, 1) = sy;
        result(2, 2) = sz;
        return result;
    }
    
    // Transformation operations
    Transform3D operator*(const Transform3D& other) const {
        Transform3D result;
        
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                result(i, j) = 0.0;
                for (int k = 0; k < 4; ++k) {
                    result(i, j) += (*this)(i, k) * other(k, j);
                }
            }
        }
        
        return result;
    }
    
    Point3D transform(const Point3D& point) const {
        double x = (*this)(0, 0) * point.x() + (*this)(0, 1) * point.y() + 
                   (*this)(0, 2) * point.z() + (*this)(0, 3);
        double y = (*this)(1, 0) * point.x() + (*this)(1, 1) * point.y() + 
                   (*this)(1, 2) * point.z() + (*this)(1, 3);
        double z = (*this)(2, 0) * point.x() + (*this)(2, 1) * point.y() + 
                   (*this)(2, 2) * point.z() + (*this)(2, 3);
        
        return Point3D(x, y, z);
    }
    
    Transform3D inverse() const {
        // Simplified inverse for rigid body transforms
        Transform3D result;
        
        // Transpose rotation part
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; ++j) {
                result(i, j) = (*this)(j, i);
            }
        }
        
        // Negative rotated translation
        Point3D translation((*this)(0, 3), (*this)(1, 3), (*this)(2, 3));
        Point3D negTranslation = result.transform(translation) * -1.0;
        
        result(0, 3) = negTranslation.x();
        result(1, 3) = negTranslation.y();
        result(2, 3) = negTranslation.z();
        
        return result;
    }

private:
    std::array<double, 16> matrix_;
};

/**
 * @brief 2D bounding box
 */
class BoundingBox2D {
public:
    BoundingBox2D() : min_(0, 0), max_(0, 0), empty_(true) {}
    BoundingBox2D(const Point2D& min, const Point2D& max) 
        : min_(min), max_(max), empty_(false) {}
    
    // Accessors
    const Point2D& min() const { return min_; }
    const Point2D& max() const { return max_; }
    bool empty() const { return empty_; }
    
    // Properties
    double width() const { return empty_ ? 0.0 : max_.x() - min_.x(); }
    double height() const { return empty_ ? 0.0 : max_.y() - min_.y(); }
    double area() const { return width() * height(); }
    
    Point2D center() const {
        return empty_ ? Point2D() : Point2D((min_.x() + max_.x()) * 0.5, 
                                           (min_.y() + max_.y()) * 0.5);
    }
    
    // Operations
    void expand(const Point2D& point) {
        if (empty_) {
            min_ = max_ = point;
            empty_ = false;
        } else {
            min_.setX(std::min(min_.x(), point.x()));
            min_.setY(std::min(min_.y(), point.y()));
            max_.setX(std::max(max_.x(), point.x()));
            max_.setY(std::max(max_.y(), point.y()));
        }
    }
    
    void expand(const BoundingBox2D& other) {
        if (!other.empty_) {
            expand(other.min_);
            expand(other.max_);
        }
    }
    
    bool contains(const Point2D& point) const {
        return !empty_ && 
               point.x() >= min_.x() && point.x() <= max_.x() &&
               point.y() >= min_.y() && point.y() <= max_.y();
    }
    
    bool intersects(const BoundingBox2D& other) const {
        return !empty_ && !other.empty_ &&
               min_.x() <= other.max_.x() && max_.x() >= other.min_.x() &&
               min_.y() <= other.max_.y() && max_.y() >= other.min_.y();
    }
    
    BoundingBox2D intersection(const BoundingBox2D& other) const {
        if (!intersects(other)) {
            return BoundingBox2D();
        }
        
        Point2D newMin(std::max(min_.x(), other.min_.x()),
                      std::max(min_.y(), other.min_.y()));
        Point2D newMax(std::min(max_.x(), other.max_.x()),
                      std::min(max_.y(), other.max_.y()));
        
        return BoundingBox2D(newMin, newMax);
    }

private:
    Point2D min_, max_;
    bool empty_;
};

/**
 * @brief 3D bounding box
 */
class BoundingBox3D {
public:
    BoundingBox3D() : min_(0, 0, 0), max_(0, 0, 0), empty_(true) {}
    BoundingBox3D(const Point3D& min, const Point3D& max) 
        : min_(min), max_(max), empty_(false) {}
    
    // Accessors
    const Point3D& min() const { return min_; }
    const Point3D& max() const { return max_; }
    bool empty() const { return empty_; }
    
    // Properties
    double width() const { return empty_ ? 0.0 : max_.x() - min_.x(); }
    double height() const { return empty_ ? 0.0 : max_.y() - min_.y(); }
    double depth() const { return empty_ ? 0.0 : max_.z() - min_.z(); }
    double volume() const { return width() * height() * depth(); }
    
    Point3D center() const {
        return empty_ ? Point3D() : Point3D((min_.x() + max_.x()) * 0.5, 
                                           (min_.y() + max_.y()) * 0.5,
                                           (min_.z() + max_.z()) * 0.5);
    }
    
    // Operations
    void expand(const Point3D& point) {
        if (empty_) {
            min_ = max_ = point;
            empty_ = false;
        } else {
            min_.setX(std::min(min_.x(), point.x()));
            min_.setY(std::min(min_.y(), point.y()));
            min_.setZ(std::min(min_.z(), point.z()));
            max_.setX(std::max(max_.x(), point.x()));
            max_.setY(std::max(max_.y(), point.y()));
            max_.setZ(std::max(max_.z(), point.z()));
        }
    }
    
    void expand(const BoundingBox3D& other) {
        if (!other.empty_) {
            expand(other.min_);
            expand(other.max_);
        }
    }
    
    bool contains(const Point3D& point) const {
        return !empty_ && 
               point.x() >= min_.x() && point.x() <= max_.x() &&
               point.y() >= min_.y() && point.y() <= max_.y() &&
               point.z() >= min_.z() && point.z() <= max_.z();
    }
    
    bool intersects(const BoundingBox3D& other) const {
        return !empty_ && !other.empty_ &&
               min_.x() <= other.max_.x() && max_.x() >= other.min_.x() &&
               min_.y() <= other.max_.y() && max_.y() >= other.min_.y() &&
               min_.z() <= other.max_.z() && max_.z() >= other.min_.z();
    }

private:
    Point3D min_, max_;
    bool empty_;
};

/**
 * @brief Line segment in 2D
 */
class LineSegment2D {
public:
    LineSegment2D(const Point2D& start, const Point2D& end) 
        : start_(start), end_(end) {}
    
    const Point2D& start() const { return start_; }
    const Point2D& end() const { return end_; }
    
    double length() const { return start_.distance(end_); }
    
    Point2D direction() const { return (end_ - start_).normalize(); }
    
    Point2D pointAt(double t) const {
        return start_ + (end_ - start_) * t;
    }
    
    double distanceToPoint(const Point2D& point) const {
        Point2D dir = end_ - start_;
        double lengthSq = dir.x() * dir.x() + dir.y() * dir.y();
        
        if (lengthSq < 1e-10) {
            return start_.distance(point);
        }
        
        double t = std::max(0.0, std::min(1.0, 
            (point - start_).dot(dir) / lengthSq));
        
        Point2D projection = start_ + dir * t;
        return point.distance(projection);
    }
    
    bool intersects(const LineSegment2D& other, Point2D* intersection = nullptr) const {
        Point2D dir1 = end_ - start_;
        Point2D dir2 = other.end_ - other.start_;
        Point2D diff = other.start_ - start_;
        
        double cross = dir1.cross(dir2);
        
        if (std::abs(cross) < 1e-10) {
            return false; // Parallel lines
        }
        
        double t1 = diff.cross(dir2) / cross;
        double t2 = diff.cross(dir1) / cross;
        
        if (t1 >= 0.0 && t1 <= 1.0 && t2 >= 0.0 && t2 <= 1.0) {
            if (intersection) {
                *intersection = start_ + dir1 * t1;
            }
            return true;
        }
        
        return false;
    }

private:
    Point2D start_, end_;
};

/**
 * @brief Polygon in 2D
 */
class Polygon2D {
public:
    Polygon2D() = default;
    explicit Polygon2D(const std::vector<Point2D>& vertices) : vertices_(vertices) {}
    
    void addVertex(const Point2D& vertex) {
        vertices_.push_back(vertex);
    }
    
    const std::vector<Point2D>& vertices() const { return vertices_; }
    size_t vertexCount() const { return vertices_.size(); }
    
    double area() const {
        if (vertices_.size() < 3) return 0.0;
        
        double area = 0.0;
        size_t n = vertices_.size();
        
        for (size_t i = 0; i < n; ++i) {
            size_t j = (i + 1) % n;
            area += vertices_[i].x() * vertices_[j].y();
            area -= vertices_[j].x() * vertices_[i].y();
        }
        
        return std::abs(area) * 0.5;
    }
    
    Point2D centroid() const {
        if (vertices_.empty()) return Point2D();
        
        double cx = 0.0, cy = 0.0;
        double area = 0.0;
        size_t n = vertices_.size();
        
        for (size_t i = 0; i < n; ++i) {
            size_t j = (i + 1) % n;
            double a = vertices_[i].x() * vertices_[j].y() - vertices_[j].x() * vertices_[i].y();
            area += a;
            cx += (vertices_[i].x() + vertices_[j].x()) * a;
            cy += (vertices_[i].y() + vertices_[j].y()) * a;
        }
        
        area *= 0.5;
        if (std::abs(area) < 1e-10) {
            // Fallback to arithmetic mean
            Point2D sum;
            for (const auto& vertex : vertices_) {
                sum += vertex;
            }
            return sum / static_cast<double>(vertices_.size());
        }
        
        return Point2D(cx / (6.0 * area), cy / (6.0 * area));
    }
    
    bool contains(const Point2D& point) const {
        if (vertices_.size() < 3) return false;
        
        bool inside = false;
        size_t n = vertices_.size();
        
        for (size_t i = 0, j = n - 1; i < n; j = i++) {
            if (((vertices_[i].y() > point.y()) != (vertices_[j].y() > point.y())) &&
                (point.x() < (vertices_[j].x() - vertices_[i].x()) * 
                 (point.y() - vertices_[i].y()) / (vertices_[j].y() - vertices_[i].y()) + vertices_[i].x())) {
                inside = !inside;
            }
        }
        
        return inside;
    }
    
    BoundingBox2D boundingBox() const {
        if (vertices_.empty()) return BoundingBox2D();
        
        BoundingBox2D box;
        for (const auto& vertex : vertices_) {
            box.expand(vertex);
        }
        
        return box;
    }
    
    std::vector<size_t> triangulate() const {
        std::vector<size_t> result;
        if (vertices_.size() < 3) return result;
        
        std::vector<size_t> indices(vertices_.size());
        std::iota(indices.begin(), indices.end(), 0);
        
        while (indices.size() > 3) {
            bool earFound = false;
            
            for (size_t i = 0; i < indices.size(); ++i) {
                size_t prev = (i == 0) ? indices.size() - 1 : i - 1;
                size_t next = (i + 1) % indices.size();
                
                if (isEar(vertices_, indices, prev, i, next)) {
                    result.push_back(indices[prev]);
                    result.push_back(indices[i]);
                    result.push_back(indices[next]);
                    
                    indices.erase(indices.begin() + i);
                    earFound = true;
                    break;
                }
            }
            
            if (!earFound) break; // Degenerate polygon
        }
        
        if (indices.size() == 3) {
            result.push_back(indices[0]);
            result.push_back(indices[1]);
            result.push_back(indices[2]);
        }
        
        return result;
    }

private:
    std::vector<Point2D> vertices_;
    
    static bool isEar(const std::vector<Point2D>& vertices,
                      const std::vector<size_t>& indices,
                      size_t prev, size_t curr, size_t next) {
        const Point2D& a = vertices[indices[prev]];
        const Point2D& b = vertices[indices[curr]];
        const Point2D& c = vertices[indices[next]];
        
        // Check if triangle is oriented correctly (counter-clockwise)
        Point2D v1 = b - a;
        Point2D v2 = c - b;
        if (v1.cross(v2) <= 0) return false;
        
        // Check if any other vertex is inside the triangle
        for (size_t i = 0; i < indices.size(); ++i) {
            if (i == prev || i == curr || i == next) continue;
            
            const Point2D& p = vertices[indices[i]];
            if (pointInTriangle(p, a, b, c)) {
                return false;
            }
        }
        
        return true;
    }
    
    static bool pointInTriangle(const Point2D& p, const Point2D& a, const Point2D& b, const Point2D& c) {
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
};

} // namespace axonvex::types::geometry