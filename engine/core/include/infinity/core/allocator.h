// infinity/core/allocator.h
//
// Allocator interface (F2.1, ADR-005, rule 03). The contract every subsystem
// that owns memory allocates through:
//
//   Ownership   - Every subsystem receives an explicit Allocator& in its
//                 constructor/init and keeps it by reference. The allocator
//                 is injected, never owned: it must outlive the subsystem
//                 that uses it.
//   Typed use   - Business logic never writes raw allocate()/deallocate()
//                 nor bare new. It uses allocateObject<T>/deallocateObject<T>
//                 so size and alignment never drift from the type.
//   Alignment   - Explicit from day 1 (F2.2 needs 16-byte SIMD math, rule 07).
//                 Alignment is a power of two >= 1; anything else is a
//                 programming error (assert/panic, ADR-003).
//   Failure     - No exceptions (-fno-exceptions) and no std::expected on the
//                 hot path: allocate() returns nullptr on failure and callers
//                 check it. nullptr is never a valid allocation.
//   Determinism - Allocators add no hidden state (rule 11, ADR-056): the same
//                 request sequence yields the same results. State lives in
//                 the concrete backend, never in mutable globals.
//
// Concrete backends (malloc wrapper, arena, pool) arrive in F2.2.
#pragma once

#include <cstddef>

namespace infinity::core {

// Returns true when alignment is a power of two and at least 1. Byte-aligned
// (alignment 1) is always valid.
[[nodiscard]] constexpr bool isValidAlignment(size_t alignment) noexcept {
    return alignment >= 1 && (alignment & (alignment - 1)) == 0;
}

// Abstract memory allocation interface (F2.1, ADR-005).
//
// Backends implement allocate/deallocate and decide how to honor alignment.
// They may restrict the contract (e.g. a pool serves one alignment only) but
// never relax it: size and alignment contracts hold for every backend.
class Allocator {
public:
    // Polymorphic base: copying would slice the backend, so allocators are
    // always passed by reference (rule 03: no state sharing across backends).
    Allocator(const Allocator&) = delete;
    Allocator& operator=(const Allocator&) = delete;

    virtual ~Allocator() = default;

    // Allocates size bytes aligned to alignment. Returns nullptr when the
    // request cannot be satisfied. alignment must be a power of two >= 1
    // (isValidAlignment); a violation is a programming error, asserted in
    // debug and handled in release (ADR-003).
    [[nodiscard]] virtual void* allocate(size_t size, size_t alignment) noexcept = 0;

    // Releases a block previously returned by allocate(). size must equal the
    // size of the original allocation; anything else is undefined behavior.
    // Passing nullptr is a no-op.
    virtual void deallocate(void* ptr, size_t size) noexcept = 0;

    // Reports whether this backend can honor alignment. Alignment 1 is always
    // supported. Pools use this to reject alignments they cannot serve.
    [[nodiscard]] virtual bool supportsAlignment(size_t alignment) const noexcept = 0;

    // Typed allocation: returns sizeof(T) bytes aligned to alignment, or
    // nullptr on failure. Prefer this over raw allocate() in business code.
    template <typename T> [[nodiscard]] T* allocateObject(size_t alignment) noexcept {
        return static_cast<T*>(allocate(sizeof(T), alignment));
    }

    // Typed release: forwards the block with sizeof(T) as its size, which
    // satisfies the size-matching contract of deallocate(). ptr may be
    // nullptr.
    template <typename T> void deallocateObject(T* ptr) noexcept { deallocate(ptr, sizeof(T)); }

protected:
    Allocator() = default;
};

} // namespace infinity::core
