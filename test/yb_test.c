/**
 * yb_test.c
 * Copyright (c) YugaByte, Inc.
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
 * in compliance with the License. You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 * Unless required by applicable law or agreed to in writing, software distributed under the License
 * is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
 * or implied. See the License for the specific language governing permissions and limitations
 * under the License.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <math.h>
#include <time.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <unistd.h>

#include <hdr/hdr_histogram.h>
#include <hdr/hdr_histogram_log.h>

#include "minunit.h"
#include "hdr_test_util.h"

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4996)
#endif

int tests_run = 0;

static char* yb_test_create(void)
{
    // counts array not allocated here, would need calloc(sizeof(hdr_histogram) + 176 * 4) for full allocation
    #ifdef FLEXIBLE_COUNTS_ARRAY
    hdr_histogram* histogram = (struct hdr_histogram*) calloc(1, sizeof(struct hdr_histogram));
    int r = yb_hdr_init(1, 16777215, 16, histogram);
    #else
    struct hdr_histogram* histogram;
    int r = hdr_init(1, 16777215, 1, &histogram);
    #endif


    mu_assert("Failed to allocate hdr_histogram", r == 0);
    mu_assert("Failed to allocate hdr_histogram", histogram != NULL);

    #ifdef FLEXIBLE_COUNTS_ARRAY
    mu_assert("Incorrect array length", compare_int64(histogram->counts_len, 176));
    #else
    mu_assert("Incorrect array length", compare_int64(histogram->counts_len, 336));
    #endif

    free(histogram);

    return 0;
}

static char* test_create_with_large_values(void)
{
    struct hdr_histogram* h = NULL;
    int r = hdr_init(20000000, 100000000, 5, &h);
    mu_assert("Didn't create", r == 0);

    hdr_record_value(h, 100000000);
    hdr_record_value(h, 20000000);
    hdr_record_value(h, 30000000);

    mu_assert(
        "50.0% Percentile",
        hdr_values_are_equivalent(h, 20000000, hdr_value_at_percentile(h, 50.0)));

    mu_assert(
        "83.33% Percentile",
        hdr_values_are_equivalent(h, 30000000, hdr_value_at_percentile(h, 83.33)));

    mu_assert(
        "83.34% Percentile",
        hdr_values_are_equivalent(h, 100000000, hdr_value_at_percentile(h, 83.34)));

    mu_assert(
        "99.0% Percentile",
        hdr_values_are_equivalent(h, 100000000, hdr_value_at_percentile(h, 99.0)));

    return 0;
}

static struct mu_result all_tests(void)
{
    mu_run_test(yb_test_create);
    mu_ok;
}

static int hdr_histogram_run_tests(void)
{
    struct mu_result result = all_tests();

    if (result.message != 0)
    {
        printf("hdr_histogram_test.%s(): %s\n", result.test, result.message);
    }
    else
    {
        printf("ALL TESTS PASSED\n");
    }

    printf("Tests run: %d\n", tests_run);

    return result.message == NULL ? 0 : -1;
}

int main()
{
    #ifdef FLEXIBLE_COUNTS_ARRAY
    struct hdr_histogram* histogram = (struct hdr_histogram*) calloc(1, sizeof(struct hdr_histogram) + 176*4);
    yb_hdr_init(1, 16777215, 16, histogram);
    #else
    struct hdr_histogram* histogram;
    hdr_init(1, 16777215, 1, &histogram);
    #endif

    printf("subbucket count: %d \n", histogram->sub_bucket_count);
    printf("bucket count: %d \n", histogram->bucket_count);

    hdr_record_value(histogram, 1);
    hdr_record_value(histogram, 2);
    hdr_record_value(histogram, 3);
    hdr_record_value(histogram, 4);
    hdr_record_value(histogram, 4);
    hdr_record_value(histogram, 31);
    hdr_record_value(histogram, 32);
    hdr_record_value(histogram, 33);
    hdr_record_value(histogram, 8388607);
    hdr_record_value(histogram, 8388608);
    hdr_record_value(histogram, 9000000);
    hdr_record_value(histogram, 16777215);
    hdr_record_values(histogram, 1, 4);
    hdr_record_value(histogram, 2);
    hdr_record_value(histogram, 8);

    printf("total number of subbuckets: %d \n", histogram->counts_len);

    int mem = hdr_get_memory_size(histogram);
    printf("Footprint: histogram struct size: %lu, total size: %d \n", sizeof(struct hdr_histogram), mem);

    // printf("\nPercentiles Printing\n\n");
    // hdr_percentiles_print(histogram,stdout,5,1.0);

    printf("\nPrinting \n\n");
    struct hdr_iter iter;

    hdr_iter_init(&iter, histogram);
    while(hdr_iter_next(&iter))
    {
        printf("index: %d, [%lld-%lld), count: %d \n",iter.counts_index, iter.value_iterated_to, iter.highest_equivalent_value + 1, iter.count);
    }

    int64_t p90 = hdr_value_at_percentile(histogram, 90);
    int64_t p50 = hdr_value_at_percentile(histogram, 50);
    int64_t p99 = hdr_value_at_percentile(histogram, 99);

    printf("p99: %lld, p90: %lld, p50: %lld \n", p99, p90, p50);


    // max value adjustment calculations
    printf("max value: %lld, highest_trackable_value: %lld \n", histogram->max_value, histogram->highest_trackable_value);
    int derived_max_mag = histogram->sub_bucket_half_count_magnitude + 1 + histogram->bucket_count - 1;
    int derived_max = pow(2, derived_max_mag);
    printf("derived max: %d \n", derived_max);

    int prelim_max_value = 3000 / 0.1;
    printf("prelim_max_value: %d \n", prelim_max_value);

    #ifdef FLEXIBLE_COUNTS_ARRAY
    struct hdr_histogram* dummy = (struct hdr_histogram*) calloc(1, sizeof(struct hdr_histogram));
    yb_hdr_init(1, prelim_max_value, 8, dummy);
    #else
    struct hdr_histogram* dummy;
    hdr_init(1, prelim_max_value, 1, &dummy);
    #endif

    int dummy_derived = dummy->sub_bucket_half_count_magnitude + dummy->bucket_count;
    int yb_hdr_max_value = pow(2, dummy_derived) - 1;
    float yb_hdr_max_latency_ms = yb_hdr_max_value * 0.1;
    printf("prelim_max_value: %d, yb_hdr_max_value: %d, yb_hdr_max_latency_ms: %f \n", prelim_max_value, yb_hdr_max_value, yb_hdr_max_latency_ms);
    
    return hdr_histogram_run_tests();
}

#if defined(_MSC_VER)
#pragma warning(pop)
#endif
