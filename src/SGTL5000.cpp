#include "SGTL5000.h"

uint16_t SGTL5000::read(uint16_t reg) {
  _wire.beginTransmission(_addr); //Sets I2C slave address
  _wire.write(reg >> 8); _wire.write(reg & 0xFF); //Send high byte/low byte (big endian) to register, write() sends 8 bits at a time
  if (_wire.endTransmission(false) != 0) { //Finish without a STOP. false asks for repeated-start. If NACK, return 0 (still valid, just so you don't error out. More robust returns bool)
    return 0;
  }
  if (_wire.requestFrom((int)_addr, 2) < 2) { //Issues repeated start, switches to read mode. Requests 2 bytes, if <2 arrive, fails
    return 0;
  }
  uint16_t val = (uint16_t)(_wire.read() << 8); //Read first byte, shift into high 8 bits OR into low 8 bits
  val |= _wire.read();
  return val;
}

bool SGTL5000::write(uint16_t reg, uint16_t val) {
  _wire.beginTransmission(_addr); //Begins I2C transaction
  _wire.write(reg >> 8); _wire.write(reg & 0xFF); //Send the 16-bit register address, MSB first
  _wire.write(val >> 8); _wire.write(val & 0xFF); //Send the 16-bit data, MSB first
  return _wire.endTransmission() == 0; //Issue a STOP
}

bool SGTL5000::modify(uint16_t reg, uint16_t val_aligned, uint16_t mask) {
  uint16_t curr = read(reg);
  uint16_t next = (uint16_t)((curr & ~mask) | (val_aligned & mask));
  return write(reg, next);
}

bool SGTL5000::enable() {
  _wire.begin(); //I2C startup
  _wire.setClock(i2cHz);
  
  //PLL stuff turned off (for now?)

  const bool ok = 
     write(CHIP_CLK_CTRL, 0x0004) //Clocking: 44.1kHz sample rate, MCLK_FREQ = 256xFs
  && write(CHIP_I2S_CTRL, 0x0030) //I2S Port Format: Codec as slave + set DLEN to 16 bits (codec default, but I think can be changed)
  && write(CHIP_SSS_CTRL, 0x0010) //Route signals: ADC -> I2S OUT/IN -> DAC. DAC_SELECT = I2S_IN, I2S_SELECT = ADC for simple loopback

  //Analog reference & outputs power up, safety and to reduce pops, straight from NXP example
  //&& write(CHIP_REF_CTRL, 0x004E) //Set ref ground (VAG) near VDDA/2, and modest bias current
  && write(CHIP_REF_CTRL, 0x01E1)
  //&& write(CHIP_LINE_OUT_CTRL, 0x0322) //Line out reference & bias current (for 3.3 V VDDIO, 10k load)
  && write(CHIP_LINE_OUT_CTRL, 0x031E)
  //&& write(CHIP_REF_CTRL, 0x004F) //Slow ramp to reduce pops
  && write(CHIP_SHORT_CTRL, 0x1106) //Headphone/center short detect trip levels (optional safety)

  && write(CHIP_ANA_POWER, 0x6AFF) //Power analog blocks: LINEOUT, HP, ADC, DAC, VAG
  && write(CHIP_DIG_POWER, 0x0073) //Power digital blocks: I2S IN/OUT, DAP, DAC, ADC

  //Select inputs, unmute in pop-safe order
  && write(CHIP_LINE_OUT_VOL, 0x0F0F)
  && modify(CHIP_ANA_CTRL, 0x0004, 0x0004) //SELECT_ADC = LINEIN, keep HP/LO muted for now
  && modify(CHIP_ANA_ADC_CTRL, 0x00CC, 0x00FF) // +12 dB both channels
  && modify(CHIP_ANA_ADC_CTRL, 0x0000, 0x0100) // clear ADC_VOL_M6DB (-6 dB) bit
  && write(CHIP_DAC_VOL, 0x3C3C) //DAC L/R = 0 dB
  && write(CHIP_ANA_HP_CTRL, 0x1818) //Analog L/R = 0 dB
  && modify(CHIP_ADCDAC_CTRL, 0x0000, 0x000C) //Unmute DAC_MUTE_LEFT and DAC_MUTE_RIGHT
  && modify(CHIP_ANA_CTRL, 0x0000, 0x0110); //Unmute MUTE_HP and MUTE_LO

  return ok;
}