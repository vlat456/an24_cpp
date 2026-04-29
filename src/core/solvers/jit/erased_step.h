#pragma once

/// Type-erased invocable for simulation component dispatch.
///
/// Encodes a (T*, method) pair as (void*, function-pointer) internally.
/// Type safety is enforced at construction — the only way to create a
/// valid ErasedStep is via the template factories, which capture the
/// concrete type T in a lambda.  Consumers never see the void*.
///
/// Layout: 2 pointers (16 bytes on 64-bit), trivially copyable.
/// Zero overhead vs a raw function-pointer call at dispatch time.

struct SimulationState;

struct ErasedStep {
    using Fn = void (*)(void*, SimulationState&, double);

    /// Invoke the stored operation.  Precondition: `*this` is non-empty
    /// (i.e. created via execute_for / commit_for).
    void invoke(SimulationState& st, double dt) const {
        fn_(self_, st, dt);
    }

    /// True if a valid operation is stored (non-null fn and self).
    explicit operator bool() const { return fn_ != nullptr && self_ != nullptr; }

    // -- Factories (only way to create a valid ErasedStep) --

    /// Create an ErasedStep that calls `component->execute(st, dt)`.
    /// SFINAE-safe: if T has no execute(), the lambda is a no-op.
    template <typename T>
    static ErasedStep execute_for(T* component) {
        return ErasedStep{
            component,
            [](void* self, SimulationState& st, double dt) {
                if constexpr (requires(T & c) { c.execute(st, dt); }) {
                    static_cast<T*>(self)->execute(st, dt);
                }
                else {
                    (void)self;
                    (void)st;
                    (void)dt;
                }
            }
        };
    }

    /// Create an ErasedStep that calls `component->commit(st, dt)`.
    /// SFINAE-safe: if T has no commit(), the lambda is a no-op.
    template <typename T>
    static ErasedStep commit_for(T* component) {
        return ErasedStep{
            component,
            [](void* self, SimulationState& st, double dt) {
                if constexpr (requires(T & c) { c.commit(st, dt); }) {
                    static_cast<T*>(self)->commit(st, dt);
                }
                else {
                    (void)self;
                    (void)st;
                    (void)dt;
                }
            }
        };
    }

private:
    /// Private — only the factories can create a valid ErasedStep.
    ErasedStep(void* self, Fn fn) : self_(self), fn_(fn) {}

    void* self_ = nullptr;
    Fn    fn_   = nullptr;
};
