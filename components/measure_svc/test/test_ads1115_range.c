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

#include "../measure_prov_ads1115_range.h"
#include "measure_provider.h"

#include "unity.h"

TEST_CASE("ADS1115 default autorange policy uses exact hysteresis", "[measurement][range]")
{
    TEST_ASSERT_TRUE(measure_prov_ads1115_range_config_validate());
    uint16_t pgas[4] = {0};
    TEST_ASSERT_EQUAL_UINT32(3U, measure_prov_ads1115_get_configured_pgas(pgas, 4U));
    TEST_ASSERT_EQUAL_UINT16(256U, pgas[0]);
    TEST_ASSERT_EQUAL_UINT16(512U, pgas[1]);
    TEST_ASSERT_EQUAL_UINT16(2048U, pgas[2]);

    uint8_t next = 0U;
    TEST_ESP_OK(measure_prov_ads1115_range_select(0U, 571U, &next));
    TEST_ASSERT_EQUAL_UINT8(0U, next);
    TEST_ESP_OK(measure_prov_ads1115_range_select(0U, 572U, &next));
    TEST_ASSERT_EQUAL_UINT8(1U, next);
    TEST_ESP_OK(measure_prov_ads1115_range_select(1U, 501U, &next));
    TEST_ASSERT_EQUAL_UINT8(1U, next);
    TEST_ESP_OK(measure_prov_ads1115_range_select(1U, 500U, &next));
    TEST_ASSERT_EQUAL_UINT8(0U, next);

    TEST_ESP_OK(measure_prov_ads1115_range_select(1U, 4159U, &next));
    TEST_ASSERT_EQUAL_UINT8(1U, next);
    TEST_ESP_OK(measure_prov_ads1115_range_select(1U, 4160U, &next));
    TEST_ASSERT_EQUAL_UINT8(2U, next);
    TEST_ESP_OK(measure_prov_ads1115_range_select(2U, 3841U, &next));
    TEST_ASSERT_EQUAL_UINT8(2U, next);
    TEST_ESP_OK(measure_prov_ads1115_range_select(2U, 3840U, &next));
    TEST_ASSERT_EQUAL_UINT8(1U, next);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
        measure_prov_ads1115_range_select(3U, 0U, &next));
}

TEST_CASE("ADS1115 raw conversion follows sample PGA", "[measurement][range]")
{
    const measure_provider_t *provider = measure_prov_ads1115_get();
    TEST_ASSERT_EQUAL_UINT32(1280U,
        provider->raw_code_to_value_u4(16384, 256U));
    TEST_ASSERT_EQUAL_UINT32(2560U,
        provider->raw_code_to_value_u4(16384, 512U));
    TEST_ASSERT_EQUAL_UINT32(10240U,
        provider->raw_code_to_value_u4(16384, 2048U));
}

TEST_CASE("ADS1115 lowest range gets extra settling conversions", "[measurement][range]")
{
    TEST_ASSERT_EQUAL_UINT8(3U, measure_prov_ads1115_settling_conversions(256U));
    TEST_ASSERT_EQUAL_UINT8(1U, measure_prov_ads1115_settling_conversions(512U));
    TEST_ASSERT_EQUAL_UINT8(1U, measure_prov_ads1115_settling_conversions(2048U));
}
