/*
 * Copyright 2026 PSU-EXT Authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "test_nvs.h"

#include <string.h>
#include "nvs.h"
#include "nvs_flash.h"

esp_err_t __real_nvs_open(const char *name, nvs_open_mode_t mode, nvs_handle_t *handle);
esp_err_t __real_nvs_open_from_partition(
    const char *partition, const char *name, nvs_open_mode_t mode, nvs_handle_t *handle);

/* Always active for the entire test executable, including worker tasks and
 * failed/aborted tests. No backup/restore window can expose saved calibration. */
esp_err_t __wrap_nvs_open(const char *name, nvs_open_mode_t mode, nvs_handle_t *handle)
{
    const char *isolated = name != NULL && strcmp(name, "meas_cfg") == 0 ?
        TEST_MEASURE_NVS_NAMESPACE : name;
    return __real_nvs_open(isolated, mode, handle);
}

esp_err_t __wrap_nvs_open_from_partition(
    const char *partition, const char *name, nvs_open_mode_t mode, nvs_handle_t *handle)
{
    const char *isolated = name != NULL && strcmp(name, "meas_cfg") == 0 ?
        TEST_MEASURE_NVS_NAMESPACE : name;
    return __real_nvs_open_from_partition(partition, isolated, mode, handle);
}

/* Also block destructive recovery inside services initialized by tests. */
esp_err_t __wrap_nvs_flash_erase(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t __wrap_nvs_flash_erase_partition(const char *partition)
{
    (void)partition;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t test_nvs_init(void)
{
    return nvs_flash_init();
}

esp_err_t test_nvs_reset_measurement(void)
{
    esp_err_t err = test_nvs_init();
    if (err != ESP_OK) {
        return err;
    }
    nvs_handle_t handle;
    err = nvs_open(TEST_MEASURE_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_erase_all(handle);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}
