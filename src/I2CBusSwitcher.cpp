#include "I2CBusSwitcher.h"
#include "driver/gpio.h"

void I2CBusSwitcher::switchTo(int sda, int scl) {
    if (currentSda == sda && currentScl == scl) {
        return;
    }

    if (currentSda >= 0) {
        gpio_reset_pin((gpio_num_t)currentSda);
    }

    if (currentScl >= 0) {
        gpio_reset_pin((gpio_num_t)currentScl);
    }

    Wire1.end();
    Wire1.begin(sda, scl);

    currentSda = sda;
    currentScl = scl;
}