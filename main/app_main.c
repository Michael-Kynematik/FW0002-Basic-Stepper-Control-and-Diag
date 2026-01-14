#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void app_main(void)
{
    setvbuf(stdin, NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IONBF, 0);

    printf("\r\nCDC echo ready on COM6. Type and press Enter.\r\n");

    while (1) {
        int c = fgetc(stdin);          // blocks until a char arrives over CDC console
        if (c >= 0) {
            printf("<%c>", (char)c);   // distinctive output proves it's from the ESP
        }
        vTaskDelay(pdMS_TO_TICKS(1));  // yield so WDT/IDLE are happy
    }
}
