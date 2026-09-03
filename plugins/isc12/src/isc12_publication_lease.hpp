#pragma once

namespace ruffneckk::isc12 {

// Borrowed proof that the selected native-publication window is still active.
// The first production caller binds this view to the synchronous initial
// D2RLoaderLoadPlugin callback. It is deliberately not a general claim that
// D2RLoader exposes a loader-owned transaction or runtime quiescence service.
class NativePublicationLeaseView final {
public:
    using ValidateFn = bool (*)(void* context) noexcept;

    constexpr NativePublicationLeaseView() noexcept = default;

    [[nodiscard]] auto IsHeld() const noexcept -> bool {
        return validate_ && validate_(context_);
    }

    // Constructs the narrow production view owned by the current synchronous
    // initial-load callback. The caller must keep context alive until every
    // coordinator stage and readiness publication has returned.
    [[nodiscard]] static constexpr auto ForInitialLoad(
            void* context,
            ValidateFn validate) noexcept -> NativePublicationLeaseView {
        return NativePublicationLeaseView{context, validate};
    }

#if defined(ISC12_CODEC_PATCH_TESTING)
    [[nodiscard]] static constexpr auto ForTesting(
            void* context,
            ValidateFn validate) noexcept -> NativePublicationLeaseView {
        return NativePublicationLeaseView{context, validate};
    }
#endif

private:
    constexpr NativePublicationLeaseView(
            void* context,
            ValidateFn validate) noexcept
        : context_{context}, validate_{validate} {}

    void* context_{};
    ValidateFn validate_{};
};

} // namespace ruffneckk::isc12
