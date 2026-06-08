// Seeed XIAO ESP32C6: Configure GPIO 3 and GPIO 14 as outputs for Wifi antenna switch
{
  gpio_config_t board_io_conf = {
      .pin_bit_mask = (1ULL << 3) | (1ULL << 14),
      .mode = GPIO_MODE_OUTPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE};
  gpio_config(&board_io_conf);

  // Set GPIO 3 and GPIO 14 to low
  gpio_set_level(3, 0);
  gpio_set_level(14, 0);
}