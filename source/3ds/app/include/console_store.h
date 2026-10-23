#ifndef N3DSRP_CONSOLE_STORE_H
#define N3DSRP_CONSOLE_STORE_H
#include "pair_store.h"
/* Initialize to zero before use; owns its records. Do not shallow-copy. */
typedef struct { uint32_t selected, count, capacity; PairRecord *records; } ConsoleStore;
void console_store_free(ConsoleStore *store);
bool console_store_clone(ConsoleStore *destination, const ConsoleStore *source);
void console_store_rollback(ConsoleStore *store, ConsoleStore *backup);
bool console_store_edit_address(ConsoleStore *store, unsigned index, const char *address);
StoreResult console_store_load(const char *path, ConsoleStore *store);
bool console_store_save(const char *path, const ConsoleStore *store);
bool console_store_add(ConsoleStore *store, const PairRecord *record);
bool console_address_is_public(const char *address);
bool console_store_add_address(ConsoleStore *store, unsigned index, const char *address);
bool console_store_remove(ConsoleStore *store, unsigned index);
#endif
