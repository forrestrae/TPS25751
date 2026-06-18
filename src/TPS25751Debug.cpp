#include "TPS25751Debug.h"

#include <Arduino.h>

namespace TPS25751Debug {

// Global debug configuration
Stream* debugStream = &Serial;
uint8_t currentDebugLevel = DEBUG_LEVEL;
uint8_t enabledCategories = DEBUG_CATEGORIES;

void setDebugStream(Stream* stream) {
    if (stream) {
        debugStream = stream;
    }
}

void setDebugLevel(uint8_t level) {
    currentDebugLevel = level;
}

void setDebugCategories(uint8_t categories) {
    enabledCategories = categories;
}

void enableCategory(uint8_t category) {
    enabledCategories |= category;
}

void disableCategory(uint8_t category) {
    enabledCategories &= ~category;
}

const char* getLevelString(uint8_t level) {
    switch (level) {
        case DEBUG_LEVEL_ERROR: return "ERROR";
        case DEBUG_LEVEL_WARN:  return "WARN ";
        case DEBUG_LEVEL_INFO:  return "INFO ";
        case DEBUG_LEVEL_DEBUG: return "DEBUG";
        case DEBUG_LEVEL_TRACE: return "TRACE";
        default:                return "?????";
    }
}

const char* getCategoryString(uint8_t category) {
    switch (category) {
        case DEBUG_CAT_I2C:        return "I2C";
        case DEBUG_CAT_REGISTER:   return "REG";
        case DEBUG_CAT_VALIDATION: return "VAL";
        case DEBUG_CAT_PARSING:    return "PAR";
        default:                   return "???";
    }
}

void debugPrint(uint8_t level, uint8_t category, const char* format, ...) {
    // Check if this debug message should be output
    if (level > currentDebugLevel || !(enabledCategories & category) || !debugStream) {
        return;
    }
    
    // Print timestamp and level info
    debugStream->print(F("["));
    debugStream->print(millis());
    debugStream->print(F("] "));
    debugStream->print(getLevelString(level));
    debugStream->print(F(" "));
    debugStream->print(getCategoryString(category));
    debugStream->print(F(": "));
    
    // Format and print the message
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    debugStream->println(buffer);
}

void debugPrintF(uint8_t level, uint8_t category, const __FlashStringHelper* format, ...) {
    // Check if this debug message should be output
    if (level > currentDebugLevel || !(enabledCategories & category) || !debugStream) {
        return;
    }
    
    // Print timestamp and level info
    debugStream->print(F("["));
    debugStream->print(millis());
    debugStream->print(F("] "));
    debugStream->print(getLevelString(level));
    debugStream->print(F(" "));
    debugStream->print(getCategoryString(category));
    debugStream->print(F(": "));
    
    // Format and print the message (Flash string version)
    char buffer[256];
    char formatBuffer[128];
    
    // Copy format string from flash to RAM
    strncpy_P(formatBuffer, (const char*)format, sizeof(formatBuffer) - 1);
    formatBuffer[sizeof(formatBuffer) - 1] = '\0';
    
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), formatBuffer, args);
    va_end(args);
    
    debugStream->println(buffer);
}

void hexDump(Stream& s, const uint8_t* data, size_t length, const char* prefix) {
    if (!data || length == 0) return;
    
    if (prefix) {
        s.print(prefix);
        s.print(F(": "));
    }
    
    // Print full hex dump with ASCII representation
    for (size_t i = 0; i < length; i += 16) {
        // Print address
        s.print(F("0x"));
        if (i < 0x10) s.print(F("0"));
        s.print(i, HEX);
        s.print(F(": "));
        
        // Print hex bytes
        size_t lineEnd = (i + 16 < length) ? i + 16 : length;
        for (size_t j = i; j < lineEnd; j++) {
            if (data[j] < 0x10) s.print(F("0"));
            s.print(data[j], HEX);
            s.print(F(" "));
        }
        
        // Pad remaining space for alignment
        for (size_t j = lineEnd; j < i + 16; j++) {
            s.print(F("   "));
        }
        
        // Print ASCII representation
        s.print(F(" |"));
        for (size_t j = i; j < lineEnd; j++) {
            char c = data[j];
            if (c >= 32 && c <= 126) {
                s.print(c);
            } else {
                s.print(F("."));
            }
        }
        s.println(F("|"));
    }
}

void hexDumpLine(Stream& s, const uint8_t* data, size_t length) {
    if (!data || length == 0) return;
    
    for (size_t i = 0; i < length; i++) {
        if (i > 0) s.print(F(" "));
        if (data[i] < 0x10) s.print(F("0"));
        s.print(data[i], HEX);
    }
}

void reportI2CError(uint8_t address, uint8_t registerAddr, const char* operation, int errorCode) {
    if (currentDebugLevel >= DEBUG_LEVEL_ERROR && (enabledCategories & DEBUG_CAT_I2C)) {
        debugPrint(DEBUG_LEVEL_ERROR, DEBUG_CAT_I2C, 
                  "I2C %s failed: addr=0x%02X, reg=0x%02X, error=%d", 
                  operation, address, registerAddr, errorCode);
    }
}

void reportValidationError(const char* registerName, const char* validation, const char* details) {
    if (currentDebugLevel >= DEBUG_LEVEL_WARN && (enabledCategories & DEBUG_CAT_VALIDATION)) {
        if (details) {
            debugPrint(DEBUG_LEVEL_WARN, DEBUG_CAT_VALIDATION,
                      "%s validation failed: %s (%s)", 
                      registerName, validation, details);
        } else {
            debugPrint(DEBUG_LEVEL_WARN, DEBUG_CAT_VALIDATION,
                      "%s validation failed: %s", 
                      registerName, validation);
        }
    }
}

} // namespace TPS25751Debug