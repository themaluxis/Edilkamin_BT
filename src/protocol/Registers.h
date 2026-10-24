// Edilkamin "The Mind" modbus register map (as observed on the BLE link).
// Addresses are 16 bit modbus holding registers. Only the registers marked
// "validated" are used by the firmware; the rest are documented for future
// work (F09) and MUST be validated on real hardware before being exposed.
#pragma once
#include <cstdint>

namespace edk {
namespace reg {

// --- Registers used by the firmware (validated on Slide2 7-UP) ---
constexpr uint16_t Thermocouple = 0x02BB;        // exhaust temp, tenths °C
constexpr uint16_t FireplaceMainStatus = 0x02E4; // (phase, state) bytes
constexpr uint16_t MainEnvTemp = 0x02E9;         // room temp, tenths °C
constexpr uint16_t WarningFlags = 0x02F4;        // 2 registers; byte1: pellet
constexpr uint16_t OnOff = 0x031C;               // write 1 = ignite, 0 = off
constexpr uint16_t Chrono = 0x0522;              // low byte 0/1
constexpr uint16_t Automatic = 0x0523;           // high byte 0/1
constexpr uint16_t TargetTemp = 0x0525;          // tenths °C
constexpr uint16_t FanPower = 0x0529;            // high: fan1 (1..6), low: power (1..5)
constexpr uint16_t Relax = 0x052B;               // 0x0101 on / 0x0000 off
constexpr uint16_t Standby = 0x052C;             // 0x0101 on / 0x0000 off

// --- Documented but not validated (from the app decompilation) ---
constexpr uint16_t MotherboardSerial = 0x000A;   // 8 regs
constexpr uint16_t MotherboardName = 0x0012;     // 8 regs
constexpr uint16_t MotherboardBootloader = 0x001A;
constexpr uint16_t MotherboardBlVersion = 0x0022;
constexpr uint16_t MotherboardApp = 0x002A;
constexpr uint16_t MotherboardAppVersion = 0x0032;
constexpr uint16_t RemoteControlSerial = 0x003C;
constexpr uint16_t EmergencyPanelVersion = 0x0064;
constexpr uint16_t RemoteControlName = 0x0076;
constexpr uint16_t DisplayIdro = 0x0096;
constexpr uint16_t Stats = 0x02C2;
constexpr uint16_t FanMachine = 0x02E7;
constexpr uint16_t Fan23Machine = 0x02E8;
constexpr uint16_t Canalized2 = 0x02EA;
constexpr uint16_t Canalized3 = 0x02EB;
constexpr uint16_t PelletRemaining = 0x02EC;
constexpr uint16_t BoilerSet = 0x02EE;
constexpr uint16_t Alarm = 0x02E6;
constexpr uint16_t FireplaceStoveStatus = 0x02E5;
constexpr uint16_t BoilerTemp = 0x02B5;
constexpr uint16_t PufferTemp = 0x02B6;
constexpr uint16_t StandbyStartTime = 0x02FC;
constexpr uint16_t DateTime = 0x0316;            // 4 regs
constexpr uint16_t EasyTimer = 0x031B;
constexpr uint16_t FireplaceCommands = 0x031C;   // same as OnOff (sweep/charge coclea)
constexpr uint16_t Data = 0x0334;                // 6 regs
constexpr uint16_t TotalAlarms = 0x033A;
constexpr uint16_t AlarmLogs1 = 0x033C;
constexpr uint16_t EcoTemp = 0x04CC;
constexpr uint16_t ComfortTemp = 0x04CD;
constexpr uint16_t ChronoData = 0x04CE;          // 42 regs (too large for 32 B frames)
constexpr uint16_t ChronoPowerData = 0x04F8;     // 42 regs
constexpr uint16_t TempFormat = 0x0524;
constexpr uint16_t TempCanalized2 = 0x0526;
constexpr uint16_t Fan23 = 0x052A;
constexpr uint16_t StandbyTimer = 0x052D;
constexpr uint16_t PelletSensor = 0x052E;        // continue coclea
constexpr uint16_t BleRegister = 0x0538;
constexpr uint16_t WebTime = 0x0538;
constexpr uint16_t StoveType = 0x05DD;
constexpr uint16_t NumberFan = 0x0548;
constexpr uint16_t Fan12Env = 0x0549;
constexpr uint16_t Fan3Env = 0x054A;
constexpr uint16_t FumesTempOff = 0x05A8;

} // namespace reg
} // namespace edk
