#include <Arduino.h>
#include <SPI.h>
#include "RFV3.h"
#include "cc1101_spi.h"

SPIClass * vspi = NULL;

void init_spi() {
  log("SPI init"); 
  pinMode(SS_PIN, OUTPUT);
  vspi = new SPIClass(VSPI);
  vspi->begin(CLK_PIN,MISO_PIN,MOSI_PIN);
  log("SPI init done"); 
}

void spi_start() {
  digitalWrite(SS_PIN, LOW);
  vspi->beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
}

void spi_end() {
  vspi->endTransaction();
  digitalWrite(SS_PIN, HIGH);
}

uint8_t spi_putc(uint8_t data) {
  return vspi->transfer(data);
}

void spi_write_strobe(uint8_t spi_instr)
{
  spi_start();
  spi_putc(spi_instr);
  spi_end();
}

uint8_t spi_read_register(uint8_t spi_instr)
{
  spi_start();
  spi_putc(spi_instr | 0x80);
  spi_instr = spi_putc(0xFF);
  spi_end();

  return spi_instr;
}

void spi_read_burst(uint8_t spi_instr, uint8_t *pArr, uint8_t length)
{
  spi_start();
  spi_putc(spi_instr | 0xC0);

  for (uint8_t i = 0; i < length; i++)
  {
    pArr[i] = spi_putc(0xFF);
  }
  spi_end();
}

void spi_write_register(uint8_t spi_instr, uint8_t value)
{
  spi_start();
  spi_putc(spi_instr | 0x00);
  spi_putc(value);
  spi_end();
}

void spi_write_burst(uint8_t spi_instr, uint8_t *pArr, uint8_t length)
{
  spi_start();
  spi_putc(spi_instr | 0x40);

  for (uint8_t i = 0; i < length ; i++)
  {
    spi_putc(pArr[i]);
  }
  spi_end();
}