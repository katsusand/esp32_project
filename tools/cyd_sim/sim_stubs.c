/* Symbols the host ESP-IDF stubs (test/host/stub) expect a program to define. */
#include <stdio.h>
#include "esp_err.h"

/* Read by the ESP_LOG stub macros. */
int g_log_quiet = 0;

const char *esp_err_to_name(esp_err_t err)
{
    static char buf[16];
    snprintf(buf, sizeof(buf), "0x%x", err);
    return buf;
}
