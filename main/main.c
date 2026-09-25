/* avocatOS entry point. */
#include "esp_log.h"
#include "avo_board.h"

void app_main(void)
{
    ESP_ERROR_CHECK(avo_board_init());
    ESP_ERROR_CHECK(avo_board_start_ui());
    ESP_LOGI("avocatOS", "running");
}
