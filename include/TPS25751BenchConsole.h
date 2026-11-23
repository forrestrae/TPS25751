#pragma once

/**
 * @file TPS25751BenchConsole.h
 * @brief Serial bench console for a TPS25751: register reads, 4CC tasks, restarts
 *
 * The console answers the line protocol of TPS25751BenchProtocol on any Stream,
 * so a host program can read registers, take the cross-check snapshot, run the
 * allowed 4CC tasks, and restart the controller without new firmware for each
 * experiment. The host side is the `tps25751-pd-bench` MCP server in
 * tools/pd-bench.
 *
 * TPS25751.h does not include this header, so firmware that does not use the
 * console does not compile it in. Firmware that wants it adds three lines:
 *
 * @code
 * #include <TPS25751BenchConsole.h>
 *
 * TPS25751 pd;
 * TPS25751BenchConsole bench(pd, SerialUSB1);   // any Stream
 *
 * void loop() {
 *     bench.poll();
 * }
 * @endcode
 *
 * A board without a configuration EEPROM also hands over its patch bundle, so
 * the console can reload it after a cold restart:
 *
 * @code
 * TPS25751BenchConsole bench(pd, SerialUSB1, tps25751_lowRegion_i2c_array, SIZEOFLRB);
 * @endcode
 *
 * This is not a register class: it does not inherit TPS25751Register and is not
 * part of the register factory.
 */

#include <Arduino.h>
#include <Stream.h>
#include "TPS25751.h"
#include "TPS25751BenchProtocol.h"

/**
 * @brief Binds the bench protocol to a TPS25751 and a Stream
 *
 * poll() reads request lines from the stream and answers them. A command runs
 * to completion inside poll(): a register read takes a few milliseconds, a PD
 * message task up to its timeout, and a restart with a patch reload several
 * seconds. Call poll() from loop() and keep interrupt handlers off the
 * console's stream.
 *
 * Only `RESTART`, `CMD`, and `WR` change the controller, and `SNAP` clears the
 * INT_EVENT1 bits it reports. `HELLO`, `RD`, and `DEC` read only.
 *
 * The console issues every 4CC task through TPS25751::executeCommand() and
 * loads the patch bundle with TPS25751PatchLoader::loadPatchBundle().
 */
class TPS25751BenchConsole {
public:
    /// Longest wait for the controller to answer again after `Gaid` or `GAID`.
    /// The controller spends about 1 s in Type-C Error Recovery before it
    /// restarts (SLVUCR8B p.62).
    static constexpr uint32_t kRestartTimeoutMs = 10000;

    /// How long the console polls COMMAND after writing `Gaid` or `GAID`. The
    /// task "never completes because the processor restarts" (SLVUCR8B p.62),
    /// so the console only confirms that the controller took the code.
    static constexpr uint32_t kRestartIssueTimeoutMs = 100;

    /// Interval between polls while the console waits for a restart.
    static constexpr uint32_t kRestartPollMs = 20;

    /// Longest wait for a controller in BOOT or PTCH mode to reach APP mode on
    /// its own (a load from EEPROM) before the console hands over to the patch
    /// loader. PTCH with INT_EVENT1 ReadyForPatch ends the wait early. The
    /// loader then makes its own ReadyForPatch and MODE checks.
    static constexpr uint32_t kPatchReadyTimeoutMs = 3000;

    /**
     * @brief Construct a console
     * @param pd                The controller to drive; must outlive the console
     * @param io                Stream that carries the protocol
     * @param patchBundle       Patch bundle image for a board without a
     *                          configuration EEPROM, or nullptr. The array must
     *                          outlive the console.
     * @param patchBundleLength Length of @p patchBundle in bytes
     */
    TPS25751BenchConsole(TPS25751 &pd, Stream &io,
                         const uint8_t *patchBundle = nullptr, size_t patchBundleLength = 0);

    /**
     * @brief Set or clear the patch bundle
     * @param image  Patch bundle image, or nullptr for none
     * @param length Length of @p image in bytes
     */
    void setPatchBundle(const uint8_t *image, size_t length);

    /// @return true if a patch bundle is set
    bool hasPatchBundle() const;

    /**
     * @brief Send a one-line summary of each request to a second stream
     * @param log Stream for the summaries, or nullptr for none (the default)
     */
    void setLogStream(Stream *log);

    /**
     * @brief Announce the console and bring the controller to APP mode
     *
     * Optional. Prints the event `! READY proto=1`. When MODE reads PTCH and a
     * patch bundle is set, loads the bundle and reports the outcome as a
     * `! PATCH` event. Call it once after Wire and the stream are started.
     *
     * begin() and `RESTART` are the only places that load the bundle. `HELLO`
     * reads only: for a controller in PTCH mode it reports `mode=PTCH` and
     * `patch_pending=1`. begin() does not read the stream, so a request sent
     * while it runs is answered afterwards; with the wait for PTCH readiness
     * and the patch load that can be several seconds after a reset.
     */
    void begin();

    /**
     * @brief Read pending input and answer every complete request line
     *
     * Returns at once when the stream has no input. A line longer than
     * TPS25751BenchProtocol::kMaxLineLength is answered with `ERR TOOLONG`,
     * and a line that contains a NUL byte with `ERR SYNTAX`.
     */
    void poll();

    /**
     * @brief Answer one request line
     *
     * poll() calls this for each line it reads. It is public for firmware that
     * reads lines itself and for the unit tests.
     *
     * @param line Null-terminated request line without the line feed
     */
    void handleLine(const char *line);

private:
    /// One register as read from the controller.
    struct RawRegister {
        uint8_t lengthByte = 0;   ///< The length byte the controller returned
        uint8_t dataLength = 0;   ///< Number of valid bytes in data
        uint8_t data[TPS25751Registers::Registers::DATA.size] = {0};
    };

    /// What bringing the controller to APP mode did.
    struct PatchReport {
        bool i2cFailed = false;    ///< MODE could not be read
        bool noBundle = false;     ///< PTCH mode and no patch bundle set
        bool attempted = false;    ///< loadPatchBundle() ran
        PatchLoadStatus status = PatchLoadStatus::Success;
        char mode[5] = {'?', 0, 0, 0, 0};  ///< MODE at the end, without trailing spaces
    };

    void handleHello(const TPS25751BenchRequest &request);
    void handleRestart(const char *id, bool cold);
    void handleRead(const TPS25751BenchRequest &request);
    void handleDecode(const TPS25751BenchRequest &request);
    void handleSnapshot(const TPS25751BenchRequest &request);
    void handleTask(const TPS25751BenchRequest &request);
    void handleWrite(const TPS25751BenchRequest &request);

    /// Read a register and keep its length byte. Returns false when the
    /// controller does not answer or reports a length over 64 bytes.
    bool readRaw(uint8_t address, RawRegister &out) const;

    /// Read MODE into a 5-character buffer without trailing spaces.
    bool readMode(char mode[5]) const;

    /// Bring the controller to APP mode, loading the patch bundle when MODE
    /// reads PTCH. Returns true when MODE ends as APP.
    bool ensureAppMode(PatchReport &report);

    /// Refuse the line in the buffer without parsing it as a request: a
    /// reply when the line starts with a valid id, otherwise an event.
    void rejectLine(TPS25751BenchError error, const __FlashStringHelper *text);

    /// Print `ok` and finish the request.
    void finishOk(const char *id);

    /// Print a complete error reply and record it for the log line.
    void fail(const char *id, TPS25751BenchError error, const __FlashStringHelper *text);

    /// Start an error reply the caller completes with println().
    void beginFail(const char *id, TPS25751BenchError error);

    void printDecode(const char *id, uint8_t address, const RawRegister &raw);
    void logRequest(const char *line, uint32_t startedMs);

    TPS25751 &_pd;
    Stream &_io;
    Stream *_log = nullptr;
    const uint8_t *_patchBundle;
    size_t _patchBundleLength;

    char _line[TPS25751BenchProtocol::kMaxLineLength + 1] = {0};
    size_t _lineLength = 0;
    bool _lineOverflow = false;
    bool _lineHasNul = false;
    TPS25751BenchError _lastError = TPS25751BenchError::None;
};
