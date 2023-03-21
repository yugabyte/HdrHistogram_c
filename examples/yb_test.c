/**
 * yb_test.c
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

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4996)
#endif


int main()
{
    struct hdr_histogram* histogram = (struct hdr_histogram*) calloc(1, sizeof(struct hdr_histogram) + 176*4);

    hdr_init_pgss(1, 16777215, 2, histogram);
    // hdr_init(1, 16777215, 1, histogram); // original hdr_init still takes in significant figures

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
    char * result;
    result = malloc(2000);

    hdr_iter_init(&iter, histogram);
    while(hdr_iter_next(&iter))
    {
        char buf[100];
        snprintf(buf, 100,"index: %d, [%lld-%lld), count: %d \n",iter.counts_index, iter.value_iterated_to, iter.highest_equivalent_value + 1, iter.count);
        strncat(result, buf, strlen(buf));
    }
    printf("%s \n", result);

    int64_t p90 = hdr_value_at_percentile(histogram, 90);
    int64_t p50 = hdr_value_at_percentile(histogram, 50);
    int64_t p99 = hdr_value_at_percentile(histogram, 99);

    printf("p99: %lld, p90: %lld, p50: %lld \n", p99, p90, p50);

    printf("max value: %lld, highest_trackable_value: %lld \n", histogram->max_value, histogram->highest_trackable_value);
    int derived_max_mag = histogram->sub_bucket_half_count_magnitude + 1 + histogram->bucket_count - 1;
    int derived_max = pow(2, derived_max_mag);
    printf("derived max: %d \n", derived_max);

    return 0;
}

#if defined(_MSC_VER)
#pragma warning(pop)
#endif
