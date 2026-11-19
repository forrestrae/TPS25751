#pragma once

#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"

/**
 * @file TPS25751ReceivedSOPIdentity.h
 * @brief RECEIVED_SOP_IDENTITY register (0x48) class implementation
 *
 * RECEIVED_SOP_IDENTITY_DATA_OBJECT holds the latest Discover Identity response
 * the controller received over USB PD using SOP. It carries a response type, a
 * count of valid VDOs (at most 6), and the VDOs themselves as raw 32-bit words.
 *
 * The class decodes the first three VDOs per USB PD R3.3: the ID Header VDO
 * (Table 6.34), the Cert Stat VDO (Table 6.38), and the Product VDO
 * (Table 6.39). The context-specific fourth to sixth VDOs stay raw. A typed value
 * means something only when responseType() is Ack and numberValidVdos() is greater
 * than the VDO's zero-based index (ID Header 0, Cert Stat 1, Product 2).
 *
 * The register is filled when the controller sends Discover Identity.
 * PORT_CONTROL Automatic ID Request (0x29 bit 16, see
 * TPS25751PortControl::automaticIdRequest()) makes the controller send it
 * automatically. INT_EVENT1 bit 51 (discoverModeCompleted()) marks a later
 * discovery step and is not a signal that this register was updated. Response
 * Type is the authoritative status.
 *
 * Register Details:
 * - Address: 0x48
 * - Size: 25 bytes (200 bits)
 * - Access: Read-only (no setters, no write path, no kAddress)
 * - Reset: all zero
 * - Byte 0: Number Valid VDOs (bits 2:0), reserved (5:3), Response Type (7:6)
 * - Bytes 1..24: VDO 1 to VDO 6, little-endian, 4 bytes each
 *
 * @code
 * auto sop = tps.readReceivedSOPIdentityRegister();
 * if (sop && sop->responseType() == TPS25751ReceivedSOPIdentity::ResponseType::Ack) {
 *     uint32_t idHeader = sop->idHeaderVdo();
 * }
 * @endcode
 *
 * @see TI TPS25751 Technical Reference Manual SLVUCR8B (Rev. B), p.47
 * @see TPS25751Register
 * @see TPS25751
 */
class TPS25751ReceivedSOPIdentity : public TPS25751Register
{
public:
    /// Type of response received (bits 7:6)
    enum class ResponseType : uint8_t {
        NotSentOrPending = 0,  ///< 0h - SOP Discover Identity REQ not sent or pending
        Ack = 1,               ///< 1h - Responder ACK received
        NakOrTimeout = 2,      ///< 2h - Responder NAK received or response timeout
        Busy = 3               ///< 3h - Responder BUSY received
    };

    /**
     * @brief Product Type (UFP), ID Header VDO bits 29:27 (USB PD R3.3 Table 6.34, SOP)
     *
     * Codes 4, 6, and 7 are invalid and the receiver should ignore the field.
     * productTypeUfp() returns them cast into this enum; toString() prints
     * "Invalid".
     */
    enum class ProductTypeUfp : uint8_t {
        NotUfp = 0,           ///< 000b - Not a UFP
        PdusbHub = 1,         ///< 001b - PDUSB Hub
        PdusbPeripheral = 2,  ///< 010b - PDUSB Peripheral
        Psd = 3,              ///< 011b - PSD
        Ama = 5               ///< 101b - Alternate Mode Adapter (AMA); deprecated by the specification
    };

    /**
     * @brief Product Type (DFP), ID Header VDO bits 25:23 (USB PD R3.3 Table 6.34, SOP)
     *
     * Codes 5 to 7 are invalid and shall not be used. productTypeDfp() returns
     * them cast into this enum; toString() prints "Invalid".
     */
    enum class ProductTypeDfp : uint8_t {
        NotDfp = 0,                  ///< 000b - Not a DFP
        PdusbHub = 1,                ///< 001b - PDUSB Hub
        PdusbHost = 2,               ///< 010b - PDUSB Host
        PowerBrick = 3,              ///< 011b - Power Brick
        AlternateModeController = 4  ///< 100b - Alternate Mode Controller (AMC)
    };

    /**
     * @brief Connector Type, ID Header VDO bits 22:21 (USB PD R3.3 Table 6.34)
     *
     * Code 1 is invalid and shall not be used. connectorType() returns it cast
     * into this enum; toString() prints "Invalid".
     */
    enum class ConnectorType : uint8_t {
        Unknown = 0,          ///< 00b - Unknown connector type; deprecated by the specification
        TypeCReceptacle = 2,  ///< 10b - USB Type-C receptacle
        TypeCPlug = 3         ///< 11b - USB Type-C plug
    };

    /// Maximum number of VDOs the register holds
    static constexpr uint8_t kMaxVdos = 6;

    explicit TPS25751ReceivedSOPIdentity() : TPS25751Register(TPS25751Registers::Registers::RECEIVED_SOP_IDENTITY.size) {}
    explicit TPS25751ReceivedSOPIdentity(const uint8_t *data) : TPS25751Register(data, TPS25751Registers::Registers::RECEIVED_SOP_IDENTITY.size) {}
    TPS25751ReceivedSOPIdentity(const uint8_t *data, const size_t len) : TPS25751Register(data, len) {}
    explicit TPS25751ReceivedSOPIdentity(TPS25751Registers::RegisterInfo regInfo) : TPS25751Register(regInfo.size) {}

    /// @brief Number Valid VDOs (bits 2:0). The TRM maximum is 6.
    uint8_t numberValidVdos() const;

    /// @brief Response Type (bits 7:6)
    ResponseType responseType() const;

    /**
     * @brief Raw 32-bit VDO by zero-based index
     *
     * Index 0 is the TRM's "RX ID SOP VDO 1" (bits 39:8), index 1 is VDO 2
     * (bits 71:40), and so on up to index 5, VDO 6 (bits 199:168). Returns 0 for
     * index 6 and above. The word is returned whether or not the VDO is within
     * numberValidVdos(); the TRM does not say that unused VDOs read zero.
     */
    uint32_t vdo(uint8_t index) const;

    /// @brief ID Header VDO, TRM "RX ID SOP VDO 1"; same as vdo(0)
    uint32_t idHeaderVdo() const { return vdo(0); }
    /// @brief Cert Stat VDO, TRM "RX ID SOP VDO 2"; same as vdo(1)
    uint32_t certStatVdo() const { return vdo(1); }
    /// @brief Product VDO, TRM "RX ID SOP VDO 3"; same as vdo(2)
    uint32_t productVdo() const { return vdo(2); }

    // --- ID Header VDO (vdo(0)), USB PD R3.3 Table 6.34 ---------------------------

    /// @brief Communications Capable as USB Host, ID Header bit 31 (R3.3 Table 6.34)
    bool usbHostCapable() const { return ((idHeaderVdo() >> 31) & 0x1u) != 0; }
    /// @brief Communications Capable as USB Device, ID Header bit 30 (R3.3 Table 6.34)
    bool usbDeviceCapable() const { return ((idHeaderVdo() >> 30) & 0x1u) != 0; }
    /// @brief Product Type (UFP), ID Header bits 29:27; invalid codes 4, 6, 7 pass through (R3.3 Table 6.34)
    ProductTypeUfp productTypeUfp() const { return static_cast<ProductTypeUfp>(rawProductTypeUfp()); }
    /// @brief Raw Product Type (UFP) code, ID Header bits 29:27
    uint8_t rawProductTypeUfp() const { return static_cast<uint8_t>((idHeaderVdo() >> 27) & 0x7u); }
    /// @brief Alternate Modes Supported, ID Header bit 26 (R3.3 Table 6.34)
    bool alternateModesSupported() const { return ((idHeaderVdo() >> 26) & 0x1u) != 0; }
    /// @brief Product Type (DFP), ID Header bits 25:23; invalid codes 5 to 7 pass through (R3.3 Table 6.34)
    ProductTypeDfp productTypeDfp() const { return static_cast<ProductTypeDfp>(rawProductTypeDfp()); }
    /// @brief Raw Product Type (DFP) code, ID Header bits 25:23
    uint8_t rawProductTypeDfp() const { return static_cast<uint8_t>((idHeaderVdo() >> 23) & 0x7u); }
    /// @brief Connector Type, ID Header bits 22:21; invalid code 1 passes through (R3.3 Table 6.34)
    ConnectorType connectorType() const { return static_cast<ConnectorType>(rawConnectorType()); }
    /// @brief Raw Connector Type code, ID Header bits 22:21
    uint8_t rawConnectorType() const { return static_cast<uint8_t>((idHeaderVdo() >> 21) & 0x3u); }
    /// @brief USB Vendor ID assigned by the USB-IF, ID Header bits 15:0 (R3.3 Table 6.34)
    uint16_t usbVendorId() const { return static_cast<uint16_t>(idHeaderVdo() & 0xFFFFu); }

    // --- Cert Stat VDO (vdo(1)), USB PD R3.3 Table 6.38 ----------------------------

    /// @brief XID assigned by the USB-IF, Cert Stat bits 31:0 (R3.3 Table 6.38)
    uint32_t xid() const { return certStatVdo(); }

    // --- Product VDO (vdo(2)), USB PD R3.3 Table 6.39 ------------------------------

    /// @brief USB Product ID, Product VDO bits 31:16 (R3.3 Table 6.39)
    uint16_t usbProductId() const { return static_cast<uint16_t>(productVdo() >> 16); }
    /// @brief bcdDevice (device version assigned by the vendor), Product VDO bits 15:0 (R3.3 Table 6.39)
    uint16_t bcdDevice() const { return static_cast<uint16_t>(productVdo() & 0xFFFFu); }

    /**
     * @brief Print the response type, the count, and the valid VDOs as hex,
     *        then the typed decode of the ID Header, Cert Stat, and Product VDOs
     *
     * The typed lines appear only when the Response Type is Ack and the VDO is
     * within numberValidVdos(); otherwise one line says the identity is not valid.
     * @param s Output stream (defaults to Serial)
     */
    void debugPrint(Stream &s = Serial) const override;

    /**
     * @brief Semantic validation
     *
     * Base checks (valid buffer, not the all-0xFF bus-error signature, expected
     * size) plus: Number Valid VDOs is at most 6 and reserved bits 5:3 read 0.
     * VDOs beyond the count and the count against the Response Type are not
     * checked, because the TRM states neither rule.
     */
    bool isSemanticallyValid() const override;

    static constexpr uint8_t kReservedMask = 0x38;  ///< byte 0 bits 5:3
};

/// @brief TRM text for a Response Type value
const char *toString(TPS25751ReceivedSOPIdentity::ResponseType val);

/// @brief USB PD R3.3 Table 6.34 text for a Product Type (UFP) value ("Invalid" for codes 4, 6, 7)
const char *toString(TPS25751ReceivedSOPIdentity::ProductTypeUfp val);
/// @brief USB PD R3.3 Table 6.34 text for a Product Type (DFP) value ("Invalid" for codes 5 to 7)
const char *toString(TPS25751ReceivedSOPIdentity::ProductTypeDfp val);
/// @brief USB PD R3.3 Table 6.34 text for a Connector Type value ("Invalid" for code 1)
const char *toString(TPS25751ReceivedSOPIdentity::ConnectorType val);
