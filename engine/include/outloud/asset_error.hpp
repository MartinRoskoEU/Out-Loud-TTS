#pragma once
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>

namespace outloud {
enum class AssetErrorCode { io, invalid_data, compatibility };
class AssetError : public std::runtime_error {
public:
    AssetError(AssetErrorCode classification,std::string message,std::uint32_t system=0)
        : std::runtime_error(std::move(message)),code(classification),system_error(system) {}
    AssetErrorCode code;
    std::uint32_t system_error;
};
}
