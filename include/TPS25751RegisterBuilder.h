#pragma once

/*
#include <memory>
#include "TPS25751RegisterFactory.h"
#include "TPS25751Status.h"
#include "TPS25751Mode.h"
#include "TPS25751BootFlags.h"
#include "TPS25751PowerPathStatus.h"
#include "TPS25751PortConfig.h"
*/

/**
 * @brief Builder class for creating specific register types using factory pattern
 * Provides type-safe register creation with fluent interface
 */
/*
class TPS25751RegisterBuilder {
public:
    /**
     * @brief Set custom factory (optional)
     #1#
    static TPS25751RegisterBuilder& withFactory(std::unique_ptr<TPS25751RegisterFactory> factory);

    /**
     * @brief Create a Status register
     #1#
    static std::unique_ptr<TPS25751Status> createStatus();
    static std::unique_ptr<TPS25751Status> createStatus(const uint8_t* data, size_t length);
    static std::unique_ptr<TPS25751Status> createStatus(const TPS25751Registers::RegisterInfo& regInfo);

    /**
     * @brief Create a Mode register
     #1#
    static std::unique_ptr<TPS25751Mode> createMode();
    static std::unique_ptr<TPS25751Mode> createMode(const uint8_t* data, size_t length);
    static std::unique_ptr<TPS25751Mode> createMode(const TPS25751Registers::RegisterInfo& regInfo);

    /**
     * @brief Create a Boot Flags register
     #1#
    static std::unique_ptr<TPS25751BootFlags> createBootFlags();
    static std::unique_ptr<TPS25751BootFlags> createBootFlags(const uint8_t* data, size_t length);
    static std::unique_ptr<TPS25751BootFlags> createBootFlags(const TPS25751Registers::RegisterInfo& regInfo);

    /**
     * @brief Create a Power Path Status register
     #1#
    static std::unique_ptr<TPS25751PowerPathStatus> createPowerPathStatus();
    static std::unique_ptr<TPS25751PowerPathStatus> createPowerPathStatus(const uint8_t* data, size_t length);
    static std::unique_ptr<TPS25751PowerPathStatus> createPowerPathStatus(const TPS25751Registers::RegisterInfo& regInfo);

    /**
     * @brief Create a Port Configuration register
     #1#
    static std::unique_ptr<TPS25751PortConfig> createPortConfig();
    static std::unique_ptr<TPS25751PortConfig> createPortConfig(const uint8_t* data, size_t length);
    static std::unique_ptr<TPS25751PortConfig> createPortConfig(const TPS25751Registers::RegisterInfo& regInfo);

    /**
     * @brief Generic register creation with type safety
     * @tparam T Register type to create
     * @return Unique pointer to register of type T
     #1#
    template<typename T>
    static std::unique_ptr<T> create() {
        static_assert(std::is_base_of_v<TPS25751Register, T>, "T must derive from TPS25751Register");

        // Use SFINAE instead of static_assert(false) to avoid compilation issues
        return createImpl<T>();
    }

    /**
     * @brief Generic register creation with data
     #1#
    template<typename T>
    static std::unique_ptr<T> create(const uint8_t* data, size_t length) {
        static_assert(std::is_base_of_v<TPS25751Register, T>, "T must derive from TPS25751Register");
        
        // Use SFINAE instead of static_assert(false) to avoid compilation issues
        return createImpl<T>(data, length);
    }

private:
    // Helper to safely cast factory-created register to specific type
    // Since we control the factory creation, we can use static_cast
    template<typename T>
    static std::unique_ptr<T> safeCast(std::unique_ptr<TPS25751Register> reg) {
        if (!reg) return nullptr;
        
        // We know the type from factory creation, so static_cast is safe
        T* castedReg = static_cast<T*>(reg.release());
        return std::unique_ptr<T>(castedReg);
    }

    // Template implementation helpers using SFINAE
    template<typename T>
    static std::unique_ptr<T> createImpl() {
        if constexpr (std::is_same_v<T, TPS25751Status>) {
            return createStatus();
        } else if constexpr (std::is_same_v<T, TPS25751Mode>) {
            return createMode();
        } else if constexpr (std::is_same_v<T, TPS25751BootFlags>) {
            return createBootFlags();
        } else if constexpr (std::is_same_v<T, TPS25751PowerPathStatus>) {
            return createPowerPathStatus();
        } else if constexpr (std::is_same_v<T, TPS25751PortConfig>) {
            return createPortConfig();
        } else {
            // This will only be instantiated if T doesn't match any known type
            static_assert(std::is_same_v<T, void>, "Unsupported register type");
            return nullptr;
        }
    }
    
    template<typename T>
    static std::unique_ptr<T> createImpl(const uint8_t* data, size_t length) {
        if constexpr (std::is_same_v<T, TPS25751Status>) {
            return createStatus(data, length);
        } else if constexpr (std::is_same_v<T, TPS25751Mode>) {
            return createMode(data, length);
        } else if constexpr (std::is_same_v<T, TPS25751BootFlags>) {
            return createBootFlags(data, length);
        } else if constexpr (std::is_same_v<T, TPS25751PowerPathStatus>) {
            return createPowerPathStatus(data, length);
        } else if constexpr (std::is_same_v<T, TPS25751PortConfig>) {
            return createPortConfig(data, length);
        } else {
            // This will only be instantiated if T doesn't match any known type
            static_assert(std::is_same_v<T, void>, "Unsupported register type");
            return nullptr;
        }
    }
};
*/

/**
 * @brief Fluent interface for register operations
 */
/*class TPS25751RegisterOperation {
public:
    explicit TPS25751RegisterOperation(std::unique_ptr<TPS25751Register> reg) : register_(std::move(reg)) {}
    
    /**
     * @brief Add validation to the operation
     #1#
    TPS25751RegisterOperation& validate() {
        if (register_ && !register_->isSemanticallyValid()) {
            register_.reset();
        }
        return *this;
    }
    
    /**
     * @brief Add debug output
     #1#
    TPS25751RegisterOperation& debug(Stream& s = Serial) {
        if (register_) {
            register_->debugPrint(s);
        }
        return *this;
    }
    
    /**
     * @brief Get the result
     #1#
    std::unique_ptr<TPS25751Register> get() && {
        return std::move(register_);
    }
    
    /**
     * @brief Cast to specific type
     #1#
    template<typename T>
    std::unique_ptr<T> as() && {
        if (!register_) return nullptr;
        
        // Since we control factory creation, static_cast is safe
        T* castedReg = static_cast<T*>(register_.release());
        return std::unique_ptr<T>(castedReg);
    }
    
    /**
     * @brief Check if operation was successful
     #1#
    explicit operator bool() const {
        return register_ != nullptr;
    }

private:
    std::unique_ptr<TPS25751Register> register_;
};*/