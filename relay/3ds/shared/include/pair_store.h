#ifndef RP_PAIR_STORE_H
#define RP_PAIR_STORE_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
/* Stable Chiaki target IDs. PS4 system software 8.0+ uses target 1000. */
static inline bool pair_target_supported(uint32_t target) {
    return target == 1000 || target == 1000100;
}
/* PS4 key type is opaque registration metadata, unused by session crypto. */
static inline bool pair_key_supported(uint32_t target, uint32_t key_type) {
    return pair_target_supported(target) && (target == 1000 || key_type == 2);
}
typedef struct {
    char address[64], identity[17], nickname[32];
    uint8_t account[8], regist_key[16], key[16], mac[6];
    uint32_t target, key_type, generation;
    bool legacy;
    bool direct; /* Legacy persisted preference; automatic startup ignores it. */
} PairRecord;
typedef enum { STORE_OK, STORE_MISSING, STORE_INVALID, STORE_IO } StoreResult;
/* CRC detects accidental SD corruption, not malicious modification. */
StoreResult pair_store_load(const char *path, PairRecord *record);
bool pair_store_save(const char *path, const PairRecord *record);
bool store_recover(const char *path);
bool store_remove(const char *path);
bool store_atomic_write(const char *path, const void *data, size_t size);
void secret_clear(void *data, size_t size);
#endif
