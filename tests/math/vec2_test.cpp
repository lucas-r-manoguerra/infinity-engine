// tests/math/vec2_test.cpp
#include "infinity/math/vec2.h"

#include <doctest/doctest.h>

using namespace infinity::math;

TEST_CASE("Vec2 default constructor zero-initializes both components") {
    const Vec2 v;
    CHECK(v.x == 0.0f);
    CHECK(v.y == 0.0f);
}

TEST_CASE("Vec2 component constructor stores x and y") {
    const Vec2 v{1.5f, -2.0f};
    CHECK(v.x == 1.5f);
    CHECK(v.y == -2.0f);
}

TEST_CASE("Vec2 zero and one return the expected constants") {
    const Vec2 z = Vec2::zero();
    CHECK(z.x == 0.0f);
    CHECK(z.y == 0.0f);

    const Vec2 o = Vec2::one();
    CHECK(o.x == 1.0f);
    CHECK(o.y == 1.0f);
}

TEST_CASE("Vec2 addition is component-wise") {
    const Vec2 a{1.0f, 2.0f};
    const Vec2 b{3.0f, 4.0f};
    const Vec2 r = a + b;
    CHECK(r.x == 4.0f);
    CHECK(r.y == 6.0f);
}

TEST_CASE("Vec2 subtraction is component-wise") {
    const Vec2 a{1.0f, 2.0f};
    const Vec2 b{3.0f, 4.0f};
    const Vec2 r = a - b;
    CHECK(r.x == -2.0f);
    CHECK(r.y == -2.0f);
}

TEST_CASE("Vec2 unary minus negates both components") {
    const Vec2 v{1.0f, -2.0f};
    const Vec2 r = -v;
    CHECK(r.x == -1.0f);
    CHECK(r.y == 2.0f);
}

TEST_CASE("Vec2 scalar multiplication scales both components") {
    const Vec2 v{1.0f, 2.0f};
    const Vec2 r = v * 3.0f;
    CHECK(r.x == 3.0f);
    CHECK(r.y == 6.0f);
}

TEST_CASE("Vec2 scalar multiplication by a negative scalar flips the sign") {
    const Vec2 v{1.0f, 2.0f};
    const Vec2 r = v * -2.0f;
    CHECK(r.x == -2.0f);
    CHECK(r.y == -4.0f);
}

TEST_CASE("Vec2 scalar division divides both components") {
    const Vec2 v{2.0f, 4.0f};
    const Vec2 r = v / 2.0f;
    CHECK(r.x == 1.0f);
    CHECK(r.y == 2.0f);
}

TEST_CASE("Vec2 compound operators mutate in place") {
    Vec2 v{1.0f, 2.0f};
    v += Vec2{3.0f, 4.0f};
    CHECK(v.x == 4.0f);
    CHECK(v.y == 6.0f);
    v -= Vec2{1.0f, 1.0f};
    CHECK(v.x == 3.0f);
    CHECK(v.y == 5.0f);
    v *= 2.0f;
    CHECK(v.x == 6.0f);
    CHECK(v.y == 10.0f);
    v /= 4.0f;
    CHECK(v.x == 1.5f);
    CHECK(v.y == 2.5f);
}

TEST_CASE("Vec2 compound operators return a reference to this") {
    Vec2 v{1.0f, 1.0f};
    Vec2& ref = (v += Vec2{1.0f, 1.0f});
    CHECK(&ref == &v);
}
