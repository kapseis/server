#pragma once

//---------- Helper macros -----------

#define Stmt(S) do { S; } while (0)

#define Stringify_(S) #S
#define Stringify(S) Stringify_(S)
#define Glue_(A, B) A##B
#define Glue(A, B) Glue_(A, B)

#define OffsetOfMember(T, m) ((usize)&((T *)0)->m)
#define ParentStartPtr(P, field_name, field_ptr) ((P *)((u8 *)field_ptr - OffsetOfMember(P, field_name)))

#define ArrayCount(a) (sizeof(a) / sizeof(*(a)))

#define global   static
#define local    static
#define function static

//---------- Context cracking ----------
// This section defines the following constants/macros:
// - ARCH     (can be tested with IsArch(type))
// - OS       (can be tested with IsOs(type))
// - OS_FLAGS (can be tested with HasOsFlags(type))
// - COMPILER (can be tested with IsCompiler(type))

#if defined(__unix__) || (defined(__APPLE__) && defined(__MACH__))
# include <unistd.h>
# if defined(_POSIX_VERSION) && !defined(_POSIX_SOURCE)
#  define _POSIX_SOURCE
# endif
#endif

#if defined(_POSIX_SOURCE)
# define _POSIX_C_SOURCE 200809L
#endif

#define OS_FLAGS_NONE  0
#define OS_FLAGS_UNIX  1
#define OS_FLAGS_POSIX 2

typedef enum { 
  OsFlags_None  = OS_FLAGS_NONE,
  OsFlags_Unix  = OS_FLAGS_UNIX,
  OsFlags_Posix = OS_FLAGS_POSIX,
} OsFlags;

#define OS_LINUX   0
#define OS_WINDOWS 1
#define OS_COUNT   2

typedef enum {
  Os_Linux   = OS_LINUX,
  Os_Windows = OS_WINDOWS,
  Os_COUNT   = OS_COUNT,
} Os;

#define ARCH_X86    0
#define ARCH_X86_64 1
#define ARCH_COUNT  2

typedef enum {
  Arch_X86    = ARCH_X86,
  Arch_X86_64 = ARCH_X86_64,
  Arch_COUNT  = ARCH_COUNT,
} Arch;

#define COMPILER_GCC   0
#define COMPILER_CLANG 1
#define COMPILER_COUNT 2

typedef enum {
  Compiler_Gcc   = COMPILER_GCC,
  Compiler_Clang = COMPILER_CLANG,
  Compiler_COUNT = COMPILER_COUNT,
} Compiler;

#if defined(__linux__)
# define OS OS_LINUX
# define OS_FLAGS (OS_FLAGS_UNIX | OS_FLAGS_POSIX)
#elif defined(_WIN32) || defined(_WIN64)
# define OS OS_WINDOWS
#endif

#if defined(__amd64__) || defined(__amd64) || defined(__x86_64__) || defined(__x86_64) || defined(_M_X64) || defined(_M_AMD64)
# define ARCH ARCH_X86_64
#elif defined(i386) || defined(__i386) || defined(__i386__) || defined(__IA32__) || defined(_M_I86) || defined(_M_IX86) || defined(__X86__) || defined(_X86) || defined(__THW_INTEL__) || defined(__I86__) || defined(__INTEL__) || defined(__386)
# define ARCH ARCH_X86
#endif

#if defined(__clang__)
# define COMPILER COMPILER_CLANG
#elif defined(__GNUC__)
# define COMPILER COMPILER_GCC
#endif

#if !defined(ARCH)
# error "Unsupported architecture."
#endif

#if !defined(COMPILER)
# error "Unsupported compiler."
#endif

#if !defined(OS)
# error "Unsupported operating system."
#endif

#if !defined(OS_FLAGS)
# define OS_FLAGS OS_FLAGS_NONE
#endif

#define IsArch(type) (ARCH == type)
#define IsCompiler(type) (COMPILER == type)
#define IsOs(type) (OS == type)
#define OsHasFlags(flags) ((OS_FLAGS & (flags)) == (flags))

//---------- libc (which we'll want to get rid of later) -----------

#include <errno.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

//---------- Integer types -----------

#if OsHasFlags(OS_FLAGS_POSIX)
# include <sys/types.h>
#endif

typedef int8_t  s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef size_t  usize;
typedef ssize_t ssize;

typedef float  f32;
typedef double f64;

typedef s32 rune;

typedef void VoidFunc(void);

#define S8_MIN  INT8_MIN
#define S8_MAX  INT8_MAX
#define S16_MIN INT16_MIN
#define S16_MAX INT16_MAX
#define S32_MIN INT32_MIN
#define S32_MAX INT32_MAX
#define S64_MIN INT64_MIN
#define S64_MAX INT64_MAX

#define U8_MAX  UINT8_MAX
#define U16_MAX UINT16_MAX
#define U32_MAX UINT32_MAX
#define U64_MAX UINT64_MAX

#define USIZE_MAX SIZE_MAX
// SSIZE_MIN and SSIZE_MAX are already defined

#define RUNE_MIN S32_MIN
#define RUNE_MAX S32_MAX

//---------- Booleans ----------

typedef u8 bool;
#define true  1
#define false 0

//----------- Memory management -----------

struct Mem_Base;

typedef void *Mem_ReserveFunc(void *ctx, usize size);
typedef void  Mem_ChangeFunc(void *ctx, void *p, usize size);

typedef struct Mem_Base {
  Mem_ReserveFunc *reserve;
  Mem_ChangeFunc  *commit;
  Mem_ChangeFunc  *decommit;
  Mem_ChangeFunc  *release;
  void            *ctx;
} Mem_Base;

function void *mem_reserve(Mem_Base *mb, usize size);
function void  mem_commit(Mem_Base *mb, void *p, usize size);
function void  mem_decommit(Mem_Base *mb, void *p, usize size);
function void  mem_release(Mem_Base *mb, void *p, usize size);

function void  mem_noop_mem_change(void *ctx, void *p, usize size);

function Mem_Base *mem_malloc_base();

typedef struct {
  Mem_Base *mb;
  void **pp;
  usize size;
  void (*call)(Mem_Base *mb, void *p, usize size);
} Mem_AutoChangeContext;

function void mem_auto_change(Mem_AutoChangeContext *ctx);

#define MemAutoChange_(mb_, pp_, size_, call_) Mem_AutoChangeContext Glue(Glue(marctx_, __LINE__), _) __attribute__((__cleanup__(mem_auto_change))) = { .mb = (mb_), .pp = (void **)(pp_), .size = (size_), .call = (call_) }
#define MemAutoDecommit(mb, pp, size) MemAutoChange_(mb, pp, size, mem_decommit)
#define MemAutoRelease(mb, pp, size) MemAutoChange_(mb, pp, size, mem_release)

#define MemReserveAutoRelease(mb, target_type, target, size) target_type target = mem_reserve(mb, size); MemAutoChange_(mb, &target, size, mem_release)

//----------- Byte ordering stuff -----------

typedef enum {
  ByteOrder_BigEndian,
  ByteOrder_LittleEndian,
} ByteOrder;

#define SYSTEM_BYTE_ORDER ((union{u8 b_[2]; u16 x_;}){.b_ = {0, 1}}.x_ == 1 ? ByteOrder_BigEndian : ByteOrder_LittleEndian)

function u64 swap_byte_order_u64(u64 x);
function u32 swap_byte_order_u32(u32 x);
function u16 swap_byte_order_u16(u16 x);

#define SwapByteOrder(x) _Generic((x), u64: swap_byte_order_u64, u32: swap_byte_order_u32, u16: swap_byte_order_u16)(x)

#define SystemToBo(bo, x) ((SYSTEM_BYTE_ORDER == (bo)) ? (x) : SwapByteOrder(x))
#define BoToSystem(bo, x) ((SYSTEM_BYTE_ORDER == (bo)) ? (x) : SwapByteOrder(x))
#define SystemToLe(x) SystemToBo(ByteOrder_LittleEndian)
#define SystemToBe(x) SystemToBo(ByteOrder_BigEndian)
#define LeToSystem(x) BoToSystem(ByteOrder_LittleEndian)
#define BeToSystem(x) BoToSystem(ByteOrder_BigEndian)

//----------- Strings -----------

typedef struct {
  const u8 *buf;
  usize     len;
} String;

typedef struct StringList {
  String cur;
  struct StringList *next;
} StringList;

#define Str(str) ((String){ .buf = ((union { const char *cs; const u8 *bs; }){ .cs = (str) }).bs, .len = sizeof(str) - 1 })

function String string_from_raw(const u8 *buf, usize len);
function void   string_destroy(Mem_Base *mb, String str);

function usize cstring_length(const char *str);
function usize ststring_length(const char *str, char sentinel);

function String  string_from_c(Mem_Base *mb, const char *str);
function String  string_from_st(Mem_Base *mb, const char *str, char sentinel);
function char   *string_to_c(Mem_Base *mb, String s);
function char   *string_to_st(Mem_Base *mb, String s, char sentinel);
function s8      string_cmp(String a, String b);

#define StringCmp(a, op, b) (string_cmp(a, b) op 0)

function String string_slice(String s, usize start_at, usize len);

#define INVALID_RUNE ((rune)-1)

typedef struct {
  ByteOrder  bo;
  u16       *buf;
  usize      len;
} Utf16String;

function Utf16String utf16string_from_raw(u16 *buf, usize len, ByteOrder bo);
function void        utf16string_destroy(Mem_Base *mb, Utf16String str);

function Utf16String utf8_to_utf16(Mem_Base *mb, String s, ByteOrder bo);
function String      utf16_to_utf8(Mem_Base *mb, Utf16String s);

typedef struct {
  usize i;
} Utf8CodepointIterator;

#define UTF8_CODEPOINT_ITERATOR_INIT ((Utf8CodepointIterator){ .i = 0 })

function rune  utf8_next_codepoint(Utf8CodepointIterator *it, String s, u8 *len);
function usize utf8_rune_count();
function u8    utf8_encoded_len(rune codepoint);
function u8    utf8_encode_codepoint(rune codepoint, u8 *s);

typedef struct {
  usize i;
} Utf16CodepointIterator;

#define UTF16_CODEPOINT_ITERATOR_INIT ((Utf16CodepointIterator){ .i = 0 })

function rune  utf16_next_codepoint(Utf16CodepointIterator *it, Utf16String s, u8 *len);
function usize utf16_rune_count();
function u8    utf16_encoded_len(rune codepoint);
function u8    utf16_encode_codepoint(rune codepoint, u16 *s, ByteOrder bo);

// Used as follows: StringEachRune(Str("asdf"), ch) printf("%c", ch);
#define StringEachRune(s, ident) \
  String Glue(Glue(utf8str_, __LINE__), _) = (s); \
  Utf8CodepointIterator Glue(Glue(utf8cpi_, __LINE__), _) = UTF8_CODEPOINT_ITERATOR_INIT; \
  u8 Glue(Glue(utf8cpl_, __LINE__), _); \
  for (rune ident = utf8_next_codepoint(&Glue(Glue(utf8cpi_, __LINE__), _), Glue(Glue(utf8str_, __LINE__), _), &Glue(Glue(utf8cpl_, __LINE__), _)); \
    (ident != 0 || Glue(Glue(utf8cpl_, __LINE__), _) != 0) && ident != INVALID_RUNE; \
    ident = utf8_next_codepoint(&Glue(Glue(utf8cpi_, __LINE__), _), Glue(Glue(utf8str_, __LINE__), _), &Glue(Glue(utf8cpl_, __LINE__), _)))

#define Utf16StringEachRune(s, ident) \
  Utf16String Glue(Glue(utf16str_, __LINE__), _) = (s); \
  Utf16CodepointIterator Glue(Glue(utf16cpi_, __LINE__), _) = UTF16_CODEPOINT_ITERATOR_INIT; \
  u8 Glue(Glue(utf16cpl_, __LINE__), _); \
  for (rune ident = utf16_next_codepoint(&Glue(Glue(utf16cpi_, __LINE__), _), Glue(Glue(utf16str_, __LINE__), _), &Glue(Glue(utf16cpl_, __LINE__), _)); \
    (ident != 0 || Glue(Glue(utf16cpl_, __LINE__), _) != 0) && ident != INVALID_RUNE; \
    ident = utf16_next_codepoint(&Glue(Glue(utf16cpi_, __LINE__), _), Glue(Glue(utf16str_, __LINE__), _), &Glue(Glue(utf16cpl_, __LINE__), _)))

//------------- Intrinsics --------------

#if defined(__has_include)
# define CanInclude(header) __has_include(header)
#else
# define CanInclude(...) 0
#endif

#if (IsArch(ARCH_X86) || IsArch(ARCH_X86_64)) && CanInclude(<immintrin.h>)
# define INTEL_INTRINSICS_AVAILABLE 1
#endif

#if IsCompiler(COMPILER_GCC) || IsCompiler(COMPILER_CLANG)
# define Likely(x)   __builtin_expect((x), 1)
# define Unlikely(x) __builtin_expect((x), 0)
#else
// TODO(rutgerbrf): find out if other compilers support these kind of things too
# define Likely(x)   x
# define Unlikely(x) x
#endif

//------------- Synchronization primitives -------------

#if OsHasFlags(OS_FLAGS_POSIX)
# include <pthread.h>
#endif

typedef struct {
#if OsHasFlags(OS_FLAGS_POSIX)
  pthread_mutex_t inner; 
#else
# error "No mutex support for this OS"
#endif
} Mutex;

typedef struct {
  Mutex *m;
} MutexGuard;

#define MUTEX_AUTO_UNLOCK __attribute__((__cleanup__(mutex_guard_unlock)))
#define MutexLockScoped(mutp) MutexGuard Glue(Glue(mlsguard_, __LINE__), _) MUTEX_AUTO_UNLOCK = mutex_lock(mutp)

function MutexGuard mutex_lock(Mutex *m);

// TODO(rutgerbrf): check m->m, afterwards do: m->m = NULL
function void mutex_guard_unlock(MutexGuard *m);

//--------------- I/O Base ---------------

typedef ssize Io_RwFunc(void *ctx, u8 *dest, usize n);
typedef s32   Io_CloseFunc(void *ctx);

typedef struct {
  Io_RwFunc *read;
  void      *ctx;
} Io_Reader;

typedef struct {
  Io_RwFunc *write;
  void      *ctx;
} Io_Writer;

typedef struct {
  Io_CloseFunc *close;
  void         *ctx;
} Io_Closer;

function ssize io_read(Io_Reader *r, u8 *dest, usize n);
function ssize io_write(Io_Writer *w, u8 *dest, usize n);
function s32   io_close(Io_Closer *c);

function s32 io_read_all(Io_Reader *r, u8 *dest, usize n);

//------------- Files -------------

#if OsHasFlags(OS_FLAGS_UNIX)
# include <fcntl.h>
# include <sys/types.h>
# include <sys/stat.h>
#endif

typedef struct {
  Mem_Base *mb;
#if OsHasFlags(OS_FLAGS_UNIX)
  Mutex fd_lock;
  int fd; // -1 means invalid
#endif
} File;

function s32   file_open(Mem_Base *mb, String path, File **f);
function s32   file_create(Mem_Base *mb, String path, File **f);
function s32   file_close(File *f);
function s32   file_get_size(File *f, usize *size);
function s32   file_cleanup_close(File **f);
function ssize file_read(File *f, u8 *dest, usize n);

function Io_Reader file_reader(File *f);
function Io_Writer file_writer(File *f);
function Io_Closer file_closer(File *f);

#define FILE_AUTO_CLOSE __attribute__((__cleanup__(file_cleanup_close)))

//------------- Debugging -------------

#if !defined(AssertBreak)
# define AssertBreak() (*(int*)0 = 0)
#endif

#if ENABLE_ASSERT
# define Assert(c) Stmt(if (!(c)) { fputs("Assertion " Stringify(c) " failed\n", stderr); AssertBreak(); })
#else
# define Assert(c)
#endif

#if ENABLE_UNREACHABLE
function void *unreachable(String loc, String reason);
# define Unreachable(reason) unreachable(Str(__FILE__ ":" Stringify(__LINE__)), Str(reason))
#else
# define Unreachable(reason)
#endif

