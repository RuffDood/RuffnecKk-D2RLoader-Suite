auto ValidatePickupEntry() noexcept -> bool {
    const D2RL::DiagnosticsServiceV1* diagnostics{};
    if (Context->QueryService(D2RL::ServiceId::Diagnostics, D2RL::DiagnosticsServiceV1Version, &diagnostics) == D2RL::ServiceQueryResult::Success
        && D2RL::HasDiagnosticsServiceV1Field(diagnostics, D2RL::DiagnosticsServiceV1RequiredSize)
        && diagnostics->queryHookStatus) {
        const D2RL::Diagnostics::HookQuery query{D2RL::Diagnostics::HookQuerySize, 0, PickupRva, PickupExpected.data(), static_cast<std::uint32_t>(PickupExpected.size()), 0};
        D2RL::Diagnostics::HookStatus status{}; status.structSize = D2RL::Diagnostics::HookStatusSize;
        if (diagnostics->queryHookStatus(Context, &query, &status) == D2RL::Diagnostics::Result::Success) {
            const auto unchanged = status.state == D2RL::Diagnostics::ModificationState::Unchanged;
            const auto known = status.state == D2RL::Diagnostics::ModificationState::Tracked && status.kind == D2RL::Diagnostics::ModificationKind::InlineHook;
            status.ownerPluginId[sizeof(status.ownerPluginId)-1] = '\0';
            if (AcceptPickupEntry(unchanged, known, status.ownerCount, status.ownerPluginId)) {
                if (!unchanged) Context->LogInfo("TeleportPickup: using Auto Deposit's tracked pickup entry; its normal deposit processing remains in the call path.");
                return true;
            }
            Context->LogError("TeleportPickup: unexpected modification of the pickup entry; nothing was patched.");
            return false;
        }
    }
    return Context->CheckExpectedBytes(PickupRva, PickupExpected.data(), static_cast<std::uint32_t>(PickupExpected.size()));
}
