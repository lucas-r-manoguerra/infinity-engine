// tests/math/vec4_test.cpp
#include "infinity/math/vec4.h"

#include <cmath>

#include <doctest/doctest.h>

using namespace infinity::math;

namespace {

// Approx with a tolerance suited to float sqrt math (relative 1e-4).
doctest::Approx near(float value) { return doctest::Approx(value).epsilon(1e-4); }

} // namespace

TEST_CASE("Vec4 default constructor zero-initializes all components") {
    const Vec4 v;
    CHECK(v.x == 0.0f);
    CHECK(v.y == 0.0f);
    CHECK(v.z == 0.0f);
    CHECK(v.w == 0.0f);
}

TEST_CASE("Vec4 component constructor stores x, y, z and w") {
    const Vec4 v{1.0f, 2.0f, 3.0f, 4.0f};
    CHECK(v.x == 1.0f);
    CHECK(v.y == 2.0f);
    CHECK(v.z == 3.0f);
    CHECK(v.w == 4.0f);
}

TEST_CASE("Vec4 zero and one return the expected constants") {
    const Vec4 z = Vec4::zero();
    CHECK(z.x == 0.0f);
    CHECK(z.y == 0.0f);
    CHECK(z.z == 0.0f);
    CHECK(z.w == 0.0f);

    const Vec4 o = Vec4::one();
    CHECK(o.x == 1.0f);
    CHECK(o.y == 1.0f);
    CHECK(o.z == 1.0f);
    CHECK(o.w == 1.0f);
}

TEST_CASE("Vec4 addition is component-wise") {
    const Vec4 a{1.0f, 2.0f, 3.0f, 4.0f};
    const Vec4 b{3.0f, 4.0f, 5.0f, 6.0f};
    const Vec4 r = a + b;
    CHECK(r.x == 4.0f);
    CHECK(r.y == 6.0f);
    CHECK(r.z == 8.0f);
    CHECK(r.w == 10.0f);
}

TEST_CASE("Vec4 subtraction is component-wise") {
    const Vec4 a{1.0f, 2.0f, 3.0f, 4.0f};
    const Vec4 b{3.0f, 4.0f, 5.0f, 6.0f};
    const Vec4 r = a - b;
    CHECK(r.x == -2.0f);
    CHECK(r.y == -2.0f);
    CHECK(r.z == -2.0f);
    CHECK(r.w == -2.0f);
}

TEST_CASE("Vec4 unary minus negates all components") {
    const Vec4 v{1.0f, -2.0f, 3.0f, -4.0f};
    const Vec4 r = -v;
    CHECK(r.x == -1.0f);
    CHECK(r.y == 2.0f);
    CHECK(r.z == -3.0f);
    CHECK(r.w == 4.0f);
}

TEST_CASE("Vec4 scalar multiplication scales all components") {
    const Vec4 v{1.0f, 2.0f, 3.0f, 4.0f};
    const Vec4 r = v * 3.0f;
    CHECK(r.x == 3.0f);
    CHECK(r.y == 6.0f);
    CHECK(r.z == 9.0f);
    CHECK(r.w == 12.0f);
}

TEST_CASE("Vec4 scalar multiplication by a negative scalar flips the sign") {
    const Vec4 v{1.0f, 2.0f, 3.0f, 4.0f};
    const Vec4 r = v * -2.0f;
    CHECK(r.x == -2.0f);
    CHECK(r.y == -4.0f);
    CHECK(r.z == -6.0f);
    CHECK(r.w == -8.0f);
}

TEST_CASE("Vec4 scalar division divides all components") {
    const Vec4 v{2.0f, 4.0f, 6.0f, 8.0f};
    const Vec4 r = v / 2.0f;
    CHECK(r.x == 1.0f);
    CHECK(r.y == 2.0f);
    CHECK(r.z == 3.0f);
    CHECK(r.w == 4.0f);
}

TEST_CASE("Vec4 compound operators mutate in place") {
    Vec4 v{1.0f, 2.0f, 3.0f, 4.0f};
    v += Vec4{3.0f, 4.0f, 5.0f, 6.0f};
    CHECK(v.x == 4.0f);
    CHECK(v.y == 6.0f);
    CHECK(v.z == 8.0f);
    CHECK(v.w == 10.0f);
    v -= Vec4{1.0f, 1.0f, 1.0f, 1.0f};
    CHECK(v.x == 3.0f);
    CHECK(v.y == 5.0f);
    CHECK(v.z == 7.0f);
    CHECK(v.w == 9.0f);
    v *= 2.0f;
    CHECK(v.x == 6.0f);
    CHECK(v.y == 10.0f);
    CHECK(v.z == 14.0f);
    CHECK(v.w == 18.0f);
    v /= 4.0f;
    CHECK(v.x == 1.5f);
    CHECK(v.y == 2.5f);
    CHECK(v.z == 3.5f);
    CHECK(v.w == 4.5f);
}

TEST_CASE("Vec4 compound operators return a reference to this") {
    Vec4 v{1.0f, 1.0f, 1.0f, 1.0f};
    Vec4& ref = (v += Vec4{1.0f, 1.0f, 1.0f, 1.0f});
    CHECK(&ref == &v);
}

TEST_CASE("Vec4 dot equals the sum of component products") {
    const Vec4 a{1.0f, 2.0f, 3.0f, 4.0f};
    const Vec4 b{4.0f, 5.0f, 6.0f, 7.0f};
    CHECK(a.dot(b) == 60.0f);
}

TEST_CASE("Vec4 lengthSquared equals the sum of component squares") {
    const Vec4 v{1.0f, 2.0f, 2.0f, 0.0f};
    CHECK(v.lengthSquared() == 9.0f);
}

TEST_CASE("Vec4 length returns the Euclidean magnitude") {
    CHECK(Vec4{3.0f, 4.0f, 0.0f, 0.0f}.length() == 5.0f);
    CHECK(Vec4{1.0f, 4.0f, 4.0f, 4.0f}.length() == 7.0f);
}

TEST_CASE("Vec4 normalized keeps direction and returns unit length") {
    const Vec4 v{3.0f, 0.0f, 4.0f, 0.0f};
    const Vec4 n = v.normalized();
    CHECK(n.x == near(0.6f));
    CHECK(n.y == near(0.0f));
    CHECK(n.z == near(0.8f));
    CHECK(n.w == near(0.0f));
    CHECK(n.length() == near(1.0f));
}

TEST_CASE("Vec4 normalized of zero vector returns zero (ADR-056 policy)") {
    const Vec4 n = Vec4::zero().normalized();
    CHECK(n.x == 0.0f);
    CHECK(n.y == 0.0f);
    CHECK(n.z == 0.0f);
    CHECK(n.w == 0.0f);
}

TEST_CASE("Vec4 normalized of a NaN vector propagates NaN") {
    const Vec4 n = Vec4{std::nanf(""), 1.0f, 1.0f, 1.0f}.normalized();
    CHECK(std::isnan(n.x));
}

TEST_CASE("Vec4 lerp at t=0.5 is the midpoint") {
    const Vec4 a{1.0f, 2.0f, 3.0f, 4.0f};
    const Vec4 b{5.0f, 4.0f, 1.0f, 0.0f};
    const Vec4 m = lerp(a, b, 0.5f);
    CHECK(m.x == near(3.0f));
    CHECK(m.y == near(3.0f));
    CHECK(m.z == near(2.0f));
    CHECK(m.w == near(2.0f));
}

TEST_CASE("Vec4 lerp at t=0 returns a and t=1 returns b") {
    const Vec4 a{1.0f, 2.0f, 3.0f, 4.0f};
    const Vec4 b{5.0f, 4.0f, 1.0f, 0.0f};
    const Vec4 atZero = lerp(a, b, 0.0f);
    CHECK(atZero.x == near(1.0f));
    CHECK(atZero.y == near(2.0f));
    CHECK(atZero.z == near(3.0f));
    CHECK(atZero.w == near(4.0f));
    const Vec4 atOne = lerp(a, b, 1.0f);
    CHECK(atOne.x == near(5.0f));
    CHECK(atOne.y == near(4.0f));
    CHECK(atOne.z == near(1.0f));
    CHECK(atOne.w == near(0.0f));
}
