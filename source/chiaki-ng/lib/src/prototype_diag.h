/* Structured, metadata-only hooks for the separately built original 3DS prototype.
 * Normal builds retain no diagnostic calls. Never pass protocol payloads or secrets. */
#ifndef CHIAKI_PROTOTYPE_DIAG_H
#define CHIAKI_PROTOTYPE_DIAG_H
#ifdef CHIAKI_OLD3DS_DIAGNOSTICS
void prototype_log(const char *event,const char *format,...) __attribute__((format(printf,2,3)));
#define CORE_TRACE(...) prototype_log(__VA_ARGS__)
#else
#define CORE_TRACE(...) ((void)0)
#endif
#endif
