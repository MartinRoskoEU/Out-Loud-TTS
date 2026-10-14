#pragma once
#include "maintenance_codec.hpp"
namespace outloud::installer {
inline constexpr bool uninstall_lifecycle_ready=true;
// Forward-only complete removal. Registry transactions do not make file/key deletion atomic.
template<class Operations> void uninstall_installation(Operations& op){
    op.preflight();op.persist();op.revalidate();
    op.remove_registration(); // all owned tokens and COM in one checked registry transaction
    op.verify_no_dependencies();
    op.remove_data();op.remove_payload();
    op.remove_signing(); // resources are independently retryable; receipts remain
    op.verify_native_removed();op.await_host();
}
}
