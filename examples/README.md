# Geometry runtime example

This example keeps geometry state and calculations in C++, while the movement
policy is controlled by `geometry.thimble`.

From the repository root, run:

```text
python3 build.py
```

The build script compiles and runs this example after the normal language and
amalgamated-header tests.

The C++ side registers an object descriptor and one shared-owned instance:

```cpp
auto world_type = host.define_object_type<GeometryWorld>("GeometryWorld");
world_type.property("total_area", &GeometryWorld::total_area);
world_type.method("move_circle", &GeometryWorld::move_circle);
host.bind_object("world", world, world_type);
```

The script controls the world using member syntax:

```thimble
world.move_circle(circle, 0.25, -0.10);
if (world.collision_count() > 0) {
    world.scale_all(0.90);
}
```

`policy_runtime.cpp` is a compact second example. It binds direct C++ data
members and an existing free function, compiles named source, then returns a map
containing validation and visibility decisions. It demonstrates the usual shape
for access rules, configuration checks and small UI policies.
