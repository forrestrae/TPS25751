#pragma once

#include <Stream.h>

// Debug level configuration.
//
// Two independent knobs:
//   DEBUG_LEVEL      - the *runtime* default: the initial value of
//                      TPS25751Debug::currentDebugLevel before any call to
//                      setDebugLevel(). Defaults to 0 (silent).
//   DEBUG_LEVEL_MAX  - the *compile-time ceiling*: messages above this level
//                      are stripped from the binary entirely (the TPS_DEBUG_*
//                      macros expand to nothing). Defaults to TRACE so that
//                      every level is compiled in and runtime setDebugLevel()
//                      can enable any of them without a build flag.
//
// To enable debug output at runtime, just call TPS25751::setDebugLevel(level)
// - no build flag required. To strip debug code for size (e.g. release builds),
// set -DDEBUG_LEVEL_MAX=0 (or any level below which messages are removed).
#ifndef DEBUG_LEVEL
#define DEBUG_LEVEL 0
#endif

// Debug categories - bitfield for selective debugging
#define DEBUG_CAT_NONE        0x00
#define DEBUG_CAT_I2C         0x01  // I2C communication
#define DEBUG_CAT_REGISTER    0x02  // Register operations
#define DEBUG_CAT_VALIDATION  0x04  // Validation failures
#define DEBUG_CAT_PARSING     0x08  // Data parsing/extraction
#define DEBUG_CAT_TASK        0x10  // 4CC command-task execution / I2Cc downstream-device proxy
#define DEBUG_CAT_ALL         0xFF

// Current debug categories - can be configured at runtime
#ifndef DEBUG_CATEGORIES
#define DEBUG_CATEGORIES DEBUG_CAT_ALL
#endif

// Debug levels
#define DEBUG_LEVEL_NONE      0
#define DEBUG_LEVEL_ERROR     1
#define DEBUG_LEVEL_WARN      2
#define DEBUG_LEVEL_INFO      3
#define DEBUG_LEVEL_DEBUG     4
#define DEBUG_LEVEL_TRACE     5

// Compile-time ceiling. Defaults to TRACE so all levels are compiled in and
// fully controllable at runtime via setDebugLevel(). Override (e.g.
// -DDEBUG_LEVEL_MAX=0) to strip higher-level debug code from the binary.
#ifndef DEBUG_LEVEL_MAX
#define DEBUG_LEVEL_MAX DEBUG_LEVEL_TRACE
#endif

namespace TPS25751Debug {

// Runtime debug configuration
extern Stream* debugStream;
extern uint8_t currentDebugLevel;
extern uint8_t enabledCategories;

// Configuration functions
void setDebugStream(Stream* stream);
void setDebugLevel(uint8_t level);
void setDebugCategories(uint8_t categories);
void enableCategory(uint8_t category);
void disableCategory(uint8_t category);

// Raw data dumping
void hexDump(Stream& s, const uint8_t* data, size_t length, const char* prefix = nullptr);
void hexDumpLine(Stream& s, const uint8_t* data, size_t length);

// Formatted debug output with timestamp and category
void debugPrint(uint8_t level, uint8_t category, const char* format, ...);
void debugPrintF(uint8_t level, uint8_t category, const __FlashStringHelper* format, ...);

// Helper for I2C error reporting
void reportI2CError(uint8_t address, uint8_t registerAddr, const char* operation, int errorCode);

// Helper for validation error reporting
void reportValidationError(const char* registerName, const char* validation, const char* details = nullptr);

} // namespace TPS25751Debug

// Convenience macros for different debug levels (compile-time optimized)
#if DEBUG_LEVEL_MAX >= DEBUG_LEVEL_ERROR
#define TPS_DEBUG_ERROR(cat, ...) TPS25751Debug::debugPrint(DEBUG_LEVEL_ERROR, cat, __VA_ARGS__)
#define TPS_DEBUG_ERROR_F(cat, ...) TPS25751Debug::debugPrintF(DEBUG_LEVEL_ERROR, cat, __VA_ARGS__)
#else
#define TPS_DEBUG_ERROR(cat, ...)
#define TPS_DEBUG_ERROR_F(cat, ...)
#endif

#if DEBUG_LEVEL_MAX >= DEBUG_LEVEL_WARN
#define TPS_DEBUG_WARN(cat, ...) TPS25751Debug::debugPrint(DEBUG_LEVEL_WARN, cat, __VA_ARGS__)
#define TPS_DEBUG_WARN_F(cat, ...) TPS25751Debug::debugPrintF(DEBUG_LEVEL_WARN, cat, __VA_ARGS__)
#else
#define TPS_DEBUG_WARN(cat, ...)
#define TPS_DEBUG_WARN_F(cat, ...)
#endif

#if DEBUG_LEVEL_MAX >= DEBUG_LEVEL_INFO
#define TPS_DEBUG_INFO(cat, ...) TPS25751Debug::debugPrint(DEBUG_LEVEL_INFO, cat, __VA_ARGS__)
#define TPS_DEBUG_INFO_F(cat, ...) TPS25751Debug::debugPrintF(DEBUG_LEVEL_INFO, cat, __VA_ARGS__)
#else
#define TPS_DEBUG_INFO(cat, ...)
#define TPS_DEBUG_INFO_F(cat, ...)
#endif

#if DEBUG_LEVEL_MAX >= DEBUG_LEVEL_DEBUG
#define TPS_DEBUG(cat, ...) TPS25751Debug::debugPrint(DEBUG_LEVEL_DEBUG, cat, __VA_ARGS__)
#define TPS_DEBUG_F(cat, ...) TPS25751Debug::debugPrintF(DEBUG_LEVEL_DEBUG, cat, __VA_ARGS__)
#else
#define TPS_DEBUG(cat, ...)
#define TPS_DEBUG_F(cat, ...)
#endif

#if DEBUG_LEVEL_MAX >= DEBUG_LEVEL_TRACE
#define TPS_DEBUG_TRACE(cat, ...) TPS25751Debug::debugPrint(DEBUG_LEVEL_TRACE, cat, __VA_ARGS__)
#define TPS_DEBUG_TRACE_F(cat, ...) TPS25751Debug::debugPrintF(DEBUG_LEVEL_TRACE, cat, __VA_ARGS__)
#else
#define TPS_DEBUG_TRACE(cat, ...)
#define TPS_DEBUG_TRACE_F(cat, ...)
#endif

// Hex dump macros
#if DEBUG_LEVEL_MAX >= DEBUG_LEVEL_DEBUG
#define TPS_DEBUG_HEXDUMP(cat, data, len, prefix) \
  do { \
    if ((TPS25751Debug::currentDebugLevel >= DEBUG_LEVEL_DEBUG) && \
        (TPS25751Debug::enabledCategories & (cat))) { \
      TPS25751Debug::hexDump(*TPS25751Debug::debugStream, data, len, prefix); \
    } \
  } while(0)
#else
#define TPS_DEBUG_HEXDUMP(cat, data, len, prefix)
#endif

// I2C and validation helpers
#define TPS_REPORT_I2C_ERROR(addr, reg, op, err) TPS25751Debug::reportI2CError(addr, reg, op, err)
#define TPS_REPORT_VALIDATION_ERROR(reg, val, details) TPS25751Debug::reportValidationError(reg, val, details)