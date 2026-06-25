/**
 * @file main.cpp
 *
 * @brief BQ25798 fault-investigator — structured charger fault diagnostics.
 *
 * Serial-console diagnostic for investigating BQ25798 charger faults on a custom
 * 4S Li-ion board powered from USB-C PD through a TPS25751D. The immediate use
 * case is a board whose STAT-pin LED shows a distinct 1 Hz flash with erratic
 * flicker plus audible inductor whine. Rather than only dumping registers, this
 * sketch decodes the charger's status / fault / flag / config / ADC registers,
 * runs heuristic checks, and prints a structured report pointing at the likely
 * failure mode.
 *
 * @section hardware Hardware Requirements
 * - Teensy 4.x host running the TPS25751 library.
 * - TPS25751D USB-C PD controller on the primary I2Ct bus.
 * - BQ25798 buck-boost charger on the TPS25751 secondary I2Cc bus (7-bit 0x6B).
 * - 4S Li-ion pack; expected VREG 16.8 V, VSYSMIN 12.0 V; input ~20 V PD.
 *
 * @section connections Connections
 * - TPS25751 on Teensy I2C (Wire), brought up by TPS25751::begin().
 * - BQ25798 reached over the TPS25751 4CC I2Cr command-task path — no direct MCU
 *   I2C connection to the charger.
 *
 * @section usage Usage Instructions
 * 1. Build/flash:  pio run -e example-bq25798-fault-investigator -t upload
 * 2. Open serial monitor @ 115200.
 * 3. Watch the periodic diagnostic sweep and the heuristic report.
 * 4. To log while reproducing the fault, set OUTPUT_CSV to 1 and rebuild.
 *
 * @section expected Expected Output
 * - Startup banner with board assumptions, I2C address, mode, and sweep cadence.
 * - A communication check (BQ25798 Part Information).
 * - Per-sweep: raw+decoded register dumps (verbose) or one CSV row, an ADC
 *   summary in engineering units, change/event blocks, and a concise summary
 *   with a likely-cause category.
 *
 * @section notes Important Notes
 * - READ-ONLY: this sketch never writes charger configuration. The BQ25798 typed
 *   driver is decode-only; configuration changes would require typed setters not
 *   yet in the driver, so a "safe setup" mode is intentionally out of scope.
 * - PACING: every register read is a 4CC I2Cr command-task. The TRM mandates
 *   >=5 s between consecutive same-type commands, so a 5.1 s delay follows every
 *   read. A full sweep therefore takes a few MINUTES, not 5 s. We rely on the
 *   BQ25798's latched FLAG registers (REG22-27) to capture transient/intermittent
 *   faults (e.g. a 1 Hz fault) that occur between these slow samples.
 *
 * @section troubleshooting Troubleshooting
 * - "device not ACKing": check the BQ25798 is present @0x6B on I2Cc and bus
 *   pull-ups; confirm the TPS25751 host came up.
 * - All ADC channels read 0: the ADC must be enabled (ADC_CONTROL ENADC=1) in
 *   hardware; this read-only sketch does not enable it.
 *
 * Wiring: BQ25798 on TPS25751 I2Cc bus (7-bit address 0x6B).
 */

#include <Arduino.h>
#include <TPS25751.h>
#include <BQ25798/BQ25798.h>

// ===========================================================================
// Compile-time configuration
// ===========================================================================

#define OUTPUT_CSV     0    ///< 0 = verbose annotated output, 1 = compact CSV line
#define HISTORY_DEPTH  4    ///< rolling snapshots kept for change/event detection

// Board expectations (from the spec's Assumptions section).
static const uint16_t kVregExpectedMv    = 16800;  ///< 4S Li-ion full-charge target
static const uint16_t kVsysminExpectedMv = 12000;  ///< expected VSYSMIN for this board
static const uint16_t kVbusPdExpectedMv  = 20000;  ///< expected USB-C PD contract
static const uint16_t kVbusPdMinOkMv     = 18000;  ///< below this, flag a sagging PD source
static const uint16_t kVbat4sMinMv       = 11600;  ///< approx valid 4S system low
static const uint16_t kVbat4sMaxMv       = 16800;  ///< 4S regulation ceiling
static const bool     kExpectCharging    = true;   ///< board normally charges when input present

static const uint32_t kReadSpacingMs     = 5100;   ///< TRM: >=5 s between same-type I2Cr

// ===========================================================================
// Globals
// ===========================================================================

const TPS25751  pd;       // host on the primary I2Ct bus
BQ25798::Device bq(pd);   // typed BQ25798 driver over I2Cr/I2Cw 4CC tasks

// ---------------------------------------------------------------------------
// Snapshot: one full set of decoded readings used by the heuristics, CSV
// output, and change/event detection. A *Valid flag distinguishes a failed
// read from a genuine zero.
// ---------------------------------------------------------------------------
struct Snapshot
{
    uint32_t tMs     = 0;
    bool     commsOk = false;

    // --- Charger status REG1B (CS0) ---
    bool cs0Valid = false;
    bool vbusPresent = false, ac1Present = false, ac2Present = false;
    bool pgStat = false, wdExpired = false, iindpmReg = false, vindpmReg = false;

    // --- Charger status REG1C (CS1) ---
    bool    cs1Valid = false;
    uint8_t chgStat = 0;   ///< BQ25798::ChargerStatus1::ChgStat
    uint8_t vbusStat = 0;  ///< BQ25798::ChargerStatus1::VbusStat

    // --- Charger status REG1D (CS2) ---
    bool cs2Valid = false;
    bool tregStat = false, vbatPresent = false;

    // --- Charger status REG1E (CS3) ---
    bool cs3Valid = false;
    bool vsysReg = false, adcDone = false;

    // --- Charger status REG1F (CS4) ---
    bool cs4Valid = false;
    bool tsCold = false, tsCool = false, tsWarm = false, tsHot = false;

    // --- Fault status REG20/21 (live) ---
    bool fsValid = false;
    bool ibatReg = false, vbusOvp = false, vbatOvp = false, ibusOcp = false;
    bool ibatOcp = false, convOcp = false, vac2Ovp = false, vac1Ovp = false;
    bool vsysShort = false, vsysOvp = false, otgOvp = false, otgUvp = false, tshut = false;

    // --- Fault flags REG26/27 (latched) ---
    bool ffValid = false;
    bool fIbatReg = false, fVbusOvp = false, fVbatOvp = false, fIbusOcp = false;
    bool fIbatOcp = false, fConvOcp = false, fVac2Ovp = false, fVac1Ovp = false;
    bool fVsysShort = false, fVsysOvp = false, fOtgOvp = false, fOtgUvp = false, fTshut = false;

    // --- Charger flags REG22 (latched, subset of interest) ---
    bool cfValid = false;
    bool fIindpm = false, fVindpm = false, fWd = false, fPoorsrc = false;
    bool fPg = false, fVbusPresent = false;

    // --- ADC results ---
    bool vbusValid = false; uint16_t vbusMv = 0;
    bool vac1Valid = false; uint16_t vac1Mv = 0;
    bool vac2Valid = false; uint16_t vac2Mv = 0;
    bool vbatValid = false; uint16_t vbatMv = 0;
    bool vsysValid = false; uint16_t vsysMv = 0;
    bool ibusValid = false; int16_t  ibusMa = 0;
    bool ibatValid = false; int16_t  ibatMa = 0;
    bool tsValid   = false; float    tsPct = 0.0f;
    bool tdieValid = false; float    tdieC = 0.0f;
    bool adcCtrlValid = false; bool  adcEnabled = false;

    // --- Configuration sanity ---
    bool vregValid = false;    uint16_t vregMv = 0;
    bool ichgValid = false;    uint16_t ichgMa = 0;
    bool vsysminValid = false; uint16_t vsysminMv = 0;
    bool iindpmValid = false;  uint16_t iindpmMa = 0;
    bool vindpmValid = false;  uint16_t vindpmMv = 0;
    bool cc0Valid = false;     bool enChg = false, enHiz = false, enTerm = false;
    bool cc1Valid = false;     uint8_t watchdog = 0;  ///< configured WD timer enum
    bool cc5Valid = false;     bool enExtilim = false, enIindpm = false;
};

// Rolling history for multi-sample pattern detection.
static Snapshot gHistory[HISTORY_DEPTH];
static uint8_t  gHistCount = 0;   ///< number of valid entries (caps at HISTORY_DEPTH)
static uint8_t  gHistHead  = 0;   ///< index of next write
static bool     gHavePrev  = false;
static Snapshot gPrev;

// ===========================================================================
// Small print helpers
// ===========================================================================

static void printMv(const __FlashStringHelper* label, uint16_t mv, uint16_t maxMv)
{
    Serial.print(label);
    Serial.print(mv);
    Serial.print(F(" mV"));
    if (mv > maxMv) {
        Serial.print(F("  [OUT OF RANGE: max "));
        Serial.print(maxMv);
        Serial.print(F(" mV]"));
    }
    Serial.println();
}

// Verbose live dump of a freshly-read register: raw hex (from the base class)
// plus the register's own decoded fields. No-op in CSV mode.
template <typename T>
static void dumpReg(const __FlashStringHelper* label,
                    const std::unique_ptr<T>& reg,
                    bool verbose)
{
    if (!verbose) return;
    Serial.print(F("-- "));
    Serial.print(label);
    Serial.println(F(" --"));
    if (reg) {
        reg->debugDumpRaw(Serial, "  raw");
        reg->debugPrint(Serial);
    } else {
        Serial.println(F("  [read failed]"));
    }
}

// True if any live protection fault bit is asserted (excludes IBAT regulation,
// which is a regulation state, not a protection fault).
static bool anyActiveFault(const Snapshot& s)
{
    if (!s.fsValid) return false;
    return s.vbusOvp || s.vbatOvp || s.ibusOcp || s.ibatOcp || s.convOcp ||
           s.vac2Ovp || s.vac1Ovp || s.vsysShort || s.vsysOvp || s.otgOvp ||
           s.otgUvp || s.tshut;
}

// True if any latched fault/charger flag of interest is set.
static bool anyLatchedFlag(const Snapshot& s)
{
    if (s.ffValid && (s.fIbatReg || s.fVbusOvp || s.fVbatOvp || s.fIbusOcp ||
                      s.fIbatOcp || s.fConvOcp || s.fVac2Ovp || s.fVac1Ovp ||
                      s.fVsysShort || s.fVsysOvp || s.fOtgOvp || s.fOtgUvp ||
                      s.fTshut))
        return true;
    if (s.cfValid && (s.fIindpm || s.fVindpm || s.fWd || s.fPoorsrc))
        return true;
    return false;
}

// ===========================================================================
// readSnapshot — perform all paced reads, fill the struct, dump live if verbose
// ===========================================================================
static void readSnapshot(Snapshot& s, bool verbose)
{
    s = Snapshot{};
    s.tMs = millis();

    // --- Comms / part check (acts as the per-sweep liveness probe) ---
    {
        auto pi = bq.readPartInfo(/*validate=*/false);
        s.commsOk = (bool)pi;
        dumpReg(F("PartInfo (REG48h)"), pi, verbose);
        delay(kReadSpacingMs);
    }

    // --- Charger status REG1B-1F ---
    {
        auto r = bq.readChargerStatus0(/*validate=*/false);
        if (r) {
            s.cs0Valid = true;
            s.iindpmReg = r->iindpmStat(); s.vindpmReg = r->vindpmStat();
            s.wdExpired = r->wdStat();     s.pgStat = r->pgStat();
            s.ac2Present = r->ac2PresentStat(); s.ac1Present = r->ac1PresentStat();
            s.vbusPresent = r->vbusPresentStat();
        }
        dumpReg(F("ChargerStatus0 (REG1Bh)"), r, verbose);
        delay(kReadSpacingMs);
    }
    {
        auto r = bq.readChargerStatus1(/*validate=*/false);
        if (r) {
            s.cs1Valid = true;
            s.chgStat  = static_cast<uint8_t>(r->chgStat());
            s.vbusStat = static_cast<uint8_t>(r->vbusStat());
        }
        dumpReg(F("ChargerStatus1 (REG1Ch)"), r, verbose);
        delay(kReadSpacingMs);
    }
    {
        auto r = bq.readChargerStatus2(/*validate=*/false);
        if (r) { s.cs2Valid = true; s.tregStat = r->tregStat(); s.vbatPresent = r->vbatPresentStat(); }
        dumpReg(F("ChargerStatus2 (REG1Dh)"), r, verbose);
        delay(kReadSpacingMs);
    }
    {
        auto r = bq.readChargerStatus3(/*validate=*/false);
        if (r) { s.cs3Valid = true; s.vsysReg = r->vsysStat(); s.adcDone = r->adcDoneStat(); }
        dumpReg(F("ChargerStatus3 (REG1Eh)"), r, verbose);
        delay(kReadSpacingMs);
    }
    {
        auto r = bq.readChargerStatus4(/*validate=*/false);
        if (r) {
            s.cs4Valid = true;
            s.tsCold = r->tsColdStat(); s.tsCool = r->tsCoolStat();
            s.tsWarm = r->tsWarmStat(); s.tsHot = r->tsHotStat();
        }
        dumpReg(F("ChargerStatus4 (REG1Fh)"), r, verbose);
        delay(kReadSpacingMs);
    }

    // --- Fault status REG20/21 (live) ---
    {
        auto r = bq.readFaultStatus0(/*validate=*/false);
        if (r) {
            s.fsValid = true;
            s.ibatReg = r->ibatRegStat(); s.vbusOvp = r->vbusOvpStat();
            s.vbatOvp = r->vbatOvpStat(); s.ibusOcp = r->ibusOcpStat();
            s.ibatOcp = r->ibatOcpStat(); s.convOcp = r->convOcpStat();
            s.vac2Ovp = r->vac2OvpStat(); s.vac1Ovp = r->vac1OvpStat();
        }
        dumpReg(F("FaultStatus0 (REG20h)"), r, verbose);
        delay(kReadSpacingMs);
    }
    {
        auto r = bq.readFaultStatus1(/*validate=*/false);
        if (r) {
            // fsValid already set above when FS0 read; keep partial if FS0 failed.
            if (!s.fsValid) s.fsValid = true;
            s.vsysShort = r->vsysShortStat(); s.vsysOvp = r->vsysOvpStat();
            s.otgOvp = r->otgOvpStat(); s.otgUvp = r->otgUvpStat();
            s.tshut = r->tshutStat();
        }
        dumpReg(F("FaultStatus1 (REG21h)"), r, verbose);
        delay(kReadSpacingMs);
    }

    // --- Fault flags REG26/27 (latched) ---
    {
        auto r = bq.readFaultFlag0(/*validate=*/false);
        if (r) {
            s.ffValid = true;
            s.fIbatReg = r->ibatRegFlag(); s.fVbusOvp = r->vbusOvpFlag();
            s.fVbatOvp = r->vbatOvpFlag(); s.fIbusOcp = r->ibusOcpFlag();
            s.fIbatOcp = r->ibatOcpFlag(); s.fConvOcp = r->convOcpFlag();
            s.fVac2Ovp = r->vac2OvpFlag(); s.fVac1Ovp = r->vac1OvpFlag();
        }
        dumpReg(F("FaultFlag0 (REG26h)"), r, verbose);
        delay(kReadSpacingMs);
    }
    {
        auto r = bq.readFaultFlag1(/*validate=*/false);
        if (r) {
            if (!s.ffValid) s.ffValid = true;
            s.fVsysShort = r->vsysShortFlag(); s.fVsysOvp = r->vsysOvpFlag();
            s.fOtgOvp = r->otgOvpFlag(); s.fOtgUvp = r->otgUvpFlag();
            s.fTshut = r->tshutFlag();
        }
        dumpReg(F("FaultFlag1 (REG27h)"), r, verbose);
        delay(kReadSpacingMs);
    }

    // --- Charger flags REG22 (latched, subset of interest) ---
    {
        auto r = bq.readChargerFlag0(/*validate=*/false);
        if (r) {
            s.cfValid = true;
            s.fIindpm = r->iindpmFlag(); s.fVindpm = r->vindpmFlag();
            s.fWd = r->wdFlag();         s.fPoorsrc = r->poorsrcFlag();
            s.fPg = r->pgFlag();         s.fVbusPresent = r->vbusPresentFlag();
        }
        dumpReg(F("ChargerFlag0 (REG22h)"), r, verbose);
        delay(kReadSpacingMs);
    }

    // --- ADC control ---
    {
        auto r = bq.readAdcControl(/*validate=*/false);
        if (r) { s.adcCtrlValid = true; s.adcEnabled = r->adcEnabled(); }
        dumpReg(F("AdcControl (REG2Eh)"), r, verbose);
        delay(kReadSpacingMs);
    }

    // --- ADC results ---
    { auto r = bq.readVbusAdc(false); if (r) { s.vbusValid = true; s.vbusMv = r->millivolts(); } dumpReg(F("VBUS ADC (REG35h)"), r, verbose); delay(kReadSpacingMs); }
    { auto r = bq.readVac1Adc(false); if (r) { s.vac1Valid = true; s.vac1Mv = r->millivolts(); } dumpReg(F("VAC1 ADC (REG37h)"), r, verbose); delay(kReadSpacingMs); }
    { auto r = bq.readVac2Adc(false); if (r) { s.vac2Valid = true; s.vac2Mv = r->millivolts(); } dumpReg(F("VAC2 ADC (REG39h)"), r, verbose); delay(kReadSpacingMs); }
    { auto r = bq.readVbatAdc(false); if (r) { s.vbatValid = true; s.vbatMv = r->millivolts(); } dumpReg(F("VBAT ADC (REG3Bh)"), r, verbose); delay(kReadSpacingMs); }
    { auto r = bq.readVsysAdc(false); if (r) { s.vsysValid = true; s.vsysMv = r->millivolts(); } dumpReg(F("VSYS ADC (REG3Dh)"), r, verbose); delay(kReadSpacingMs); }
    { auto r = bq.readIbusAdc(false); if (r) { s.ibusValid = true; s.ibusMa = r->milliamps(); } dumpReg(F("IBUS ADC (REG31h)"), r, verbose); delay(kReadSpacingMs); }
    { auto r = bq.readIbatAdc(false); if (r) { s.ibatValid = true; s.ibatMa = r->milliamps(); } dumpReg(F("IBAT ADC (REG33h)"), r, verbose); delay(kReadSpacingMs); }
    { auto r = bq.readTsAdc(false);   if (r) { s.tsValid = true; s.tsPct = r->percent(); }       dumpReg(F("TS ADC (REG3Fh)"), r, verbose);   delay(kReadSpacingMs); }
    { auto r = bq.readTdieAdc(false); if (r) { s.tdieValid = true; s.tdieC = r->celsius(); }      dumpReg(F("TDIE ADC (REG41h)"), r, verbose); delay(kReadSpacingMs); }

    // --- Configuration sanity ---
    { auto r = bq.readChargeVoltageLimit(false);   if (r) { s.vregValid = true; s.vregMv = r->millivolts(); } dumpReg(F("ChargeVoltageLimit/VREG (REG01h)"), r, verbose); delay(kReadSpacingMs); }
    { auto r = bq.readChargeCurrentLimit(false);   if (r) { s.ichgValid = true; s.ichgMa = r->milliamps(); } dumpReg(F("ChargeCurrentLimit/ICHG (REG03h)"), r, verbose); delay(kReadSpacingMs); }
    { auto r = bq.readMinimalSystemVoltage(false); if (r) { s.vsysminValid = true; s.vsysminMv = r->millivolts(); } dumpReg(F("MinimalSystemVoltage/VSYSMIN (REG00h)"), r, verbose); delay(kReadSpacingMs); }
    { auto r = bq.readInputCurrentLimit(false);    if (r) { s.iindpmValid = true; s.iindpmMa = r->milliamps(); } dumpReg(F("InputCurrentLimit/IINDPM (REG06h)"), r, verbose); delay(kReadSpacingMs); }
    { auto r = bq.readInputVoltageLimit(false);    if (r) { s.vindpmValid = true; s.vindpmMv = r->millivolts(); } dumpReg(F("InputVoltageLimit/VINDPM (REG05h)"), r, verbose); delay(kReadSpacingMs); }
    {
        auto r = bq.readChargerControl0(false);
        if (r) { s.cc0Valid = true; s.enChg = r->enChg(); s.enHiz = r->enHiz(); s.enTerm = r->enTerm(); }
        dumpReg(F("ChargerControl0 (REG0Fh)"), r, verbose);
        delay(kReadSpacingMs);
    }
    {
        auto r = bq.readChargerControl1(false);
        if (r) { s.cc1Valid = true; s.watchdog = static_cast<uint8_t>(r->watchdog()); }
        dumpReg(F("ChargerControl1/Watchdog (REG10h)"), r, verbose);
        delay(kReadSpacingMs);
    }
    {
        auto r = bq.readChargerControl5(false);
        if (r) { s.cc5Valid = true; s.enExtilim = r->enExtilim(); s.enIindpm = r->enIindpm(); }
        dumpReg(F("ChargerControl5/EN_EXTILIM,EN_IINDPM (REG14h)"), r, verbose);
        delay(kReadSpacingMs);
    }
}

// ===========================================================================
// printAdcSummary — engineering-units view (always printed, both modes)
// ===========================================================================
static void printAdcSummary(const Snapshot& s)
{
    Serial.println(F("--- ADC (engineering units) ---"));
    if (!s.adcCtrlValid) {
        Serial.println(F("  ADC_CONTROL: [read failed]"));
    } else if (!s.adcEnabled) {
        Serial.println(F("  ADC_CONTROL: ADC DISABLED (ENADC=0) -> readings below are not live"));
    }
    if (s.vbusValid) printMv(F("  VBUS : "), s.vbusMv, 30000); else Serial.println(F("  VBUS : [read failed]"));
    if (s.vac1Valid) printMv(F("  VAC1 : "), s.vac1Mv, 30000); else Serial.println(F("  VAC1 : [read failed]"));
    if (s.vac2Valid) printMv(F("  VAC2 : "), s.vac2Mv, 30000); else Serial.println(F("  VAC2 : [read failed]"));
    if (s.vbatValid) printMv(F("  VBAT : "), s.vbatMv, 20000); else Serial.println(F("  VBAT : [read failed]"));
    if (s.vsysValid) printMv(F("  VSYS : "), s.vsysMv, 20000); else Serial.println(F("  VSYS : [read failed]"));
    if (s.ibusValid) { Serial.print(F("  IBUS : ")); Serial.print(s.ibusMa); Serial.println(F(" mA")); } else Serial.println(F("  IBUS : [read failed]"));
    if (s.ibatValid) { Serial.print(F("  IBAT : ")); Serial.print(s.ibatMa); Serial.println(F(" mA")); } else Serial.println(F("  IBAT : [read failed]"));
    if (s.tsValid)   { Serial.print(F("  TS   : ")); Serial.print(s.tsPct, 2); Serial.println(F(" % REGN")); } else Serial.println(F("  TS   : [read failed]"));
    if (s.tdieValid) { Serial.print(F("  TDIE : ")); Serial.print(s.tdieC, 1); Serial.println(F(" degC")); } else Serial.println(F("  TDIE : [read failed]"));
}

// ===========================================================================
// printCsvLine — compact one-line row for logging while reproducing the fault
// ===========================================================================
static void printCsvHeader()
{
    Serial.println(F("CSV,t_ms,commsOk,vbusMv,vac1Mv,vac2Mv,vbatMv,vsysMv,ibusMa,ibatMa,tsPct,tdieC,"
                     "activeFault,latchedFlag,chgStat,vbusStat,enChg,enHiz,vregMv,ichgMa,vsysminMv,iindpmMa,vindpmMv,wdExpired"));
}

static void printCsvLine(const Snapshot& s)
{
    Serial.print(F("CSV,"));
    Serial.print(s.tMs);          Serial.print(',');
    Serial.print(s.commsOk);      Serial.print(',');
    Serial.print(s.vbusValid ? s.vbusMv : -1); Serial.print(',');
    Serial.print(s.vac1Valid ? s.vac1Mv : -1); Serial.print(',');
    Serial.print(s.vac2Valid ? s.vac2Mv : -1); Serial.print(',');
    Serial.print(s.vbatValid ? s.vbatMv : -1); Serial.print(',');
    Serial.print(s.vsysValid ? s.vsysMv : -1); Serial.print(',');
    if (s.ibusValid) Serial.print(s.ibusMa); else Serial.print(F("NA")); Serial.print(',');
    if (s.ibatValid) Serial.print(s.ibatMa); else Serial.print(F("NA")); Serial.print(',');
    if (s.tsValid)   Serial.print(s.tsPct, 2); else Serial.print(F("NA")); Serial.print(',');
    if (s.tdieValid) Serial.print(s.tdieC, 1); else Serial.print(F("NA")); Serial.print(',');
    Serial.print(anyActiveFault(s)); Serial.print(',');
    Serial.print(anyLatchedFlag(s)); Serial.print(',');
    Serial.print(s.chgStat);  Serial.print(',');
    Serial.print(s.vbusStat); Serial.print(',');
    Serial.print(s.enChg);    Serial.print(',');
    Serial.print(s.enHiz);    Serial.print(',');
    Serial.print(s.vregValid ? s.vregMv : -1);       Serial.print(',');
    Serial.print(s.ichgValid ? s.ichgMa : -1);       Serial.print(',');
    Serial.print(s.vsysminValid ? s.vsysminMv : -1); Serial.print(',');
    Serial.print(s.iindpmValid ? s.iindpmMa : -1);   Serial.print(',');
    Serial.print(s.vindpmValid ? s.vindpmMv : -1);   Serial.print(',');
    Serial.print(s.wdExpired);
    Serial.println();
}

// ===========================================================================
// trackChanges — report latched flags newly set since the previous sample
// Returns true if any new flag/fault appeared.
// ===========================================================================
static bool reportNew(const __FlashStringHelper* name, bool prev, bool cur)
{
    if (cur && !prev) { Serial.print(F("    NEW: ")); Serial.println(name); return true; }
    return false;
}

static bool trackChanges(const Snapshot& p, const Snapshot& c)
{
    bool any = false;
    bool header = false;
    auto ensureHeader = [&]() {
        if (!header) { Serial.println(F("--- Changes since previous sample ---")); header = true; }
    };

    // Latched fault flags
    if (p.ffValid && c.ffValid) {
        struct { const __FlashStringHelper* n; bool pv, cv; } ff[] = {
            { F("IBAT_REG flag"),  p.fIbatReg,  c.fIbatReg },
            { F("VBUS_OVP flag"),  p.fVbusOvp,  c.fVbusOvp },
            { F("VBAT_OVP flag"),  p.fVbatOvp,  c.fVbatOvp },
            { F("IBUS_OCP flag"),  p.fIbusOcp,  c.fIbusOcp },
            { F("IBAT_OCP flag"),  p.fIbatOcp,  c.fIbatOcp },
            { F("CONV_OCP flag"),  p.fConvOcp,  c.fConvOcp },
            { F("VAC2_OVP flag"),  p.fVac2Ovp,  c.fVac2Ovp },
            { F("VAC1_OVP flag"),  p.fVac1Ovp,  c.fVac1Ovp },
            { F("VSYS_SHORT flag"),p.fVsysShort,c.fVsysShort },
            { F("VSYS_OVP flag"),  p.fVsysOvp,  c.fVsysOvp },
            { F("OTG_OVP flag"),   p.fOtgOvp,   c.fOtgOvp },
            { F("OTG_UVP flag"),   p.fOtgUvp,   c.fOtgUvp },
            { F("TSHUT flag"),     p.fTshut,    c.fTshut },
        };
        for (auto& e : ff) { if (e.cv && !e.pv) { ensureHeader(); any |= reportNew(e.n, e.pv, e.cv); } }
    }

    // Latched charger flags of interest
    if (p.cfValid && c.cfValid) {
        struct { const __FlashStringHelper* n; bool pv, cv; } cf[] = {
            { F("IINDPM flag"),    p.fIindpm,  c.fIindpm },
            { F("VINDPM flag"),    p.fVindpm,  c.fVindpm },
            { F("Watchdog flag"),  p.fWd,      c.fWd },
            { F("Poor-source flag"), p.fPoorsrc, c.fPoorsrc },
        };
        for (auto& e : cf) { if (e.cv && !e.pv) { ensureHeader(); any |= reportNew(e.n, e.pv, e.cv); } }
    }

    return any;
}

// ===========================================================================
// Heuristic checks (spec items 1-10) operating on the current snapshot and
// rolling history.
// ===========================================================================
static const char* tsRegion(const Snapshot& s)
{
    if (!s.cs4Valid) return "unknown";
    if (s.tsCold) return "COLD (charge suspended)";
    if (s.tsHot)  return "HOT (charge suspended)";
    if (s.tsCool) return "COOL (reduced current)";
    if (s.tsWarm) return "WARM (reduced voltage)";
    return "normal";
}

static const char* chgPhase(uint8_t v)
{
    switch (v) {
        case 0: return "Not charging";
        case 1: return "Trickle charge";
        case 2: return "Pre-charge";
        case 3: return "Fast charge (CC)";
        case 4: return "Taper charge (CV)";
        case 6: return "Top-off";
        case 7: return "Termination done";
        default: return "Reserved/unknown";
    }
}

// Detect the poor-source / unstable-input pattern across history.
static bool detectPoorSource()
{
    if (gHistCount < 3) return false;
    uint8_t vbusToggles = 0, prevPresent = 0xFF, poorsrcCount = 0, vindpmCount = 0;
    for (uint8_t i = 0; i < gHistCount; ++i) {
        const Snapshot& h = gHistory[i];
        if (h.cs0Valid) {
            uint8_t present = h.vbusPresent ? 1 : 0;
            if (prevPresent != 0xFF && present != prevPresent) vbusToggles++;
            prevPresent = present;
        }
        if (h.cfValid && h.fPoorsrc) poorsrcCount++;
        if (h.cfValid && h.fVindpm)  vindpmCount++;
        if (h.cs0Valid && h.vindpmReg) vindpmCount++;
    }
    return (vbusToggles >= 2) || (poorsrcCount >= 2) || (vindpmCount >= 3);
}

// Likely-cause category for the concise summary.
enum class Cause { None, InputPath, BatteryPath, Temperature, Overcurrent, SystemRail, Configuration, Unknown };

static const char* causeName(Cause c)
{
    switch (c) {
        case Cause::None:          return "none (no active faults)";
        case Cause::InputPath:     return "input path";
        case Cause::BatteryPath:   return "battery path";
        case Cause::Temperature:   return "temperature qualification";
        case Cause::Overcurrent:   return "overcurrent";
        case Cause::SystemRail:    return "system rail";
        case Cause::Configuration: return "configuration";
        default:                   return "unknown";
    }
}

static Cause classifyCause(const Snapshot& s)
{
    if (s.fsValid) {
        if (s.ibusOcp || s.ibatOcp || s.convOcp) return Cause::Overcurrent;
        if (s.vsysShort || s.vsysOvp)            return Cause::SystemRail;
        if (s.vbatOvp)                            return Cause::BatteryPath;
        if (s.vbusOvp || s.vac1Ovp || s.vac2Ovp) return Cause::InputPath;
        if (s.tshut)                              return Cause::Temperature;
    }
    if (s.cs4Valid && (s.tsCold || s.tsHot))     return Cause::Temperature;
    if (s.vbusValid && s.vbusMv < kVbusPdMinOkMv) return Cause::InputPath;
    if ((s.cfValid && s.fPoorsrc) || (s.cs0Valid && s.vindpmReg)) return Cause::InputPath;
    if (s.vregValid && s.vregMv != kVregExpectedMv) return Cause::Configuration;
    if (s.cs0Valid && kExpectCharging && s.vbusPresent && s.cc0Valid && !s.enChg) return Cause::Configuration;
    if (anyActiveFault(s)) return Cause::Unknown;
    return Cause::None;
}

static void runHeuristics(const Snapshot& s)
{
    Serial.println(F("--- Diagnostic checks ---"));

    // 1. STAT fault correlation
    Serial.println(F("  [STAT] A 1 Hz STAT blink means the charger believes a fault is present."));
    if (anyActiveFault(s)) {
        Serial.println(F("  [STAT] STAT blink explained by active fault bits."));
    } else if (anyLatchedFlag(s)) {
        Serial.println(F("  [STAT] STAT blink may be from intermittent fault; inspect latched flags and time correlation."));
    } else {
        Serial.println(F("  [STAT] STAT behavior not explained by digital fault registers; inspect STAT LED circuit, pullup, sink current, and noise."));
    }

    // 2. Input-path checks
    if (s.vbusValid && s.vac1Valid && s.vac2Valid) {
        // Without an external ACFET/RBFET path VAC should track VBUS.
        const int32_t dv1 = (int32_t)s.vac1Mv - s.vbusMv;
        const int32_t dv2 = (int32_t)s.vac2Mv - s.vbusMv;
        if (abs(dv1) > 1500 && abs(dv2) > 1500) {
            Serial.println(F("  [INPUT] VAC1/VAC2 do not track VBUS (>1.5 V apart) — expected to track if no external ACFET/RBFET path."));
        }
    }
    if (s.vbusValid && s.vbusPresent && s.vbusMv < kVbusPdMinOkMv) {
        Serial.print(F("  [INPUT] VBUS ")); Serial.print(s.vbusMv);
        Serial.print(F(" mV is below expected PD (~")); Serial.print(kVbusPdExpectedMv);
        Serial.println(F(" mV) — possible PD-contract/source droop."));
    }
    if (s.vbusOvp || s.vac1Ovp || s.vac2Ovp) Serial.println(F("  [INPUT] VBUS/VAC over-voltage active."));
    if (s.cs0Valid && s.vindpmReg) Serial.println(F("  [INPUT] Input voltage regulation (VINDPM) active — source sagging under load."));
    if (s.cs0Valid && s.iindpmReg) Serial.println(F("  [INPUT] Input current regulation (IINDPM) active — input current limited."));

    // 3. ILIM_HIZ checks
    if (s.cc5Valid) {
        Serial.print(F("  [ILIM] EN_EXTILIM = ")); Serial.println(s.enExtilim ? F("enabled") : F("disabled"));
        if (s.enExtilim) {
            Serial.println(F("  [ILIM] Actual input limit = lower of the ILIM_HIZ pin-derived limit and IINDPM register."));
            Serial.println(F("  [ILIM] ILIM_HIZ near/below the HiZ threshold can stop switching or cause cycling."));
            Serial.println(F("  [ILIM] MCU cannot read the ILIM_HIZ pin — measure it with a DMM/scope if cycling is suspected."));
        }
    }

    // 4. Charge-enable checks
    if (s.cc0Valid) {
        Serial.print(F("  [CHG] EN_CHG = ")); Serial.println(s.enChg ? F("enabled") : F("DISABLED"));
        Serial.println(F("  [CHG] Reminder: CE pin is active-low and must not float."));
        if (!s.enChg && kExpectCharging) {
            Serial.println(F("  [CHG] Charging expected but appears disabled — check: register EN_CHG, CE pin, TS region, active fault, watchdog, or termination."));
        }
    }

    // 5. Thermistor / TS checks
    Serial.print(F("  [TS] Region: ")); Serial.println(tsRegion(s));
    if (s.cs4Valid && (s.tsCold || s.tsHot)) {
        Serial.println(F("  [TS] Charging suspended by temperature qualification (out of valid charge range)."));
        Serial.println(F("  [TS] Verify NTC divider REGN -> TS -> GND and measure TS voltage physically."));
    }

    // 6. Battery / 4S configuration checks
    if (s.vbatValid) {
        if (s.vbatMv < kVbat4sMinMv) { Serial.print(F("  [BAT] VBAT ")); Serial.print(s.vbatMv); Serial.println(F(" mV below 4S valid range (~11.6 V) — deeply discharged / UV-like.")); }
        if (s.vbatMv > kVbat4sMaxMv) { Serial.print(F("  [BAT] VBAT ")); Serial.print(s.vbatMv); Serial.println(F(" mV above 4S regulation (16.8 V) — possible OVP-like condition.")); }
    }
    if (s.vregValid && s.vregMv != kVregExpectedMv) { Serial.print(F("  [BAT] VREG ")); Serial.print(s.vregMv); Serial.println(F(" mV != 16.8 V expected for a 4S Li-ion pack.")); }
    if (s.vsysminValid && s.vsysminMv != kVsysminExpectedMv) { Serial.print(F("  [BAT] VSYSMIN ")); Serial.print(s.vsysminMv); Serial.println(F(" mV != 12.0 V expected for this board.")); }
    if (s.cs1Valid) { Serial.print(F("  [BAT] Charge phase: ")); Serial.println(chgPhase(s.chgStat)); }

    // 7. SYS rail checks
    if (s.vsysValid) {
        Serial.print(F("  [SYS] VSYS = ")); Serial.print(s.vsysMv); Serial.println(F(" mV"));
        if (s.vsysminValid && s.vsysMv <= s.vsysminMv + 200) {
            Serial.println(F("  [SYS] VSYS at/near VSYSMIN — check pulsed system/LED load, SYS capacitance, battery supplement, and input power limit."));
        }
    }
    if (s.vsysShort) Serial.println(F("  [SYS] VSYS short-circuit fault active."));
    if (s.vsysOvp)   Serial.println(F("  [SYS] VSYS over-voltage fault active."));

    // 8. Overcurrent checks
    if (s.ibusOcp || s.ibatOcp || s.convOcp || s.ibatReg) {
        if (s.ibusOcp) Serial.println(F("  [OC] IBUS over-current (IBUS_OCP) active."));
        if (s.ibatOcp) Serial.println(F("  [OC] IBAT over-current (IBAT_OCP) active."));
        if (s.convOcp) Serial.println(F("  [OC] Converter over-current (CONV_OCP) active."));
        if (s.ibatReg) Serial.println(F("  [OC] IBAT discharge-current regulation active."));
        if (s.ibusValid) { Serial.print(F("  [OC] IBUS = ")); Serial.print(s.ibusMa); Serial.println(F(" mA")); }
        if (s.ibatValid) { Serial.print(F("  [OC] IBAT = ")); Serial.print(s.ibatMa); Serial.println(F(" mA")); }
        Serial.println(F("  [OC] CONV_OCP/whine hardware checks: inductor value & saturation current; SW1/SW2 layout & ringing;"));
        Serial.println(F("       PMID/SYS/BAT cap placement; shorted/excess downstream load; LED-driver transients; wrong ICHG/IINDPM config."));
    }

    // 9. Poor-source / unstable-input pattern over history
    if (detectPoorSource()) {
        Serial.println(F("  [PATTERN] possible poor-source / PD-contract / input-current-limit cycling (detected over recent samples)."));
    }

    // 10. Watchdog & register-reset checks
    if (s.wdExpired) Serial.println(F("  [WD] Watchdog expired (WD_STAT) — host has not petted the watchdog; config may revert to defaults."));
    if (s.cfValid && s.fWd) Serial.println(F("  [WD] Watchdog flag latched since last sample."));
    if (gHavePrev && s.vregValid && gPrev.vregValid && s.vregMv != gPrev.vregMv) {
        Serial.println(F("  [WD] VREG changed between samples — configuration may be resetting during the fault cycle (check watchdog)."));
    }
}

// ===========================================================================
// printFaultEvent — block emitted when a new fault/flag appears (spec item 11)
// ===========================================================================
static void printFaultEvent(const Snapshot& s)
{
    Serial.println(F("***** FAULT EVENT *****"));
    Serial.print(F("  t = ")); Serial.print(s.tMs); Serial.println(F(" ms"));

    Serial.print(F("  Active faults:"));
    if (!anyActiveFault(s)) Serial.print(F(" none"));
    else {
        if (s.vbusOvp) Serial.print(F(" VBUS_OVP"));
        if (s.vbatOvp) Serial.print(F(" VBAT_OVP"));
        if (s.ibusOcp) Serial.print(F(" IBUS_OCP"));
        if (s.ibatOcp) Serial.print(F(" IBAT_OCP"));
        if (s.convOcp) Serial.print(F(" CONV_OCP"));
        if (s.vac1Ovp) Serial.print(F(" VAC1_OVP"));
        if (s.vac2Ovp) Serial.print(F(" VAC2_OVP"));
        if (s.vsysShort) Serial.print(F(" VSYS_SHORT"));
        if (s.vsysOvp) Serial.print(F(" VSYS_OVP"));
        if (s.otgOvp)  Serial.print(F(" OTG_OVP"));
        if (s.otgUvp)  Serial.print(F(" OTG_UVP"));
        if (s.tshut)   Serial.print(F(" TSHUT"));
    }
    Serial.println();

    Serial.print(F("  Rails: "));
    if (s.vbusValid) { Serial.print(F("VBUS=")); Serial.print(s.vbusMv); Serial.print(F("mV ")); }
    if (s.vac1Valid) { Serial.print(F("VAC1=")); Serial.print(s.vac1Mv); Serial.print(F("mV ")); }
    if (s.vac2Valid) { Serial.print(F("VAC2=")); Serial.print(s.vac2Mv); Serial.print(F("mV ")); }
    if (s.vbatValid) { Serial.print(F("VBAT=")); Serial.print(s.vbatMv); Serial.print(F("mV ")); }
    if (s.vsysValid) { Serial.print(F("VSYS=")); Serial.print(s.vsysMv); Serial.print(F("mV ")); }
    if (s.ibusValid) { Serial.print(F("IBUS=")); Serial.print(s.ibusMa); Serial.print(F("mA ")); }
    if (s.ibatValid) { Serial.print(F("IBAT=")); Serial.print(s.ibatMa); Serial.print(F("mA ")); }
    if (s.tsValid)   { Serial.print(F("TS="));   Serial.print(s.tsPct, 1); Serial.print(F("% ")); }
    Serial.println();

    // Top likely causes (up to 3), ordered by specificity.
    Serial.println(F("  Top likely causes:"));
    uint8_t n = 0;
    auto emit = [&](const __FlashStringHelper* m) { if (n < 3) { Serial.print(F("    - ")); Serial.println(m); n++; } };
    if (s.ibusOcp || s.ibatOcp || s.convOcp) emit(F("Overcurrent: check inductor saturation, downstream short/load, ICHG/IINDPM config."));
    if (s.vsysShort) emit(F("VSYS short: check SYS rail wiring and downstream short."));
    if (s.vsysOvp)   emit(F("VSYS OVP: check load dump / pulsed load and SYS capacitance."));
    if (s.vbatOvp)   emit(F("VBAT OVP: check pack voltage vs 4S regulation and VREG setting."));
    if (s.vbusOvp || s.vac1Ovp || s.vac2Ovp) emit(F("Input OVP: check adapter/PD voltage and VAC OVP threshold."));
    if (s.tshut || s.tsCold || s.tsHot) emit(F("Thermal/TS: check die temperature and NTC divider."));
    if ((s.cfValid && s.fPoorsrc) || (s.cs0Valid && s.vindpmReg)) emit(F("Poor source / VINDPM: PD contract or input limit cycling."));
    if (s.cc0Valid && !s.enChg && kExpectCharging) emit(F("Charging disabled: EN_CHG/CE/TS/watchdog/termination."));
    if (n == 0) emit(F("No digital fault — inspect STAT LED circuit and analog signals."));
    Serial.println(F("***********************"));
}

// ===========================================================================
// printSummary — concise, de-duplicated periodic summary (spec item 12)
// ===========================================================================
static void printSummary(const Snapshot& s)
{
    static bool  haveLast = false;
    static bool  lastActive = false;
    static Cause lastCause = Cause::None;

    const bool  active = anyActiveFault(s);
    const Cause cause  = classifyCause(s);

    // Suppress identical consecutive summaries.
    if (haveLast && active == lastActive && cause == lastCause) return;
    haveLast = true; lastActive = active; lastCause = cause;

    Serial.println(F("--- Summary ---"));
    if (active) {
        Serial.print(F("  Active faults: "));
        bool first = true;
        auto p = [&](bool b, const __FlashStringHelper* m) { if (b) { if (!first) Serial.print(F(", ")); Serial.print(m); first = false; } };
        p(s.vbusOvp, F("VBUS_OVP")); p(s.vbatOvp, F("VBAT_OVP"));
        p(s.ibusOcp, F("IBUS_OCP")); p(s.ibatOcp, F("IBAT_OCP")); p(s.convOcp, F("CONV_OCP"));
        p(s.vac1Ovp, F("VAC1_OVP")); p(s.vac2Ovp, F("VAC2_OVP"));
        p(s.vsysShort, F("VSYS_SHORT")); p(s.vsysOvp, F("VSYS_OVP"));
        p(s.otgOvp, F("OTG_OVP")); p(s.otgUvp, F("OTG_UVP")); p(s.tshut, F("TSHUT"));
        Serial.println();
    } else {
        Serial.println(F("  No active faults"));
    }
    Serial.print(F("  Likely cause category: "));
    Serial.println(causeName(cause));
}

// ===========================================================================
// History management
// ===========================================================================
static void pushHistory(const Snapshot& s)
{
    gHistory[gHistHead] = s;
    gHistHead = (gHistHead + 1) % HISTORY_DEPTH;
    if (gHistCount < HISTORY_DEPTH) gHistCount++;
}

// ===========================================================================
// setup / loop
// ===========================================================================
void setup()
{
    Serial.begin(115200);
    while (!Serial && millis() < 3000) {
        delay(10);
    }

    TPS25751::setDebugLevel(DEBUG_LEVEL_WARN);
    TPS25751::setDebugCategories(DEBUG_CAT_I2C | DEBUG_CAT_TASK);

    Serial.println(F("Initializing TPS25751 host..."));
    pd.begin();

    // --- Startup banner ---
    Serial.println();
    Serial.println(F("=========================================================="));
    Serial.println(F(" BQ25798 Fault Investigator"));
    Serial.println(F("=========================================================="));
    Serial.println(F(" Board assumptions: 4S Li-ion; VREG 16.8 V; VSYSMIN 12.0 V"));
    Serial.println(F(" Input: USB-C PD via TPS25751D, typically ~20 V contract"));
    Serial.print  (F(" BQ25798 on TPS25751 I2Cc bus @ 7-bit 0x"));
    Serial.println(BQ25798::kDefaultI2CAddress, HEX);
#if OUTPUT_CSV
    Serial.println(F(" Output mode: CSV (logging)"));
#else
    Serial.println(F(" Output mode: VERBOSE (interactive)"));
#endif
    Serial.println(F(" Mode: READ-ONLY (no charger configuration is written)"));
    Serial.println(F("   Note: configuration changes are out of scope; they would"));
    Serial.println(F("   require typed setters not yet in the BQ25798 driver."));
    Serial.println(F(" Pacing: 5.1 s per read (TRM >=5 s between same-type I2Cr)."));
    Serial.println(F("   A full sweep takes a few MINUTES; latched FLAG registers"));
    Serial.println(F("   capture transient faults missed between slow samples."));
    Serial.println(F("=========================================================="));

    // --- Communication check ---
    Serial.println();
    Serial.println(F("Verifying BQ25798 communication (Part Information, REG48h)..."));
    auto info = bq.readPartInfo(/*validate=*/true);
    if (info) {
        Serial.print(F("  OK: PN[5:3]=0b")); Serial.print(info->partNumber(), BIN);
        Serial.print(F(", DEV_REV[2:0]=")); Serial.println(info->deviceRevision());
    } else {
        Serial.println(F("  FAILED: BQ25798 did not ACK / PN check failed."));
        Serial.println(F("  Check: device present @0x6B on I2Cc, bus pull-ups, TPS25751 host up."));
        Serial.println(F("  Continuing anyway — diagnostic reads will report per-register failures."));
    }
    delay(kReadSpacingMs);

#if OUTPUT_CSV
    printCsvHeader();
#endif

    Serial.println(F("Starting diagnostic sweeps..."));
}

void loop()
{
    Snapshot cur;

#if OUTPUT_CSV
    const bool verbose = false;
#else
    const bool verbose = true;
    Serial.println();
    Serial.print(F("========== Diagnostic sweep @ "));
    Serial.print(millis());
    Serial.println(F(" ms =========="));
#endif

    readSnapshot(cur, verbose);

#if OUTPUT_CSV
    printCsvLine(cur);
#else
    printAdcSummary(cur);

    bool newEvent = false;
    if (gHavePrev) {
        newEvent = trackChanges(gPrev, cur);
    }
    // A newly-appearing active fault is also an event.
    if (anyActiveFault(cur) && (!gHavePrev || !anyActiveFault(gPrev))) {
        newEvent = true;
    }

    runHeuristics(cur);

    if (newEvent) {
        printFaultEvent(cur);
    }

    printSummary(cur);

    Serial.println(F("========== end of sweep =========="));
#endif

    pushHistory(cur);
    gPrev = cur;
    gHavePrev = true;

    // Loop pacing is provided by the per-read 5.1 s delays inside readSnapshot.
}
