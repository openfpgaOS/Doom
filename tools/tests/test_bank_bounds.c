/* Exercise the production bank binder with bounded and malformed preloads. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "of_services.h"
static struct of_services_table services;
#undef OF_SVC
#define OF_SVC (&services)
#include BANK_SOURCE

int main(int argc, char **argv)
{
    assert(argc == 2 || argc == 3);
    const unsigned prefix = sizeof(ofsf_header_t) + OFSF_PRESET_COUNT * sizeof(ofsf_preset_t);
    unsigned size = prefix + sizeof(ofsf_zone_t) + 32;
    uint8_t *data = calloc(1, size);
    assert(data);
    ofsf_header_t *h = (ofsf_header_t *)data;
    ofsf_preset_t *p = (ofsf_preset_t *)(data + sizeof(*h));
    ofsf_zone_t *z = (ofsf_zone_t *)(data + prefix);
    h->magic = OFSF_MAGIC; h->version = OFSF_VERSION;
    h->sample_rate = 44100; h->zone_count = 1;
    h->sample_data_offset = prefix + sizeof(*z); h->sample_data_size = 32;
    p[0].zone_count = 1;
    z->sample_length = 16; z->key_hi = z->vel_hi = 127;
    z->loop_mode = OFSF_LOOP_FORWARD; z->loop_start = 2; z->loop_end = 16;
    int valid = !strcmp(argv[1], "valid");
    if (!strcmp(argv[1], "zone-wrap")) h->zone_count = 0x10000000;
    else if (!strcmp(argv[1], "metadata-overlap")) h->sample_data_offset = 0;
    else if (!strcmp(argv[1], "truncated-zone")) size = prefix + sizeof(*z) - 1;
    else if (!strcmp(argv[1], "truncated-header")) size = sizeof(*h) - 1;
    else if (!strcmp(argv[1], "sample-range")) z->sample_length = 17;
    else if (!strcmp(argv[1], "sample-wrap")) z->sample_length = UINT32_MAX;
    else if (!strcmp(argv[1], "sample-offset")) z->sample_offset = UINT32_MAX - 1;
    else if (!strcmp(argv[1], "loop-range")) z->loop_end = 17;
    else if (!strcmp(argv[1], "loop-empty")) z->loop_start = z->loop_end;
    else if (!strcmp(argv[1], "preset-range")) p[0].zone_start = 1;
    else if (!strcmp(argv[1], "odd-offset")) z->sample_offset = 1;
    else if (!strcmp(argv[1], "odd-blob")) h->sample_data_size = 31;
    else if (!strcmp(argv[1], "zero-rate")) h->sample_rate = 0;
    else if (!strcmp(argv[1], "file")) {
        assert(argc == 3);
        FILE *fp = fopen(argv[2], "rb"); assert(fp);
        assert(!fseek(fp, 0, SEEK_END));
        long len = ftell(fp); assert(len > 0 && (unsigned long)len <= UINT32_MAX);
        rewind(fp); free(data); size = len; data = malloc(size); assert(data);
        assert(fread(data, 1, size, fp) == size); fclose(fp); valid = 1;
    } else assert(valid);
    services.magic = OF_SVC_MAGIC;
    services.smp_bank_preload_base = data;
    services.smp_bank_preload_size = size;
    int result = of_smp_bank_bind_preloaded();
    assert((result == 1) == valid);
    if (valid) {
        assert(of_smp_bank_get() && of_smp_bank_sample_base());
        const ofsf_zone_t *found[4];
        assert(of_smp_zone_lookup(0, 0, 60, 100, found, 4) > 0);
    } else assert(!of_smp_bank_get() && !of_smp_bank_sample_base());
    free(preset_copy); free(zone_copy); free(data);
    printf("PASS: bank %s\n", argv[1]);
}
