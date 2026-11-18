#pragma once

namespace ukiyoemount {

// Deliberately activated BENCH-ONLY GPIO and UART self-tests.
// Never connect this firmware to an E-mount camera while a bench test runs.
bool bench_gpio_test();
bool bench_uart_session(bool extended);

}  // namespace ukiyoemount
