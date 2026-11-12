#include "SPI.h"
#include "gr551x_sdk.h"

extern "C" {
#include "app_spi.h"
}

SPIClass SPI;

static void spi_evt(app_spi_evt_t *e) { (void)e; }

void SPIClass::configure(uint32_t clock, arduino::SPIMode mode)
{
  if (ready) app_spi_deinit(APP_SPI_ID_MASTER);
  ready = false;

  static app_spi_params_t p;
  memset(&p, 0, sizeof(p));
  p.id = APP_SPI_ID_MASTER;
  p.pin_cfg.cs   = { APP_IO_TYPE_NORMAL, APP_IO_MUX_7, APP_IO_PIN_0, APP_IO_MODE_OUTPUT, APP_IO_NOPULL, APP_SPI_PIN_DISABLE };
  p.pin_cfg.clk  = { APP_IO_TYPE_NORMAL, PIN_SPI_MUX, 1u << PIN_SPI_SCK,  APP_IO_MODE_MUX, APP_IO_NOPULL, APP_SPI_PIN_ENABLE };
  p.pin_cfg.mosi = { APP_IO_TYPE_NORMAL, PIN_SPI_MUX, 1u << PIN_SPI_MOSI, APP_IO_MODE_MUX, APP_IO_NOPULL, APP_SPI_PIN_ENABLE };
  p.pin_cfg.miso = { APP_IO_TYPE_NORMAL, PIN_SPI_MUX, 1u << PIN_SPI_MISO, APP_IO_MODE_MUX, APP_IO_NOPULL, APP_SPI_PIN_ENABLE };
  p.init.data_size     = SPI_DATASIZE_8BIT;
  p.init.clock_polarity = (mode == arduino::SPI_MODE2 || mode == arduino::SPI_MODE3) ? SPI_POLARITY_HIGH : SPI_POLARITY_LOW;
  p.init.clock_phase    = (mode == arduino::SPI_MODE1 || mode == arduino::SPI_MODE3) ? SPI_PHASE_2EDGE : SPI_PHASE_1EDGE;
  uint32_t div = (SystemCoreClock + clock - 1) / clock;   /* jamais plus rapide que demandé */
  if (div < 2) div = 2;
  if (div & 1) div++;
  p.init.baudrate_prescaler = div;
  p.init.ti_mode      = SPI_TIMODE_DISABLE;
  p.init.slave_select = SPI_SLAVE_SELECT_0;
  p.is_soft_cs        = false;
  err = app_spi_init(&p, spi_evt);
  ready = (err == APP_DRV_SUCCESS);
  cur_clock = clock;
  cur_mode = mode;
}

void SPIClass::begin()
{
  if (!ready) configure(cur_clock ? cur_clock : SPI_DEFAULT_HZ, cur_mode);
}

void SPIClass::end()
{
  if (ready) app_spi_deinit(APP_SPI_ID_MASTER);
  ready = false;
}

void SPIClass::beginTransaction(arduino::SPISettings s)
{
  uint32_t clock = s.getClockFreq();
  if (clock > SPI_DEFAULT_HZ) clock = SPI_DEFAULT_HZ;   /* SPIM du GR551x : on reste à la fréquence validée */
  if (!ready || clock != cur_clock || s.getDataMode() != cur_mode) configure(clock, s.getDataMode());
}

void SPIClass::transfer(const void *tx, void *rx, size_t count)
{
  if (!ready || !count) return;
  uint16_t e = app_spi_transmit_receive_sync(APP_SPI_ID_MASTER, (uint8_t *)tx, (uint8_t *)rx, (uint32_t)count, 100);
  if (e != APP_DRV_SUCCESS && !err) err = e;
}

uint8_t SPIClass::transfer(uint8_t data)
{
  uint8_t in = 0;
  transfer(&data, &in, 1);
  return in;
}

uint16_t SPIClass::transfer16(uint16_t data)
{
  uint8_t out[2] = { (uint8_t)(data >> 8), (uint8_t)data };
  uint8_t in[2] = { 0, 0 };
  transfer(out, in, 2);
  return (uint16_t)((in[0] << 8) | in[1]);
}

void SPIClass::transfer(void *buf, size_t count)
{
  transfer(buf, buf, count);
}
