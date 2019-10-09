#ifndef __COMMON_H__
#define __COMMON_H__

#include "tp_types.h"

#ifdef _LINUX
	#define MULTI_LINE_MACRO_BEGIN do {
	#define MULTI_LINE_MACRO_END \
			} while(0) 
	#define ATTR_SEC_MOD1 __attribute__((section(".mod1")))
#else
	#define MULTI_LINE_MACRO_BEGIN do {
	#define MULTI_LINE_MACRO_END \
	__pragma(warning(push)) \
	__pragma(warning(disable:4127)) \
	} while(0) \
	__pragma(warning(pop))

	#define ATTR_SEC_MOD1
#endif


#define BITS(FROM, LEN) ((1ULL << ((LEN) - 1)) << (FROM))
#define SUBST(VALUE, FROM, LEN, FIELD) (VALUE = ((VALUE) & ~BITS(FROM, LEN)) | (((FIELD) << (FROM)) & BITS(FROM, LEN)))

#ifndef BIT
	#define BIT(X) (1UL << (X))
#endif

#ifndef ROUND_TO_PAGES
#define ROUND_TO_PAGES(Size)  (((size_t)(Size) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))
#endif

#ifndef MAX
#define MAX(X, Y)  (((X) > (Y)) ? (X) : (Y))
#endif

#ifndef MIN
#define MIN(X, Y)  (((X) < (Y)) ? (X) : (Y))
#endif

#ifdef __cplusplus
extern "C" {
#endif

	void __cdecl TPmemset(void * _Dst, int _Val, size_t _Size);
	void TPmemcpy(void *dst, const void *src, size_t size);
	size_t TPstrlen(const char *str);
	int TPmemcmp(const void * _Buf1, const void * _Buf2, size_t _Size);
	unsigned char get_cur_apic_id(void);
	unsigned long long get_ticks_per_second(void);
    
    BOOLEAN is_write_protect_enabled(void);
    void disable_write_protect(void);
    void enable_write_protect(void);

    BOOLEAN is_supervisor_exec_prot_enabled(void);
    void disable_supervisor_exec_prot(void);
    void enable_supervisor_exec_prot(void);

    BOOLEAN is_supervisor_access_prot_enabled(void);
    void disable_supervisor_access_prot(void);
    void enable_supervisor_access_prot(void);

	wchar_t* describe_status(OSSTATUS status);

#ifdef __cplusplus
}
#endif

#define TP_ALLOC(TYPE) ((TYPE*)tp_alloc(sizeof(TYPE)))
#define TP_ALLOC_ALIGNED(TYPE) ((TYPE*)tp_alloc(ROUND_TO_PAGES(sizeof(TYPE))))

#ifdef ARRAY_SIZE
	#undef ARRAY_SIZE
#endif
#define ARRAY_SIZE(x) (sizeof(x) / sizeof(*(x)))


#endif
