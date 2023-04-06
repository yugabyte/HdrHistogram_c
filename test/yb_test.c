/*
 * Copyright (c) YugaByte, Inc.
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
 * in compliance with the License. You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 * Unless required by applicable law or agreed to in writing, software distributed under the License
 * is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
 * or implied. See the License for the specific language governing permissions and limitations
 * under the License.
 */

#include <math.h>
#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>

#include "hdr/hdr_histogram.h"
#include "hdr/hdr_histogram_log.h"

#include "minunit.h"

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4996)
#endif

int tests_run = 0;
static const int YB_DEFAULT_MAX = 16777215;
static const int YB_DEFAULT_BUCKET_FACTOR = 16;

/*
 * For a given value, determines the histogram subbucket that captures this value and
 * returns the width of that subbucket.
 */
static int get_subbucket_width(hdr_histogram* h, int value)
{
    return hdr_next_non_equivalent_value(h, value) - lowest_equivalent_value(h, value);
}

/*
 * Checks that the difference between a and b is at most b * variation.
 */
static bool compare_values(double a, double b, double variation)
{
    return compare_double(a, b, b * variation);
}

/*
 * Not used for tests, but useful when printing diagnostics is desired.
 */
static char* print_subbuckets(hdr_histogram* h)
{
    struct hdr_iter iter;
    hdr_iter_init(&iter, h);
    while(hdr_iter_next(&iter))
    {
        printf("index: %d, [%lld-%lld), count: %"COUNT_PRINT_FORMAT" \n", iter.counts_index,
            iter.value_iterated_to, iter.highest_equivalent_value + 1, iter.count);
    }
    return 0;
}

static int yb_hdr_init_self_allocate(int64_t lowest_discernible_value,
    int64_t highest_trackable_value, int yb_bucket_factor, hdr_histogram** h)
{
    struct hdr_histogram_bucket_config cfg;
    hdr_histogram* histogram;

    int r = yb_hdr_calculate_bucket_config(lowest_discernible_value,
        highest_trackable_value, yb_bucket_factor, &cfg);
    if (r)
    {
        return r;
    }

    histogram = (hdr_histogram*) calloc(1, sizeof(hdr_histogram) +
        cfg.counts_len * sizeof(count_t));

    yb_hdr_init(lowest_discernible_value,
        highest_trackable_value, yb_bucket_factor, histogram);
    *h = histogram;

    return 0;
}

/* 
 * histogram initialization and counts len test
 */
static char* yb_test_create(void)
{
    /* 
     * counts array not allocated here, would need calloc(sizeof(hdr_histogram) + 176 * 
     * sizeof(count_t)) for full allocation
     */
#ifdef YB_FLEXIBLE_COUNTS_ARRAY
    hdr_histogram* h = (hdr_histogram*) calloc(1, sizeof(hdr_histogram));
    int r = yb_hdr_init(1, YB_DEFAULT_MAX, YB_DEFAULT_BUCKET_FACTOR, h);
#else
    hdr_histogram* h;
    int r = hdr_init(1, YB_DEFAULT_MAX, 1, &h);
#endif

    mu_assert("Failed to allocate hdr_histogram", r == 0);
    mu_assert("Failed to allocate hdr_histogram", h != NULL);

#ifdef YB_FLEXIBLE_COUNTS_ARRAY
    int expected_counts_len = 176;
#else
    int expected_counts_len = 336;
#endif
    mu_assert("Incorrect counts array length", compare_int64(h->counts_len, expected_counts_len));

    hdr_close(h);

    return 0;
}

/* 
 * percentile calc test ported over from hdr_histogram_test.c
 */
static char* yb_test_create_with_large_values(void)
{
    hdr_histogram* h;
#ifdef YB_FLEXIBLE_COUNTS_ARRAY
    int r = yb_hdr_init_self_allocate(20000000, 100000000, 32, &h);
#else
    int r = hdr_init(20000000, 100000000, 1, &h);
#endif

    mu_assert("Didn't create", r == 0);

    hdr_record_value(h, 100000000);
    hdr_record_value(h, 20000000);
    hdr_record_value(h, 30000000);

    mu_assert(
        "Incorrect 50.0% Percentile",
        hdr_values_are_equivalent(h, 20000000, hdr_value_at_percentile(h, 50.0)));

    mu_assert(
        "Incorrect 83.33% Percentile",
        hdr_values_are_equivalent(h, 30000000, hdr_value_at_percentile(h, 83.33)));

    mu_assert(
        "Incorrect 83.34% Percentile",
        hdr_values_are_equivalent(h, 100000000, hdr_value_at_percentile(h, 83.34)));

    mu_assert(
        "Incorrect 99.0% Percentile",
        hdr_values_are_equivalent(h, 100000000, hdr_value_at_percentile(h, 99.0)));

    hdr_close(h);

    return 0;
}

/* 
 * test invalid bucket factor (yb) or sig fig input (mainline hdr_histogram)
 */
static char* yb_test_invalid_bucket_factor(void)
{
#ifdef YB_FLEXIBLE_COUNTS_ARRAY
    hdr_histogram* h = (hdr_histogram*) calloc(1, sizeof(hdr_histogram));
    int r = yb_hdr_init(1, YB_DEFAULT_MAX, -1, h);
#else
    hdr_histogram* h = NULL;
    int r = hdr_alloc(YB_DEFAULT_MAX, -1, &h);
#endif

    mu_assert("Result was not EINVAL", r == EINVAL);

#ifndef YB_FLEXIBLE_COUNTS_ARRAY
    mu_assert("Histogram was not null", h == 0);
#endif

    hdr_close(h);

    return 0;
}

/* 
 * confirm inserts are recorded in the expected subbuckets
 * We are interested in yb default config only so we skip the body when
 * YB_FLEXIBLE_COUNTS_ARRAY is not set up - that would be a 32 sub_bucket_count histogram
 */
static char* yb_test_insert(void)
{
#if defined(YB_FLEXIBLE_COUNTS_ARRAY) && defined(YB_USE_SHORT_COUNT_TYPE)

    hdr_histogram* h;
    int r = yb_hdr_init_self_allocate(1, YB_DEFAULT_MAX, YB_DEFAULT_BUCKET_FACTOR, &h);

    mu_assert("Didn't create", r == 0);

    mu_assert("Incorrect hdr_histogram struct size", compare_int64(sizeof(hdr_histogram), 96));
    mu_assert("Incorrect hdr_histogram total size", compare_int64(hdr_get_memory_size(h), 800));
    mu_assert("Incorrect subbucket count", compare_int64(h->sub_bucket_count,
        YB_DEFAULT_BUCKET_FACTOR));
    mu_assert("Incorrect bucket count", compare_int64(h->bucket_count, 21));

    /* 
     * first bucket
     */
    hdr_record_value(h, 1);
    hdr_record_value(h, 5);

    /* 
     * bucket becomes resolution 2, 17 will be recorded as 16
     */
    hdr_record_values(h, 17, 3);

    /* 
     * second-to-last-bucket
     */
    hdr_record_value(h, 8388607);

    /* 
     * beginning of last bucket, check these values fall into same subbucket
     */
    hdr_record_value(h, 8388608);
    hdr_record_value(h, 9000000);

    /* 
     * max value and beyond max, check that 16777216 is not recorded but 16777215 is
     */
    hdr_record_value(h, YB_DEFAULT_MAX);
    hdr_record_value(h, YB_DEFAULT_MAX + 1);

    mu_assert("Incorrect first bucket value", compare_int64(h->counts[1], 1));
    mu_assert("Incorrect first bucket value", compare_int64(h->counts[5], 1));

    mu_assert("Incorrect second bucket value", compare_int64(h->counts[16], 3));

    mu_assert("Incorrect penultimate bucket value", compare_int64(h->counts[167], 1));
    mu_assert("Incorrect last bucket value", compare_int64(h->counts[168], 2));

    mu_assert("Incorrect last subbucket value", compare_int64(h->counts[h->counts_len - 1], 1));

    mu_assert("Incorrect subbucket width", compare_int64(get_subbucket_width(h, 123), 8));

    int final_subbucket_beg = YB_DEFAULT_MAX + 1 - get_subbucket_width(h, YB_DEFAULT_MAX);

    mu_assert("Incorrect final subbucket beginning", compare_int64(
        lowest_equivalent_value(h, YB_DEFAULT_MAX), final_subbucket_beg));
    mu_assert("Incorrect final subbucket end", compare_int64(
        highest_equivalent_value(h, YB_DEFAULT_MAX), YB_DEFAULT_MAX));

    mu_assert("Incorrect p50 value", compare_values(hdr_value_at_percentile(h, 50), 17, 0.001));
    mu_assert("Incorrect p90 value", compare_values(hdr_value_at_percentile(h, 90),
        9437183, 0.001));
    mu_assert("Incorrect p99 value", compare_values(hdr_value_at_percentile(h, 99),
        YB_DEFAULT_MAX, 0.001));

    hdr_close(h);

#endif

    return 0;
}

/* 
 * confirm we can calculate the actual histogram maximum for an arbitrary histogram max input 
 * like 30000, using only subbucket size and bucket count
 * we are interested in yb default config only so we skip the body when
 * YB_FLEXIBLE_COUNTS_ARRAY is not set up - that would be a 32 sub_bucket_count histogram
 */
static char* yb_test_derived_max(void)
{
#if defined(YB_FLEXIBLE_COUNTS_ARRAY) && defined(YB_USE_SHORT_COUNT_TYPE)

    const int test_max = 30000;
    hdr_histogram* h;
    int r = yb_hdr_init_self_allocate(1, test_max, YB_DEFAULT_BUCKET_FACTOR, &h);

    mu_assert("Didn't create", r == 0);

    mu_assert("Incorrect hdr_histogram total size", compare_int64(hdr_get_memory_size(h), 512));
    mu_assert("Incorrect subbucket count", compare_int64(h->sub_bucket_count,
        YB_DEFAULT_BUCKET_FACTOR));
    mu_assert("Incorrect bucket count", compare_int64(h->bucket_count, 12));
    mu_assert("Incorrect counts len", compare_int64(h->counts_len, 104));
    mu_assert("Incorrect subbucket width", compare_int64(get_subbucket_width(h, 6000), 512));

    /* 
     * check that we can derive actual max value of histogram
     */
    int h_magnitude = h->sub_bucket_half_count_magnitude + h->bucket_count;
    int derived_max = pow(2, h_magnitude) - 1;
    mu_assert("Incorrect derived_max value", compare_int64(derived_max, 32767));

    /* 
     * derived and test max should belong to same bucket, and have same subbucket sizes
     */
    mu_assert("test_max and derived_max should belong to same bucket", 
        compare_int64(get_subbucket_width(h, test_max), get_subbucket_width(h, derived_max)));
    get_subbucket_width(h, test_max);

    int final_subbucket_beg = derived_max + 1 - get_subbucket_width(h, derived_max);

    mu_assert("Incorrect final subbucket beginning", compare_int64(
        lowest_equivalent_value(h, derived_max), final_subbucket_beg));
    mu_assert("Incorrect final subbucket end", compare_int64(
        highest_equivalent_value(h, derived_max), derived_max));

    /* 
     * should not be able to record beyond derived max
     */
    hdr_record_value(h, derived_max);
    hdr_record_value(h, derived_max + 1);
    mu_assert("recorded value beyond derived_max", compare_int64(h->counts[h->counts_len - 1], 1));

    hdr_close(h);

#endif

    return 0;
}

static struct mu_result all_tests(void)
{
    mu_run_test(yb_test_create);
    mu_run_test(yb_test_create_with_large_values);
    mu_run_test(yb_test_invalid_bucket_factor);
    mu_run_test(yb_test_insert);
    mu_run_test(yb_test_derived_max);
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
    return hdr_histogram_run_tests();
}

#if defined(_MSC_VER)
#pragma warning(pop)
#endif
