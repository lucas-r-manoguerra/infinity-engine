// tests/math/vec3_test.cpp
#include "infinity/math/vec3.h"

#include <cmath>
#include <random>

#include <doctest/doctest.h>

using namespace infinity::math;

namespace {

constexpr float EPSILON = 1e-4f;

// Approx with a tolerance suited to float sqrt math (relative 1e-4).
doctest::Approx near(float value) { return doctest::Approx(value).epsilon(1e-4); }

bool nearZero(float value) { return std::abs(value) < EPSILON; }

} // namespace

TEST_CASE("Vec3 default constructor zero-initializes all components") {
    const Vec3 v;
    CHECK(v.x == 0.0f);
    CHECK(v.y == 0.0f);
    CHECK(v.z == 0.0f);
}

TEST_CASE("Vec3 component constructor stores x, y and z") {
    const Vec3 v{1.0f, 2.0f, 3.0f};
    CHECK(v.x == 1.0f);
    CHECK(v.y == 2.0f);
    CHECK(v.z == 3.0f);
}

TEST_CASE("Vec3 zero and one return the expected constants") {
    const Vec3 z = Vec3::zero();
    CHECK(z.x == 0.0f);
    CHECK(z.y == 0.0f);
    CHECK(z.z == 0.0f);

    const Vec3 o = Vec3::one();
    CHECK(o.x == 1.0f);
    CHECK(o.y == 1.0f);
    CHECK(o.z == 1.0f);
}

TEST_CASE("Vec3 up right and forward follow the +Y up -Z forward convention") {
    const Vec3 up = Vec3::up();
    CHECK(up.x == 0.0f);
    CHECK(up.y == 1.0f);
    CHECK(up.z == 0.0f);

    const Vec3 right = Vec3::right();
    CHECK(right.x == 1.0f);
    CHECK(right.y == 0.0f);
    CHECK(right.z == 0.0f);

    const Vec3 forward = Vec3::forward();
    CHECK(forward.x == 0.0f);
    CHECK(forward.y == 0.0f);
    CHECK(forward.z == -1.0f);
}

TEST_CASE("Vec3 addition is component-wise") {
    const Vec3 a{1.0f, 2.0f, 3.0f};
    const Vec3 b{3.0f, 4.0f, 5.0f};
    const Vec3 r = a + b;
    CHECK(r.x == 4.0f);
    CHECK(r.y == 6.0f);
    CHECK(r.z == 8.0f);
}

TEST_CASE("Vec3 subtraction is component-wise") {
    const Vec3 a{1.0f, 2.0f, 3.0f};
    const Vec3 b{3.0f, 4.0f, 5.0f};
    const Vec3 r = a - b;
    CHECK(r.x == -2.0f);
    CHECK(r.y == -2.0f);
    CHECK(r.z == -2.0f);
}

TEST_CASE("Vec3 unary minus negates all components") {
    const Vec3 v{1.0f, -2.0f, 3.0f};
    const Vec3 r = -v;
    CHECK(r.x == -1.0f);
    CHECK(r.y == 2.0f);
    CHECK(r.z == -3.0f);
}

TEST_CASE("Vec3 scalar multiplication scales all components") {
    const Vec3 v{1.0f, 2.0f, 3.0f};
    const Vec3 r = v * 3.0f;
    CHECK(r.x == 3.0f);
    CHECK(r.y == 6.0f);
    CHECK(r.z == 9.0f);
}

TEST_CASE("Vec3 scalar multiplication by a negative scalar flips the sign") {
    const Vec3 v{1.0f, 2.0f, 3.0f};
    const Vec3 r = v * -2.0f;
    CHECK(r.x == -2.0f);
    CHECK(r.y == -4.0f);
    CHECK(r.z == -6.0f);
}

TEST_CASE("Vec3 scalar division divides all components") {
    const Vec3 v{2.0f, 4.0f, 6.0f};
    const Vec3 r = v / 2.0f;
    CHECK(r.x == 1.0f);
    CHECK(r.y == 2.0f);
    CHECK(r.z == 3.0f);
}

TEST_CASE("Vec3 compound operators mutate in place") {
    Vec3 v{1.0f, 2.0f, 3.0f};
    v += Vec3{3.0f, 4.0f, 5.0f};
    CHECK(v.x == 4.0f);
    CHECK(v.y == 6.0f);
    CHECK(v.z == 8.0f);
    v -= Vec3{1.0f, 1.0f, 1.0f};
    CHECK(v.x == 3.0f);
    CHECK(v.y == 5.0f);
    CHECK(v.z == 7.0f);
    v *= 2.0f;
    CHECK(v.x == 6.0f);
    CHECK(v.y == 10.0f);
    CHECK(v.z == 14.0f);
    v /= 4.0f;
    CHECK(v.x == 1.5f);
    CHECK(v.y == 2.5f);
    CHECK(v.z == 3.5f);
}

TEST_CASE("Vec3 compound operators return a reference to this") {
    Vec3 v{1.0f, 1.0f, 1.0f};
    Vec3& ref = (v += Vec3{1.0f, 1.0f, 1.0f});
    CHECK(&ref == &v);
}

TEST_CASE("Vec3 dot equals the sum of component products") {
    const Vec3 a{1.0f, 2.0f, 3.0f};
    const Vec3 b{4.0f, 5.0f, 6.0f};
    CHECK(a.dot(b) == 32.0f);
}

TEST_CASE("Vec3 dot of perpendicular vectors is zero") {
    CHECK(Vec3::right().dot(Vec3::up()) == 0.0f);
}

TEST_CASE("Vec3 cross is perpendicular to both inputs") {
    const Vec3 a{1.0f, 0.0f, 0.0f};
    const Vec3 b{0.0f, 1.0f, 0.0f};
    const Vec3 c = a.cross(b);
    CHECK(c.x == 0.0f);
    CHECK(c.y == 0.0f);
    CHECK(c.z == 1.0f);
    CHECK(nearZero(c.dot(a)));
    CHECK(nearZero(c.dot(b)));
}

TEST_CASE("Vec3 cross of basis vectors follows the right-hand rule") {
    // With +Y up / -Z forward: cross(up, right) == forward.
    const Vec3 r = Vec3::up().cross(Vec3::right());
    CHECK(r.x == 0.0f);
    CHECK(r.y == 0.0f);
    CHECK(r.z == -1.0f);
}

TEST_CASE("Vec3 lengthSquared equals the sum of component squares") {
    const Vec3 v{1.0f, 2.0f, 2.0f};
    CHECK(v.lengthSquared() == 9.0f);
}

TEST_CASE("Vec3 length returns the Euclidean magnitude") {
    CHECK(Vec3{3.0f, 4.0f, 0.0f}.length() == 5.0f);
    CHECK(Vec3{1.0f, 2.0f, 2.0f}.length() == 3.0f);
}

TEST_CASE("Vec3 normalized keeps direction and returns unit length") {
    const Vec3 v{3.0f, 0.0f, 4.0f};
    const Vec3 n = v.normalized();
    CHECK(n.x == near(0.6f));
    CHECK(n.y == near(0.0f));
    CHECK(n.z == near(0.8f));
    CHECK(n.length() == near(1.0f));
}

TEST_CASE("Vec3 normalized of zero vector returns zero (ADR-056 policy)") {
    const Vec3 n = Vec3::zero().normalized();
    CHECK(n.x == 0.0f);
    CHECK(n.y == 0.0f);
    CHECK(n.z == 0.0f);
}

TEST_CASE("Vec3 normalized of a NaN vector propagates NaN") {
    const Vec3 n = Vec3{std::nanf(""), 1.0f, 1.0f}.normalized();
    CHECK(std::isnan(n.x));
}

TEST_CASE("Vec3 normalized of random vectors has unit length and same direction") {
    // Deterministic: fixed seed (rule 06, ADR-017), never flaky.
    std::mt19937 rng{20260101u};
    std::uniform_real_distribution<float> dist{-10.0f, 10.0f};
    int checked = 0;
    for (int i = 0; i < 100; ++i) {
        const Vec3 v{dist(rng), dist(rng), dist(rng)};
        if (v.lengthSquared() == 0.0f) {
            continue;
        }
        const Vec3 n = v.normalized();
        CHECK(n.length() == near(1.0f));
        CHECK(n.dot(v) > 0.0f);
        const Vec3 c = n.cross(v);
        CHECK(nearZero(c.x));
        CHECK(nearZero(c.y));
        CHECK(nearZero(c.z));
        ++checked;
    }
    CHECK(checked == 100);
}

TEST_CASE("Vec3 distance is the length of the difference") {
    const Vec3 a{1.0f, 2.0f, 3.0f};
    const Vec3 b{4.0f, 6.0f, 3.0f};
    CHECK(distance(a, b) == 5.0f);
    CHECK(distance(a, a) == 0.0f);
}

TEST_CASE("Vec3 lerp at t=0.5 is the midpoint") {
    const Vec3 a{1.0f, 2.0f, 3.0f};
    const Vec3 b{5.0f, 4.0f, 1.0f};
    const Vec3 m = lerp(a, b, 0.5f);
    CHECK(m.x == near(3.0f));
    CHECK(m.y == near(3.0f));
    CHECK(m.z == near(2.0f));
}

TEST_CASE("Vec3 lerp at t=0 returns a and t=1 returns b") {
    const Vec3 a{1.0f, 2.0f, 3.0f};
    const Vec3 b{5.0f, 4.0f, 1.0f};
    const Vec3 atZero = lerp(a, b, 0.0f);
    CHECK(atZero.x == near(1.0f));
    CHECK(atZero.y == near(2.0f));
    CHECK(atZero.z == near(3.0f));
    const Vec3 atOne = lerp(a, b, 1.0f);
    CHECK(atOne.x == near(5.0f));
    CHECK(atOne.y == near(4.0f));
    CHECK(atOne.z == near(1.0f));
}
