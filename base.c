#include "base.h"

#if INTEL_INTRINSICS_AVAILABLE
# define NATIVE_SWAP_64 1
# define NATIVE_SWAP_32 1
# define NATIVE_SWAP_16 1
#else
# define NATIVE_SWAP_64 0
# define NATIVE_SWAP_32 0
# define NATIVE_SWAP_16 0
#endif

#if INTEL_INTRINSICS_AVAILABLE
# include <immintrin.h>
function u64
swap_byte_order_u64(u64 x) {
  return (u64)_bswap64((s64)x);
}

function u32
swap_byte_order_u32(u32 x) {
  return (u32)_bswap((s32)x);
}

function u16
swap_byte_order_u16(u16 x) {
  return _rotwl(x, 8);
}
#endif

#if !NATIVE_SWAP_64
function u64
swap_byte_order_u64(u64 x) {
  return ((x >> 56) & 0x00000000000000FF)
       | ((x >> 40) & 0x000000000000FF00)
       | ((x >> 24) & 0x0000000000FF0000)
       | ((x >>  8) & 0x00000000FF000000)
       | ((x <<  8) & 0x000000FF00000000)
       | ((x << 24) & 0x0000FF0000000000)
       | ((x << 40) & 0x00FF000000000000)
       | ((x << 56) & 0xFF00000000000000);
}
#endif

#if !NATIVE_SWAP_32
function u32
swap_byte_order_u32(u32 x) {
  return ((x >> 24) & 0x000000FF)
       | ((x >>  8) & 0x0000FF00)
       | ((x <<  8) & 0x00FF0000)
       | ((x << 24) & 0xFF000000);
}
#endif

#if !NATIVE_SWAP_16
function u16
swap_byte_order_u16(u16 x) {
  u16 high = (u16)(x >> 8) & (u16)0x00FF;
  u16 low  = (u16)(x << 8) & (u16)0xFF00;
  return high | low;
}
#endif

#undef NATIVE_SWAP_64
#undef NATIVE_SWAP_32
#undef NATIVE_SWAP_16

function void *
mem_reserve(Mem_Base *mb, usize size) {
  return mb->reserve(mb->ctx, size);
}

function void
mem_commit(Mem_Base *mb, void *p, usize size) {
  mb->commit(mb->ctx, p, size);
}

function void
mem_decommit(Mem_Base *mb, void *p, usize size) {
  mb->decommit(mb->ctx, p, size);
}

function void
mem_release(Mem_Base *mb, void *p, usize size) {
  mb->release(mb->ctx, p, size);
}

function void *
mem_reserve_commit(Mem_Base *mb, usize size) {
  void *p = mb->reserve(mb->ctx, size);
  mem_commit(mb, p, size);
  return p;
}

function void
mem_decommit_release(Mem_Base *mb, void *p, usize size) {
  mem_decommit(mb, p, size);
  mem_release(mb, p, size);
}

function void
mem_noop_mem_change(void *ctx_, void *p_, usize size_) {
  (void)ctx_; (void)p_; (void)size_;
}

function void *
mem_malloc_reserve(void *ctx_, usize size) {
  (void)ctx_;
  return malloc(size);
}

function void
mem_malloc_release(void *ctx_, void *p, usize size_) {
  (void)ctx_; (void)size_;
  free(p);
}

function Mem_Base *
mem_malloc_base(void) {
  local Mem_Base base = {0};
  if (base.reserve == NULL) { 
    base.reserve  = mem_malloc_reserve;
    base.commit   = mem_noop_mem_change;
    base.decommit = mem_noop_mem_change;
    base.release  = mem_malloc_release;
  }
  return &base;
}

function void
mem_auto_change(Mem_AutoChangeContext *ctx) {
  if (ctx->pp != NULL) ctx->call(ctx->mb, *ctx->pp, ctx->size);
}

function usize
ststring_length(const char *str, char sentinel) {
  const char *end;
  __asm__("movq %1, %%rdi\n\t"
          "movb %2, %%al\n\t"
          "movq $-1, %%rcx\n\t"
          "cld\n\t"
          "repne scasb\n\t"
          "movq %%rdi, %0"
          : "=r" (end)
          : "r" (str), "r" (sentinel)
          : "rcx", "rdi", "al");
  // end ends up being the memory location of the byte
  // after the last byte of str. We want the memory location
  // of the last byte of the string, so we decrement it by one.
  end--;
  Assert(*end == sentinel);
  return (usize)end - (usize)str;
}

function String
string_from_st(Mem_Base *mb, const char *str, char sentinel) {
  usize length = ststring_length(str, sentinel);
  u8    *bytes = mem_reserve(mb, length);
  memmove(bytes, str, length);
  return string_from_raw(bytes, length);
}

function String
string_from_c(Mem_Base *mb, const char *str) {
  return string_from_st(mb, str, 0);
}

function char *
string_to_st(Mem_Base *mb, String s, char sentinel) {
  char *ststr = mem_reserve(mb, s.len + 1);
  mem_commit(mb, ststr, s.len + 1);
  memmove(ststr, s.buf, s.len);
  ststr[s.len] = sentinel;
  return ststr;
}

function char *
string_to_c(Mem_Base *mb, String s) {
  return string_to_st(mb, s, 0);
}

function String
string_from_raw(const u8 *buf, usize len) {
  return (String){ .buf = buf, .len = len };
}

function usize
cstring_length(const char *str) {
  return ststring_length(str, 0);
}

function Utf16String
utf16string_from_raw(u16 *buf, usize len, ByteOrder bo) {
  return (Utf16String){
    .bo =  bo,
    .buf = buf,
    .len = len,
  };
}

// TODO(rutgerbrf): this code should probably be fuzzed!
// It should also be (heavily) optimized. There's a lot of potential for that.
// Golang is faster at processing UTF-8 than this implementation. We could be faster.
function rune
utf8_next_codepoint(String s, u8 *len) {
#define AssertMinLength(n) Stmt(if (Unlikely(s.len < n)) return INVALID_RUNE)

  *len = 0;
  if (Unlikely(s.len == 0)) {
    // This way the caller can still distinguish NUL characters from EOFs,
    // because for those, *len would be set to 1.
    return 0;
  }

  u8 start = s.buf[0];
  if ((start & 0x80) != 0x80) { // start & 0b1000_0000 != 0b1000_0000
    // An ASCII codepoint.
    *len = 1;
    return (rune)start;
  } else if ((start & 0xE0) == 0xC0) { // start & 0b1110_0000 == 0b1100_0000
    // One byte follows
    AssertMinLength(2);
    u8 second = s.buf[1];
    u8 second_data = second & 0x7F;
    if (Unlikely(second_data == second)) return INVALID_RUNE;
    *len = 2;
    // ((start & 0b0001_1111) << 6) | (second & 0b0011_1111)
    return (rune)((u32)(start & 0x1F) << 6) + (rune)(second & 0x3F);
  } else if ((start & 0xF0) == 0xE0) { // start & 0b1111_0000 == 0b1110_0000
    // Two bytes follow
    AssertMinLength(3);
    u8 second = s.buf[1];
    u8 second_data = second & 0x7F;
    if (Unlikely(second_data == second)) return INVALID_RUNE;
    u8 third = s.buf[2];
    u8 third_data = third & 0x7F;
    if (Unlikely(third_data == third)) return INVALID_RUNE;
    //   ((start  & 0b0000_1111) << 12)
    // + ((second & 0b0011_1111) <<  6)
    // +  (third  & 0b0011_1111)
    *len = 3;
    return (rune)((u32)(start  & 0x0F) << 12)
         + (rune)((u32)(second & 0x3F) <<  6)
         + (rune)      (third  & 0x3F);
  } else if ((start & 0xF8) == 0xF0) { // start & 0b1111_1000 == 0b1111_0000
    // Three bytes follow
    AssertMinLength(4);
    u8 second = s.buf[1];
    u8 second_data = second & 0x7F;
    if (Unlikely(second_data == second)) return INVALID_RUNE;
    u8 third = s.buf[2];
    u8 third_data = third & 0x7F;
    if (Unlikely(third_data == third)) return INVALID_RUNE;
    u8 fourth = s.buf[3];
    u8 fourth_data = fourth & 0x7F;
    if (Unlikely(fourth_data == fourth)) return INVALID_RUNE;
    //   ((start  & 0b0000_0111) << 18)
    // + ((second & 0b0011_1111) << 12)
    // + ((third  & 0b0011_1111) <<  6)
    // +  (fourth & 0b0011_1111)
    *len = 4;
    return (rune)((u32)(start  & 0x07) << 18)
         + (rune)((u32)(second & 0x3F) << 12)
         + (rune)((u32)(third  & 0x3F) <<  6)
         +       (rune)(fourth & 0x3F);
  }

  return INVALID_RUNE;

#undef AssertRemainingCapacity
}

function usize
utf8_rune_count(String s) {
  usize count = 0;
  StringEachRune(s, _) { count++; }
  return count;
}

function u8
utf8_encoded_len(rune codepoint) {
  if (codepoint < 0x80)    return 1;
  if (codepoint < 0x800)   return 2;
  if (codepoint < 0x10000) return 3;
  return 4;
}

function rune
utf16_next_codepoint(Utf16String s, u8 *len) {
  *len = 0;
  if (Unlikely(s.len == 0)) {
    // This way the caller can still distinguish NUL characters from EOFs,
    // because for those, *len would be set to 1.
    return 0;
  }

  u16 start = U16FromBo(s.buf[0], s.bo);
  if (start < 0xD800 || start > 0xDFFF) {
    *len = 1;
    return (rune)start;
  }

  // NOTE(rutgerbrf):
  //  With regard to https://en.wikipedia.org/wiki/UTF-16#U.2BD800_to_U.2BDFFF,
  //  we may not actually these checks to result in an error.
  if (Unlikely((start & 0xD800) != 0xD800)) return INVALID_RUNE;
  if (Unlikely(s.len < 2)) return INVALID_RUNE;

  u16 second = U16FromBo(s.buf[1], s.bo);
  if (Unlikely((second & 0xDC00) != 0xDC00)) return INVALID_RUNE;

  *len = 2;
  return (rune)0x10000 + (rune)(((u32)(start - 0xD800) << 10) | (u32)(second - 0xDC00));

#undef SetCodepointLen
}

function u8
utf8_encode_codepoint(rune codepoint, u8 *s) {
  if (codepoint < 0x80) {
    *s = (u8)codepoint;
    return 1;
  }
  if (codepoint < 0x800) {
    s[0] = (u8)(0xC0 | (((u32)codepoint >> 6) & 0x1F));
    s[1] = (u8)(0x80 |  ((u32)codepoint       & 0x3F));
    return 2;
  }
  if (codepoint < 0x10000) {
    s[0] = (u8)(0xE0 | (((u32)codepoint >> 12) & 0x0F));
    s[1] = (u8)(0x80 | (((u32)codepoint >>  6) & 0x3F));
    s[2] = (u8)(0x80 |  ((u32)codepoint        & 0x3F));
    return 3;
  }
  // TODO(rutgerbrf): consider limiting the codepoint to 0x1FFFF
  s[0] = (u8)(0xF0 | (((u32)codepoint >> 18) & 0x07));
  s[1] = (u8)(0x80 | (((u32)codepoint >> 12) & 0x3F));
  s[2] = (u8)(0x80 | (((u32)codepoint >>  6) & 0x3F));
  s[3] = (u8)(0x80 |  ((u32)codepoint        & 0x3F));
  return 4;
}

function u8
utf16_encode_codepoint(rune codepoint, u16 *s, ByteOrder bo) {
  if (Unlikely(codepoint >= 0xD800 && codepoint <= 0xDFFF)) {
    // Technically invalid code points
    // See the note about these codepoints from earlier.
    // We may want to encode these code point for compatibility reasons anyways.
    return 0;
  }

  if (codepoint >= 0x10000) {
    codepoint -= 0x10000;
    s[0] /* high */ = U16ToBo((u16)(0xD800 + (((u16)codepoint >> 10) & 0x3FF)), bo);
    s[1] /* low  */ = U16ToBo((u16)(0xDC00 +       (codepoint        & 0x3FF)), bo);
    return 2;
  }

  *s = U16FromBo((u16)codepoint, bo);
  return 1;
}

function u8
utf16_encoded_len(rune codepoint) {
  if (codepoint >= 0xD800 && codepoint <= 0xDFFF) return 0;
  if (codepoint >= 0x10000) return 2;
  return 1;
}

function usize
utf16_rune_count(Utf16String s) {
  usize count = 0;
  Utf16StringEachRune(s, _) { count++; }
  return count;
}

function Utf16String
utf8_to_utf16(Mem_Base *mb, String s, ByteOrder bo) {
  usize utf16_length = 0;
  StringEachRune(s, codepoint) {
    utf16_length += utf16_encoded_len(codepoint);
  }
  u16 *buf = mem_reserve(mb, utf16_length * sizeof(u16));
  usize i = 0;
  StringEachRune(s, codepoint) {
    i += utf16_encode_codepoint(codepoint, &buf[i], bo);
  }
  Assert(i == utf16_length);
  return utf16string_from_raw(buf, utf16_length, bo);
}

function String
utf16_to_utf8(Mem_Base *mb, Utf16String s) {
  usize utf8_length = 0;
  Utf16StringEachRune(s, codepoint) {
    utf8_length += utf8_encoded_len(codepoint);
  }
  u8 *buf = mem_reserve(mb, utf8_length);
  usize i = 0;
  Utf16StringEachRune(s, codepoint) {
    i += utf8_encode_codepoint(codepoint, &buf[i]);
  }
  Assert(i == utf8_length);
  return string_from_raw(buf, utf8_length);
}

function void
string_destroy(Mem_Base *mb, String s) {
  void *p = (union { void *p; const u8 *buf; }){ .buf = s.buf }.p;
  mem_decommit(mb, p, s.len);
  mem_release(mb, p, s.len);
}

function void
utf16string_destroy(Mem_Base *mb, Utf16String s) {
  void *p = (union { void *p; const u16 *buf; }){ .buf = s.buf }.p;
  mem_decommit(mb, (u8 *)p, s.len * sizeof(u16));
  mem_release(mb, (u8 *)p, s.len * sizeof(u16));
}

function String
string_slice(String s, usize start_at, usize len) {
  Assert((start_at + len) <= s.len);
  return string_from_raw(s.buf + start_at, len);
}

function s32
file_open(Mem_Base *mb, String path, File **f) {
  Assert(f != NULL);
  *f = mem_reserve(mb, sizeof(File));
  char *cpath = string_to_c(mb, path);

  s32 fd;
  while ((fd = open(cpath, O_RDONLY)) == -1) {
    switch (errno) {
    case EINTR: continue;
    default:    return errno;
    }
  }
  **f = (File){
    .mb = mb,
    .fd = fd,
  };

  mem_release(mb, cpath, path.len + 1);
  return 0;
}

function s32
file_create(Mem_Base *mb, String path, File **f) {
  *f = mem_reserve(mb, sizeof(File));
  char *cpath = string_to_c(mb, path);

  s32 fd;
  while ((fd = open(cpath, O_WRONLY|O_CREAT|O_TRUNC, 0666)) == -1) {
    switch (errno) {
    case EINTR: continue;
    default:    return errno;
    }
  }
  **f = (File){
    .mb = mb,
    .fd = fd,
  };

  mem_release(mb, cpath, path.len + 1);
  return 0;
}

function bool
file_is_valid_(File *f) {
  return f->fd != -1;
}

function ssize
file_read(File *f, u8 *dest, usize n) {
#if OsHasFlags(OS_FLAGS_UNIX)
  MutexLockScoped(&f->fd_lock);
  Assert(file_is_valid_(f));
  ssize ret;
  while ((ret = read(f->fd, dest, n)) == -1) {
    switch (errno) {
    case EINTR: continue;
    case EBADF: Unreachable("file_is_valid_ should have caught this");
    default:    return errno;
    }
  }
  return ret;
#else
# error "file_read is not implemented for this OS"
#endif
}

function s32
file_close(File *f) {
  if (f == NULL)
    return EBADF; 
#if OsHasFlags(OS_FLAGS_UNIX)
  MutexLockScoped(&f->fd_lock);
  Assert(file_is_valid_(f));
  while (close(f->fd) == -1) {
    switch (errno) {
    case EINTR: continue;
    case EBADF: Unreachable("file_is_valid should have caught this");
    default:    return errno;
    }
  }
  mem_release(f->mb, f, sizeof(File));
  return 0;
#else
# error "file_close is not implemented for this OS"
#endif
}

function s32
file_cleanup_close(File **f) {
  return file_close(*f);
}

function Io_Reader
file_reader(File *f) {
  return (Io_Reader){ .ctx = f, .read = (Io_RwFunc *)file_read };
}

function s32
file_get_size(File *f, usize *size) {
#if OsHasFlags(OS_FLAGS_UNIX)
  Assert(size != NULL);
  MutexLockScoped(&f->fd_lock);
  Assert(file_is_valid_(f));
  struct stat statbuf;
  while (fstat(f->fd, &statbuf) == -1) {
    switch (errno) {
    case EINTR: continue;
    case EBADF: Unreachable("file_is_valid should have caught this");
    default:    return errno;
    }
  }
  Assert(statbuf.st_size >= 0);
  *size = (usize)statbuf.st_size;
  return 0;
#else
# error "file_get_size is not implemented for this OS"
#endif
}

function MutexGuard
mutex_lock(Mutex *m) {
#if OsHasFlags(OS_FLAGS_POSIX)
  pthread_mutex_lock(&m->inner);
  return (MutexGuard){ .m = m };
#else
# error "No mutex_lock support for this OS"
#endif
}

function void
mutex_unlock_(Mutex *m) {
#if OsHasFlags(OS_FLAGS_POSIX)
  pthread_mutex_unlock(&m->inner);
#else
# error "No mutex_unlock_ support for this OS"
#endif
}

function void
mutex_guard_unlock(MutexGuard *g) {
#if IsCompiler(COMPILER_GCC) || IsCompiler(COMPILER_CLANG)
  Mutex *m = __atomic_exchange_n(/* ptr */ &g->m, /* val */ (Mutex *)NULL, /* memorder */ __ATOMIC_SEQ_CST);
  if (m == NULL) Unreachable("Double unlock or invalid mutex guard");
  mutex_unlock_(m);
#else
# error "No mutex_guard_unlock support for this compiler"
#endif
}

function ssize
io_read(Io_Reader *r, u8 *dest, usize n) {
  return r->read(r->ctx, dest, n);
}

function ssize
io_write(Io_Writer *w, u8 *dest, usize n) {
  return w->write(w->ctx, dest, n);
}

function s32
io_close(Io_Closer *c) {
  return c->close(c->ctx);
}

function s32
io_read_all(Io_Reader *r, u8 *dest, usize n) {
  while (n > 0) {
    ssize read = io_read(r, dest, n);
    if (read < 0) return errno;
    Assert((usize)read <= n);
    n    -= (usize)read;
    dest += (usize)read;
  }
  return 0;
}

#if ENABLE_UNREACHABLE
# if IsCompiler(COMPILER_GCC) || IsCompiler(COMPILER_CLANG)
__attribute__((noreturn))
# endif
function void *
unreachable(String loc, String reason) {
  fputs("Reached unreachable code", stderr);
  s32 reason_len = (s32)ClampTop(reason.len, S32_MAX);
  if (loc.len != 0) fprintf(stderr, " (%.*s)", reason_len, reason.buf);
  fprintf(stderr, ": %.*s", reason_len, reason.buf);
  AssertBreak();
# if IsCompiler(COMPILER_GCC) || IsCompiler(COMPILER_CLANG)
  __builtin_unreachable();
# else
  return NULL; // never reached
# endif
}
#endif

function s8
string_cmp(String a, String b) {
  if (a.len < b.len) return -1;
  if (a.len > b.len) return  1;
  usize i = 0;
  while (i < a.len) {
    if (a.buf[i] < b.buf[i]) return -1;
    if (a.buf[i] > b.buf[i]) return  1;
    i++;
  }
  return 0;
}
