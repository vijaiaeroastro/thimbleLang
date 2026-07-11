#include "thimble/thimble.hpp"

#include <cmath>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
struct Circle { double x; double y; double radius; };

class GeometryWorld {
public:
    GeometryWorld()
        : circles_{{0.0, 0.0, 2.0}, {3.0, 0.0, 1.5},
                   {8.0, 0.0, 1.0}, {8.8, 0.0, 1.0}} {}

    int count() const { return static_cast<int>(circles_.size()); }

    double total_area() const {
        constexpr double pi = 3.14159265358979323846;
        double result = 0.0;
        for (const auto& circle : circles_) result += pi * circle.radius * circle.radius;
        return result;
    }

    void move_circle(int index, double dx, double dy) {
        if (!valid_index(index)) return;
        auto& circle = circles_[static_cast<std::size_t>(index)];
        circle.x += dx;
        circle.y += dy;
    }

    void scale_all(double factor) {
        for (auto& circle : circles_) circle.radius *= factor;
    }

    double distance(int first, int second) const {
        if (!valid_index(first) || !valid_index(second)) return -1.0;
        const auto& a = circles_[static_cast<std::size_t>(first)];
        const auto& b = circles_[static_cast<std::size_t>(second)];
        const double dx = a.x - b.x;
        const double dy = a.y - b.y;
        return std::sqrt(dx * dx + dy * dy);
    }

    bool overlaps(int first, int second) const {
        if (!valid_index(first) || !valid_index(second)) return false;
        const auto& a = circles_[static_cast<std::size_t>(first)];
        const auto& b = circles_[static_cast<std::size_t>(second)];
        return distance(first, second) <= a.radius + b.radius;
    }

    int collision_count() const {
        int result = 0;
        for (int first = 0; first < count(); ++first)
            for (int second = first + 1; second < count(); ++second)
                if (overlaps(first, second)) ++result;
        return result;
    }

    std::string summary() const {
        std::ostringstream output;
        output << count() << " circles, " << collision_count()
               << " overlaps, area=" << total_area();
        return output.str();
    }

private:
    bool valid_index(int index) const { return index >= 0 && index < count(); }
    std::vector<Circle> circles_;
};

std::string read_file(const std::string& path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("could not open script: " + path);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: geometry_runtime <script.thimble>\n";
        return 2;
    }

    auto world = std::make_shared<GeometryWorld>();
    thimble::HostContext host;
    auto world_type = host.define_object_type<GeometryWorld>("GeometryWorld");
    world_type.property("total_area", &GeometryWorld::total_area);
    world_type.method("count", &GeometryWorld::count);
    world_type.method("move_circle", &GeometryWorld::move_circle);
    world_type.method("scale_all", &GeometryWorld::scale_all);
    world_type.method("distance", &GeometryWorld::distance);
    world_type.method("overlaps", &GeometryWorld::overlaps);
    world_type.method("collision_count", &GeometryWorld::collision_count);
    world_type.method("summary", &GeometryWorld::summary);
    host.bind_object("world", world, world_type);

    auto program = thimble::compile(read_file(argv[1]), host);
    if (!program) {
        std::cerr << "compile error: " << program.error().message << '\n';
        return 1;
    }
    auto result = program.value().execute(host, thimble::Limits{10000, 64});
    if (!result) {
        std::cerr << "runtime error: " << result.error().message << '\n';
        return 1;
    }
    auto message = result.value().as_string();
    if (!message) {
        std::cerr << "script returned " << thimble::type_name(result.value().type())
                  << ", expected string\n";
        return 1;
    }
    std::cout << message.value() << '\n';
}
