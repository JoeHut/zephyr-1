#include <zephyr.h>
#include <misc/reboot.h>
#include <device.h>
#include <string.h>
#include <nvs/nvs.h>
#include <entropy.h>
#include <flash.h>

#include <logging/log.h>
LOG_MODULE_REGISTER(nvs_test);

#define NVS_SECTOR_SIZE FLASH_ERASE_BLOCK_SIZE /* Multiple of FLASH_PAGE_SIZE */
#define NVS_SECTOR_COUNT 4 /* At least 2 sectors */
#define NVS_STORAGE_OFFSET FLASH_AREA_STORAGE_OFFSET /* Start address of the
						      * filesystem in flash
						      */
#define NVS_STORAGE_SIZE FLASH_AREA_STORAGE_SIZE
#define NVS_STORAGE_FLASH_DEV_NAME DT_FLASH_DEV_NAME

#define ERASE_FLASH false
#define NVS_TEST_ITEM_SIZE 6
#define NVS_TEST_ITEM_ID 1
#define NVS_WRITE_COUNT_ID 2

#define WRITES_PER_CYCLE 10
#define SLEEP_TIME K_MSEC(300)

static struct nvs_fs fs = {
	.sector_size = NVS_SECTOR_SIZE,
	.sector_count = NVS_SECTOR_COUNT,
	.offset = NVS_STORAGE_OFFSET,
};

static u8_t test_data[NVS_TEST_ITEM_SIZE];

int randomize(u8_t *data, size_t len)
{
	int ret;
	struct device *dev = device_get_binding(CONFIG_ENTROPY_NAME);

	if (dev == NULL) {
		LOG_ERR("Entropy device not found");
		return -ENODEV;
	}

	ret = entropy_get_entropy(dev, data, len);
	if (ret != 0) {
		LOG_ERR("Failed to get Entropy with %d", ret);
		return ret;
	}

	return ret;
}

int erase_flash_storage_partition(void)
{
	struct device *flash_dev;
	int ret;

	flash_dev = device_get_binding(NVS_STORAGE_FLASH_DEV_NAME);
	if (flash_dev == NULL) {
		LOG_ERR("Failed to get flash_dev");
		return -ENODEV;
	}
	ret = flash_write_protection_set(flash_dev, false);
	if (ret != 0) {
		LOG_ERR("Disabling write protection failed with %d", ret);
		return ret;
	}

	ret = flash_erase(flash_dev, NVS_STORAGE_OFFSET, NVS_STORAGE_SIZE);
	if (ret != 0) {
		LOG_ERR("Erasing NVS flash partition failed with %d", ret);
		return ret;
	}

	return ret;
}

void main(void)
{
	int ret;

	s32_t start_time;
	s32_t end_time;
	u32_t number_of_writes;
	ssize_t read_len;

	start_time = k_uptime_get_32();
	LOG_INF("Start init at %dms", start_time);

#if ERASE_FLASH
	ret = erase_flash_storage_partition();
	if (ret != 0) {
		LOG_ERR("Erasing flash failed with %d", ret);
		return;
	}

	while (1) {
		LOG_DBG("Flash erased. Spinning...");
		k_sleep(1000);
	}
#endif /* ERASE_FLASH */

	ret = nvs_init(&fs, NVS_STORAGE_FLASH_DEV_NAME);
	if (ret) {
		LOG_ERR("Init NVS failed with %d", ret);
		return;
	}

	read_len = nvs_read(&fs, 2, &number_of_writes,
			sizeof(number_of_writes));
	if (read_len < 0) {
		number_of_writes = 0;
		nvs_write(&fs, 2, &number_of_writes,
				sizeof(number_of_writes));
	}

	end_time = k_uptime_get_32();
	LOG_INF("Init took %dms", end_time - start_time);

	for (int i = 0; i < WRITES_PER_CYCLE; i++) {
		ret = randomize(test_data, sizeof(test_data));
		if (ret) {
			LOG_ERR("Failed to randomize data with %d", ret);
			return;
		}

		ret = nvs_write(&fs, NVS_TEST_ITEM_ID,
				test_data, sizeof(test_data));
		if (ret < 0) {
			LOG_ERR("Writing data failed with %d", ret);
			return;
		}

		if (ret == 0) {
			LOG_ERR("Value has been written already");
		}

		if (ret != sizeof(test_data)) {
			LOG_ERR("Expected to write %d bytes, "
					"but only %d have been written",
					sizeof(test_data), ret);
		}

		number_of_writes++;
	}

	ret = nvs_write(&fs, NVS_WRITE_COUNT_ID,
			&number_of_writes, sizeof(number_of_writes));
	LOG_ERR("Wrote %d junks of %d bytes\n",
			number_of_writes, sizeof(test_data));

	k_sleep(SLEEP_TIME);
	sys_reboot(0);
}
