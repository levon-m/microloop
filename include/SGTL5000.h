#pragma once
#include <Arduino.h>
#include <Wire.h>

class SGTL5000 {
public:
  explicit SGTL5000(uint8_t i2c_addr = 0x0A, TwoWire& wire = Wire) : _addr(i2c_addr), _wire(wire) {}

  uint32_t i2cHz = 400000;

  bool enable();

  //Map volume pot to either Analog HP Gain (better for HP) or Digital DAC gain
  //For kill switch, DAC mute better for fast and pop-free
  //Maybe analog too if any hiss remains behind the DAC
  //bool setHPVolume_dB(float dB);   // 0 dB is nominal (0x18 code)
  //bool setDACVolume_dB(float dB);  // 0 dB is 0x3C per channel
  //bool muteHP(bool en);
  //bool muteDAC(bool en);

  // Debug helper (leave public if you want to peek a register)
  uint16_t read(uint16_t reg);

private:
  TwoWire& _wire;
  uint8_t  _addr;

  bool write(uint16_t reg, uint16_t val);
  bool modify(uint16_t reg, uint16_t val_aligned, uint16_t mask); // RMW

  //static uint8_t hpCodeFromdB(float dB);   // 0.5 dB/step, 0 dB = 0x18
  //static uint8_t dacCodeFromdB(float dB);  // 0.5 dB/step, 0 dB = 0x3C

  //Only register addresses that I use
  static constexpr uint16_t CHIP_ID           = 0x0000;
  static constexpr uint16_t CHIP_DIG_POWER    = 0x0002;
  static constexpr uint16_t CHIP_CLK_CTRL     = 0x0004;
  static constexpr uint16_t CHIP_I2S_CTRL     = 0x0006;
  static constexpr uint16_t CHIP_SSS_CTRL     = 0x000A;
  static constexpr uint16_t CHIP_ADCDAC_CTRL  = 0x000E;
  static constexpr uint16_t CHIP_DAC_VOL      = 0x0010;
  static constexpr uint16_t CHIP_PAD_STRENGTH = 0x0014;
  static constexpr uint16_t CHIP_ANA_ADC_CTRL = 0x0020;
  static constexpr uint16_t CHIP_ANA_HP_CTRL  = 0x0022;
  static constexpr uint16_t CHIP_ANA_CTRL     = 0x0024;
  static constexpr uint16_t CHIP_LINREG_CTRL  = 0x0026;
  static constexpr uint16_t CHIP_REF_CTRL     = 0x0028;
  static constexpr uint16_t CHIP_LINE_OUT_CTRL= 0x002C;
  static constexpr uint16_t CHIP_LINE_OUT_VOL = 0x002E;
  static constexpr uint16_t CHIP_ANA_POWER    = 0x0030;
  static constexpr uint16_t CHIP_PLL_CTRL     = 0x0032;
  static constexpr uint16_t CHIP_CLK_TOP_CTRL = 0x0034;
  static constexpr uint16_t CHIP_ANA_STATUS   = 0x0036;
  static constexpr uint16_t CHIP_SHORT_CTRL   = 0x003C;
};
