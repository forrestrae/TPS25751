#ifndef TPS25751DOWNSTREAMDEVICE_H
#define TPS25751DOWNSTREAMDEVICE_H

#include <cstdint>
#include <cstddef>

#include "TPS25751.h"
#include "TPS25751Task.h"

/**
 * @brief Proxy for a device on the TPS25751's secondary I2Cc bus (TRM Sec 4.4.2/4.4.3)
 *
 * The TPS25751 acts as an I2C controller on a second bus (I2Cc_SDA/SCL), reaching
 * downstream parts (e.g. a BQ25798 charger) on the host's behalf. Access is proxied
 * through the generic 4CC task interface (TPS25751::executeCommand()) using the
 * **I2Cr** (read) and **I2Cw** (write) task codes, which carry a target device
 * address plus register offset/length/payload bytes in the DATA payload.
 *
 * This class is, for a device on I2Cc, what TPS25751 is for its own registers: it
 * owns no I2C bus directly (all transactions are relayed through the host's
 * executeCommand()) and exposes the same read/typed-read shape so that downstream
 * device register classes can extend the existing device-agnostic decoder
 * TPS25751Register exactly like the 15 TPS25751 register classes do.
 *
 * ## DATA payload layouts (TRM Sec 4.4.2/4.4.3, DATA is 0-indexed; "Byte 1" = DATA[0])
 *
 * - **I2Cr input** (3 B): DATA[0]=target addr (7-bit, bit7 reserved=0), DATA[1]=register
 *   offset, DATA[2]=NumBytes to read. **Output:** DATA[0]=task return code,
 *   DATA[1..NumBytes]=bytes read (read data starts at **offset 1**; max 63 read bytes,
 *   since DATA[0] is the return code in the 64-byte DATA register).
 * - **I2Cw input** (3 + payload B, TRM Table 5-25): DATA[0]=target addr (7-bit),
 *   DATA[1]=Length, DATA[2]=register offset, DATA[3..]=payload (DATA bytes 4-14,
 *   max 11 bytes). The controller transmits exactly Length bytes starting at the
 *   register-offset byte, so **Length counts the offset byte + payload (= len + 1)**
 *   -- with Length == payload-len it does a pointer-only write that leaves the
 *   register unchanged. NOTE the byte order differs from I2Cr {addr, regOffset,
 *   numBytes}: I2Cw swaps length/offset and its count includes the offset byte.
 *   **Output:** DATA[0]=task return code (success only means the write was *queued*,
 *   not necessarily completed on the wire -- confirm with an I2Cr read).
 *
 * ## TRM 5-second spacing constraint
 *
 * Per the TRM, consecutive I2Cr commands -- and, separately, consecutive I2Cw
 * commands -- must be issued at least 5 seconds apart. This is *host pacing*
 * guidance, distinct from executeCommand()'s much shorter (~200 ms) completion
 * poll. This class tracks the last I2Cr/I2Cw issue time via millis() and emits a
 * DEBUG_CAT_TASK warning if a call arrives sooner than that -- it does **not**
 * hard-block the call, since the caller may already be managing the timing
 * (e.g. across multiple downstream-device instances sharing one host).
 *
 * ## Direct (non-proxied) transport
 *
 * For bench bring-up, validation, or boards where the downstream part is wired
 * straight to the MCU's own I2C bus (rather than behind the TPS25751 on I2Cc),
 * the alternate `TPS25751DownstreamDevice(TwoWire&, addr)` constructor selects a
 * **direct** transport. In that mode reads/writes are plain Arduino-`Wire`
 * register transactions against the part -- no 4CC relay, so none of the
 * proxy-path constraints apply: there is **no** TRM 5 s spacing and **no** I2Cr/
 * I2Cw payload cap (63 B read / 11 B write); the only limit is the `TwoWire`
 * buffer. The transport is fixed at construction; the typed-read shape and every
 * caller above readRegister()/writeRegister() are identical either way.
 */
class TPS25751DownstreamDevice
{
public:
    /**
     * @brief Construct a proxy for a device at @p deviceAddress on the host's I2Cc bus
     * @param host          TPS25751 instance whose executeCommand() relays I2Cr/I2Cw tasks
     * @param deviceAddress 7-bit I2C address of the downstream device (bit 7 is masked off)
     */
    TPS25751DownstreamDevice(const TPS25751& host, uint8_t deviceAddress);

    /**
     * @brief Construct a driver that talks to the device **directly** on @p wire
     *
     * Selects the direct (non-proxied) transport: readRegister()/writeRegister()
     * issue plain Arduino-`Wire` register transactions against @p deviceAddress on
     * @p wire, with no 4CC relay through a TPS25751. None of the proxy-path
     * constraints apply (no TRM 5 s spacing, no 63/11-byte payload caps). Intended
     * for bench/bring-up where the part is on the MCU's own bus.
     *
     * @param wire          I2C bus the downstream device is wired to (caller calls begin())
     * @param deviceAddress 7-bit I2C address of the downstream device (bit 7 is masked off)
     */
    TPS25751DownstreamDevice(TwoWire& wire, uint8_t deviceAddress);

    /**
     * @brief Read @p len bytes from downstream register @p devReg via I2Cr
     * @param devReg Register offset on the downstream device
     * @param buf    Destination buffer, at least @p len bytes
     * @param len    Number of bytes to read (max 63)
     * @return true if the task completed successfully and the bytes were copied
     */
    bool readRegister(uint8_t devReg, uint8_t* buf, size_t len) const;

    /**
     * @brief Write @p len bytes to downstream register @p devReg via I2Cw
     * @param devReg Register offset on the downstream device
     * @param data   Payload to write
     * @param len    Number of bytes to write (max 11 per TRM Table 5-25)
     * @return true if the task completed and reported success (queued); does not
     *         guarantee the write completed on the downstream wire
     */
    bool writeRegister(uint8_t devReg, const uint8_t* data, size_t len) const;

    /**
     * @brief Typed read: decode @p len bytes from @p devReg into a TPS25751Register-derived T
     *
     * Mirrors TPS25751::readRegister<T>(). T must be constructible as T(const uint8_t*, size_t),
     * the same convention used by every existing register class.
     *
     * @tparam T     Decoder type, must derive from TPS25751Register
     * @param devReg Register offset on the downstream device
     * @param out    Decoded output object
     * @param len    Number of raw bytes to read and decode (max 63)
     * @return true if the underlying read succeeded
     */
    template<typename T>
    bool readRegister(uint8_t devReg, T& out, size_t len) const
    {
        uint8_t buf[64];
        if (len > sizeof(buf) || !readRegister(devReg, buf, len)) {
            return false;
        }
        out = T(buf, len);
        return true;
    }

private:
    const TPS25751* _host;   ///< Proxied transport host; nullptr in direct mode
    TwoWire* _wire;          ///< Direct transport bus; nullptr in proxied mode
    bool _direct;            ///< true: direct Wire transport; false: I2Cr/I2Cw proxy
    uint8_t _deviceAddress;
    mutable uint32_t _lastI2CrMs;
    mutable uint32_t _lastI2CwMs;

    /// Minimum spacing the TRM requires between consecutive same-type (I2Cr or I2Cw) commands
    static constexpr uint32_t kMinCommandSpacingMs = 5000;

    /**
     * @brief Check elapsed time against kMinCommandSpacingMs and warn (not block) if too soon
     * @param lastMs   Reference to the tracked timestamp of the last same-type command (updated)
     * @param taskName "I2Cr" or "I2Cw", for the warning message
     */
    void enforceSpacing(uint32_t& lastMs, const char* taskName) const;

    /// Proxied transport: relay the read/write through the host's I2Cr/I2Cw 4CC tasks.
    bool readProxied(uint8_t devReg, uint8_t* buf, size_t len) const;
    bool writeProxied(uint8_t devReg, const uint8_t* data, size_t len) const;

    /// Direct transport: plain Arduino-`Wire` register read/write against the part.
    bool readDirect(uint8_t devReg, uint8_t* buf, size_t len) const;
    bool writeDirect(uint8_t devReg, const uint8_t* data, size_t len) const;
};

#endif // TPS25751DOWNSTREAMDEVICE_H
