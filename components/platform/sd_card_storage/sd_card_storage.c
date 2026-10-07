#include <inttypes.h>
#include <stdio.h>
#include "driver/spi_common.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdkconfig.h"
#include "sd_card_storage.h"
#include "sdmmc_cmd.h"

static const char *TAG = "sd_card_storage";
static const char *SD_CARD_MOUNT_POINT = "/sdcard";

static bool s_initialized = false;
static bool s_mounted = false;
static sdmmc_card_t *s_card = NULL;

static spi_host_device_t sd_card_storage_host_from_config(void)
{
    return (CONFIG_SD_CARD_STORAGE_SPI_HOST == 3) ? SPI3_HOST : SPI2_HOST;
}

esp_err_t sd_card_storage_init(void)
{
#if CONFIG_SD_CARD_STORAGE_ENABLED
    if (s_initialized) {
        return s_mounted ? ESP_OK : ESP_FAIL;
    }

    spi_host_device_t spi_host = sd_card_storage_host_from_config();
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = spi_host;

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = CONFIG_SD_CARD_STORAGE_PIN_MOSI,
        .miso_io_num = CONFIG_SD_CARD_STORAGE_PIN_MISO,
        .sclk_io_num = CONFIG_SD_CARD_STORAGE_PIN_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096,
        .flags = SPICOMMON_BUSFLAG_MASTER,
        .isr_cpu_id = ESP_INTR_CPU_AFFINITY_AUTO,
        .intr_flags = 0,
    };
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = CONFIG_SD_CARD_STORAGE_PIN_CS;
    slot_config.host_id = spi_host;

    esp_vfs_fat_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = CONFIG_SD_CARD_STORAGE_MOUNT_MAX_FILES,
        .allocation_unit_size = CONFIG_SD_CARD_STORAGE_ALLOCATION_UNIT_SIZE,
        .disk_status_check_enable = false,
        .use_one_fat = false,
    };

    ESP_RETURN_ON_ERROR(spi_bus_initialize(spi_host, &bus_cfg, SDSPI_DEFAULT_DMA),
                        TAG,
                        "sd spi bus init failed");

    esp_err_t err = esp_vfs_fat_sdspi_mount(SD_CARD_MOUNT_POINT,
                                            &host,
                                            &slot_config,
                                            &mount_config,
                                            &s_card);
    if (err != ESP_OK) {
        spi_bus_free(spi_host);
        return err;
    }

    s_initialized = true;
    s_mounted = true;

    uint64_t capacity_mb = 0;
    if (s_card != NULL) {
        capacity_mb = ((uint64_t)s_card->csd.capacity) * s_card->csd.sector_size / (1024ULL * 1024ULL);
    }

    ESP_LOGI(TAG,
             "SD card mounted: host=SPI%d SCLK=%d MOSI=%d MISO=%d CS=%d mount=%s capacity=%" PRIu64 "MB",
             CONFIG_SD_CARD_STORAGE_SPI_HOST,
             CONFIG_SD_CARD_STORAGE_PIN_SCLK,
             CONFIG_SD_CARD_STORAGE_PIN_MOSI,
             CONFIG_SD_CARD_STORAGE_PIN_MISO,
             CONFIG_SD_CARD_STORAGE_PIN_CS,
             SD_CARD_MOUNT_POINT,
             capacity_mb);
    if (s_card != NULL) {
        sdmmc_card_print_info(stdout, s_card);
    }

    return ESP_OK;
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

bool sd_card_storage_is_mounted(void)
{
    return s_mounted;
}

esp_err_t sd_card_storage_deinit(void)
{
#if CONFIG_SD_CARD_STORAGE_ENABLED
    if (!s_mounted) {
        return ESP_OK;
    }

    esp_err_t err = esp_vfs_fat_sdcard_unmount(SD_CARD_MOUNT_POINT, s_card);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SD card unmount reported %s; releasing anyway", esp_err_to_name(err));
    }
    /* The bus is released whatever the unmount said: keeping it would make the
       next init fail on a bus that is already taken. */
    esp_err_t bus_err = spi_bus_free(sd_card_storage_host_from_config());
    if (bus_err != ESP_OK) {
        ESP_LOGW(TAG, "SD card SPI bus free failed: %s", esp_err_to_name(bus_err));
    }

    s_card = NULL;
    s_mounted = false;
    s_initialized = false;
    return err != ESP_OK ? err : bus_err;
#else
    return ESP_OK;
#endif
}

esp_err_t sd_card_storage_probe(void)
{
#if CONFIG_SD_CARD_STORAGE_ENABLED
    if (!s_mounted || s_card == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    size_t sector_size = s_card->csd.sector_size;
    if (sector_size == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    /* DMA-capable, so the driver does not allocate a bounce buffer per probe. */
    void *sector = heap_caps_malloc(sector_size, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (sector == NULL) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = sdmmc_read_sectors(s_card, sector, 0, 1);
    heap_caps_free(sector);
    return err;
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t sd_card_storage_get_space(uint64_t *total_bytes, uint64_t *free_bytes)
{
#if CONFIG_SD_CARD_STORAGE_ENABLED
    ESP_RETURN_ON_FALSE(total_bytes != NULL && free_bytes != NULL, ESP_ERR_INVALID_ARG, TAG, "outputs required");
    ESP_RETURN_ON_FALSE(s_mounted, ESP_ERR_INVALID_STATE, TAG, "SD card is not mounted");
    return esp_vfs_fat_info(SD_CARD_MOUNT_POINT, total_bytes, free_bytes);
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

const char *sd_card_storage_get_mount_point(void)
{
    return SD_CARD_MOUNT_POINT;
}
