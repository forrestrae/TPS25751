#include <Stream.h>

#include "TPS25751ReceivedSOPIdentity.h"

uint8_t TPS25751ReceivedSOPIdentity::numberValidVdos() const
{
    return extractBits(0, 3);
}

TPS25751ReceivedSOPIdentity::ResponseType TPS25751ReceivedSOPIdentity::responseType() const
{
    return static_cast<ResponseType>(extractBits(6, 2));
}

uint32_t TPS25751ReceivedSOPIdentity::vdo(uint8_t index) const
{
    if (index >= kMaxVdos) return 0;
    // VDO 1 starts at bit 8; the last one (index 5, bit 168) ends at bit 200,
    // the end of the 25-byte buffer.
    return extractBits32(static_cast<uint8_t>(8 + 32 * index), 32);
}

const char *toString(TPS25751ReceivedSOPIdentity::ResponseType val)
{
    switch (val) {
        case TPS25751ReceivedSOPIdentity::ResponseType::NotSentOrPending: return "SOP Discover Identity REQ not sent or pending";
        case TPS25751ReceivedSOPIdentity::ResponseType::Ack: return "Responder ACK received";
        case TPS25751ReceivedSOPIdentity::ResponseType::NakOrTimeout: return "Responder NAK received or response timeout";
        case TPS25751ReceivedSOPIdentity::ResponseType::Busy: return "Responder BUSY received";
        default: return "Undefined";
    }
}

const char *toString(TPS25751ReceivedSOPIdentity::ProductTypeUfp val)
{
    switch (val) {
        case TPS25751ReceivedSOPIdentity::ProductTypeUfp::NotUfp: return "Not a UFP";
        case TPS25751ReceivedSOPIdentity::ProductTypeUfp::PdusbHub: return "PDUSB Hub";
        case TPS25751ReceivedSOPIdentity::ProductTypeUfp::PdusbPeripheral: return "PDUSB Peripheral";
        case TPS25751ReceivedSOPIdentity::ProductTypeUfp::Psd: return "PSD";
        case TPS25751ReceivedSOPIdentity::ProductTypeUfp::Ama: return "Alternate Mode Adapter (AMA, deprecated)";
        default: return "Invalid";
    }
}

const char *toString(TPS25751ReceivedSOPIdentity::ProductTypeDfp val)
{
    switch (val) {
        case TPS25751ReceivedSOPIdentity::ProductTypeDfp::NotDfp: return "Not a DFP";
        case TPS25751ReceivedSOPIdentity::ProductTypeDfp::PdusbHub: return "PDUSB Hub";
        case TPS25751ReceivedSOPIdentity::ProductTypeDfp::PdusbHost: return "PDUSB Host";
        case TPS25751ReceivedSOPIdentity::ProductTypeDfp::PowerBrick: return "Power Brick";
        case TPS25751ReceivedSOPIdentity::ProductTypeDfp::AlternateModeController: return "Alternate Mode Controller (AMC)";
        default: return "Invalid";
    }
}

const char *toString(TPS25751ReceivedSOPIdentity::ConnectorType val)
{
    switch (val) {
        case TPS25751ReceivedSOPIdentity::ConnectorType::Unknown: return "Unknown (deprecated)";
        case TPS25751ReceivedSOPIdentity::ConnectorType::TypeCReceptacle: return "USB Type-C Receptacle";
        case TPS25751ReceivedSOPIdentity::ConnectorType::TypeCPlug: return "USB Type-C Plug";
        default: return "Invalid";
    }
}

namespace {

// Zero-padded hex; Stream's HEX output drops leading zeros.
void printHexPadded(Stream &s, uint32_t value, int digits)
{
    for (int shift = (digits - 1) * 4; shift >= 0; shift -= 4)
    {
        s.print(static_cast<unsigned int>((value >> shift) & 0xFu), HEX);
    }
}

}  // namespace

void TPS25751ReceivedSOPIdentity::debugPrint(Stream &s) const
{
    s.println(F("========================================"));
    s.print(F("RECEIVED_SOP_IDENTITY (0x"));
    s.print(static_cast<uint8_t>(TPS25751Registers::Address::RECEIVED_SOP_IDENTITY), HEX);
    s.print(F(") - "));
    s.print(size());
    s.println(F(" bytes"));
    s.println(F("========================================"));

    s.print(F("Response Type: "));
    s.print(toString(responseType()));
    s.print(F(" (raw "));
    s.print(static_cast<uint8_t>(responseType()));
    s.println(F(")"));

    const uint8_t count = numberValidVdos();
    s.print(F("Number Valid VDOs: "));
    s.println(count);

    static const char *const labels[kMaxVdos] = {"ID Header", "Cert Stat", "Product", "VDO 4", "VDO 5", "VDO 6"};
    const uint8_t shown = count < kMaxVdos ? count : kMaxVdos;
    for (uint8_t i = 0; i < shown; i++)
    {
        s.print(labels[i]);
        s.print(F(" VDO: 0x"));
        const uint32_t value = vdo(i);
        // Zero-padded to 8 hex digits; Stream's HEX output drops leading zeros.
        for (int shift = 28; shift >= 0; shift -= 4)
        {
            s.print(static_cast<unsigned int>((value >> shift) & 0xFu), HEX);
        }
        s.println();
    }

    // Typed decode, USB PD R3.3 Tables 6.34, 6.38, and 6.39. Meaningful only for
    // an Ack response and only for VDOs within the valid count.
    if (responseType() != ResponseType::Ack || count == 0)
    {
        s.println(F("Identity not valid (no Ack response or no valid VDOs); typed decode skipped"));
    }
    else
    {
        s.print(F("ID Header: VID 0x"));
        printHexPadded(s, usbVendorId(), 4);
        s.print(F(", Host: "));
        s.print(usbHostCapable() ? F("Yes") : F("No"));
        s.print(F(", Device: "));
        s.print(usbDeviceCapable() ? F("Yes") : F("No"));
        s.print(F(", UFP: "));
        s.print(toString(productTypeUfp()));
        s.print(F(", DFP: "));
        s.print(toString(productTypeDfp()));
        s.print(F(", Alt Modes: "));
        s.print(alternateModesSupported() ? F("Yes") : F("No"));
        s.print(F(", Connector: "));
        s.println(toString(connectorType()));

        if (count > 1)
        {
            s.print(F("Cert Stat: XID 0x"));
            printHexPadded(s, xid(), 8);
            s.println();
        }
        if (count > 2)
        {
            s.print(F("Product: PID 0x"));
            printHexPadded(s, usbProductId(), 4);
            s.print(F(", bcdDevice 0x"));
            printHexPadded(s, bcdDevice(), 4);
            s.println();
        }
    }

    s.println(F("========================================"));
}

bool TPS25751ReceivedSOPIdentity::isSemanticallyValid() const
{
    if (!isValid() || !hasValidData()) return false;

    constexpr size_t expectedSize = TPS25751Registers::Registers::RECEIVED_SOP_IDENTITY.size;
    if (!isValidSize(expectedSize)) return false;

    if (numberValidVdos() > kMaxVdos)
    {
        TPS_DEBUG_WARN(DEBUG_CAT_VALIDATION, "RECEIVED_SOP_IDENTITY: Number Valid VDOs %u exceeds %u", numberValidVdos(), kMaxVdos);
        return false;
    }
    if ((raw()[0] & kReservedMask) != 0)
    {
        TPS_DEBUG_WARN(DEBUG_CAT_VALIDATION, "RECEIVED_SOP_IDENTITY: reserved bits 5:3 set (0x%02X)", raw()[0] & kReservedMask);
        return false;
    }

    return true;
}
