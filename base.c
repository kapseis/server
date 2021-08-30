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
  return _bswap64(x);
}

function u32
swap_byte_order_u32(u32 x) {
  return _bswap(x);
}

function u16
swap_byte_order_u16(u16 x) {
  return _rotwl(x, 8);
}
#endif

#if !NATIVE_SWAP_64
function u64
swap_byte_order_u64(u64 x) {
  return ((x >> 56) & 0x00000000000000ff)
       | ((x >> 40) & 0x000000000000ff00)
       | ((x >> 24) & 0x0000000000ff0000)
       | ((x >>  8) & 0x00000000ff000000)
       | ((x <<  8) & 0x000000ff00000000)
       | ((x << 24) & 0x0000ff0000000000)
       | ((x << 40) & 0x00ff000000000000)
       | ((x << 56) & 0xff00000000000000);
}
#endif

#if !NATIVE_SWAP_32
function u32
swap_byte_order_u32(u32 x) {
  return ((x >> 24) & 0x000000ff)
       | ((x >>  8) & 0x0000ff00)
       | ((x <<  8) & 0x00ff0000)
       | ((x << 24) & 0xff000000);
}
#endif

#if !NATIVE_SWAP_16
function u16
swap_byte_order_u16(u16 x) {
  return ((x >> 8) & 0x00ff) | ((x << 8) & 0xff00);
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

function void
mem_noop_mem_change(void *ctx, void *p, usize size) {}

function void *
mem_malloc_reserve(void *ctx_, usize size) {
  return malloc(size);
}

function void
mem_malloc_release(void *ctx_, void *p, usize size_) {
  free(p);
}

function Mem_Base *
mem_malloc_base() {
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
  char *ststr = malloc(s.len + 1);
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
  return end - str;
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
utf8_next_codepoint(Utf8CodepointIterator *it, String s, u8 *len) {
#define SetCodepointLen(n) Stmt(if (len) *len = n)
#define AssertRemainingCapacity(n) Stmt(if (Unlikely(s.len - it->i < (n))) return INVALID_RUNE)

  SetCodepointLen(0);
  if (Unlikely(it->i == s.len)) {
    // This way the caller can still distinguish NUL characters from EOFs,
    // because for those, *len would be set to 1.
    return 0;
  }

  u8 start = s.buf[it->i++];
  if ((start & 0x80) != 0x80) { // start & 0b1000_0000 != 0b1000_0000
    // An ASCII codepoint.
    SetCodepointLen(1);
    return (rune)start;
  } else if ((start & 0xe0) == 0xc0) { // start & 0b1110_0000 == 0b1100_0000
    // One byte follows
    AssertRemainingCapacity(1);
    u8 second = s.buf[it->i++];
    u8 second_data = second & 0x7f;
    if (Unlikely(second_data == second)) return INVALID_RUNE;
    SetCodepointLen(2);
    // ((start & 0b0001_1111) << 6) | (second & 0b0011_1111)
    return ((u32)(start & 0x1f) << 6) + (u32)(second & 0x3f);
  } else if ((start & 0xf0) == 0xe0) { // start & 0b1111_0000 == 0b1110_0000
    // Two bytes follow
    AssertRemainingCapacity(2);
    u8 second = s.buf[it->i++];
    u8 second_data = second & 0x7f;
    if (Unlikely(second_data == second)) return INVALID_RUNE;
    u8 third = s.buf[it->i++];
    u8 third_data = third & 0x7f;
    if (Unlikely(third_data == third)) return INVALID_RUNE;
    //   ((start  & 0b0000_1111) << 12)
    // + ((second & 0b0011_1111) <<  6)
    // +  (third  & 0b0011_1111)
    SetCodepointLen(3);   
    return ((u32)(start  & 0x0f) << 12)
         + ((u32)(second & 0x3f) <<  6)
         +  (u32)(third  & 0x3f);
  } else if ((start & 0xf8) == 0xf0) { // start & 0b1111_1000 == 0b1111_0000
    // Three bytes follow
    AssertRemainingCapacity(3);
    u8 second = s.buf[it->i++];
    u8 second_data = second & 0x7f;
    if (Unlikely(second_data == second)) return INVALID_RUNE;
    u8 third = s.buf[it->i++];
    u8 third_data = third & 0x7f;
    if (Unlikely(third_data == third)) return INVALID_RUNE;
    u8 fourth = s.buf[it->i++];
    u8 fourth_data = fourth & 0x7f;
    if (Unlikely(fourth_data == fourth)) return INVALID_RUNE;
    //   ((start  & 0b0000_0111) << 18)
    // + ((second & 0b0011_1111) << 12)
    // + ((third  & 0b0011_1111) <<  6)
    // +  (fourth & 0b0011_1111)
    SetCodepointLen(4);
    return ((u32)(start  & 0x07) << 18)
         + ((u32)(second & 0x3f) << 12)
         + ((u32)(third  & 0x3f) <<  6)
         +  (u32)(fourth & 0x3f);
  }

  return INVALID_RUNE;

#undef SetCodepointLen
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
utf16_next_codepoint(Utf16CodepointIterator *it, Utf16String s, u8 *len) {
#define SetCodepointLen(n) Stmt(if (len) *len = n)
  
  SetCodepointLen(0);
  if (Unlikely(it->i == s.len)) {
    // This way the caller can still distinguish NUL characters from EOFs,
    // because for those, *len would be set to 1.
    return 0;
  }

  u16 start = BoToSystem(s.bo, s.buf[it->i++]);
  if (start < 0xd800 || start > 0xdfff) {
    SetCodepointLen(1);
    return (rune)start;
  }

  // NOTE(rutgerbrf):
  //  With regard to https://en.wikipedia.org/wiki/UTF-16#U.2BD800_to_U.2BDFFF,
  //  we may not actually these checks to result in an error.
  if (Unlikely((start & 0xd800) != 0xd800)) return INVALID_RUNE;
  if (Unlikely(s.len - it->i == 0)) return INVALID_RUNE;

  u16 second = BoToSystem(s.bo, s.buf[it->i++]);
  if (Unlikely((second & 0xdc00) != 0xdc00)) return INVALID_RUNE;

  SetCodepointLen(2);
  return (u32)0x10000 + (((u32)(start - 0xd800) << 10) | ((u32)(second - 0xdc00)));

#undef SetCodepointLen
}

function u8
utf8_encode_codepoint(rune codepoint, u8 *s) {
  if (codepoint < 0x80) {
    *s = codepoint;
    return 1;
  }
  if (codepoint < 0x800) {
    s[0] = 0xc0 | (((u32)codepoint >> 6) & 0x1f);
    s[1] = 0x80 |  ((u32)codepoint       & 0x3f);
    return 2;
  }
  if (codepoint < 0x10000) {
    s[0] = 0xe0 | (((u32)codepoint >> 12) & 0x0f);
    s[1] = 0x80 | (((u32)codepoint >>  6) & 0x3f);
    s[2] = 0x80 |  ((u32)codepoint        & 0x3f);
    return 3;
  }
  // TODO(rutgerbrf): consider limiting the codepoint to 0x1ffff
  s[0] = 0xf0 | (((u32)codepoint >> 18) & 0x07);
  s[1] = 0x80 | (((u32)codepoint >> 12) & 0x3f);
  s[2] = 0x80 | (((u32)codepoint >>  6) & 0x3f);
  s[3] = 0x80 |  ((u32)codepoint        & 0x3f);
  return 4;
}

function u8
utf16_encode_codepoint(rune codepoint, u16 *s, ByteOrder bo) {
  if (Unlikely(codepoint >= 0xd800 && codepoint <= 0xdfff)) {
    // Technically invalid code points
    // See the note about these codepoints from earlier.
    // We may want to encode these code point for compatibility reasons anyways.
    return 0;
  }

  if (codepoint >= 0x10000) {
    codepoint -= 0x10000;
    s[0] /* high */ = SystemToBo(bo, 0xd800 + (u32)((codepoint >> 10) & 0x3ff));
    s[1] /* low  */ = SystemToBo(bo, 0xdc00 + (u32)( codepoint        & 0x3ff));
    return 2;
  }

  *s = SystemToBo(bo, (u16)codepoint);
  return 1;
}

function u8
utf16_encoded_len(rune codepoint) {
  if (codepoint >= 0xd800 && codepoint <= 0xdfff) return 0;
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
  // TODO(rutgerbrf): what if the user would prefer using mem_decommit?
  mem_release(mb, (void *)s.buf, s.len);
}

function void
utf16string_destroy(Mem_Base *mb, Utf16String s) {
  // TODO(rutgerbrf): what if the user would prefer using mem_decommit?
  mem_release(mb, s.buf, s.len*sizeof(u16));
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
#if OsHasFlags(Os_Unix)
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
#if OsHasFlags(OsFlags_Unix)
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
#if OsHasFlags(OsFlags_Unix)
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
  *size = statbuf.st_size;
  return 0;
#else
# error "file_get_size is not implemented for this OS"
#endif
}

function MutexGuard
mutex_lock(Mutex *m) {
#if OsHasFlags(OsFlags_Posix)
  pthread_mutex_lock(&m->inner);
  return (MutexGuard){ .m = m };
#else
# error "No mutex_lock support for this OS"
#endif
}

function void
mutex_unlock_(Mutex *m) {
#if OsHasFlags(OsFlags_Posix)
  pthread_mutex_unlock(&m->inner);
#else
# error "No mutex_unlock_ support for this OS"
#endif
}

function void
mutex_guard_unlock(MutexGuard *g) {
#if IsCompiler(Compiler_Gcc) || IsCompiler(Compiler_Clang)
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

function s32
io_read_all(Io_Reader *r, u8 *dest, usize n) {
  ssize total_read = 0;
  ssize this_call_read = 0;
  while (total_read < n) {
    this_call_read = io_read(r, dest + total_read, n - total_read);
    if (this_call_read < 0) return errno;
    total_read += this_call_read;
  }
  return 0;
}

#if ENABLE_UNREACHABLE
function void *
unreachable(String loc, String reason) {
  fputs("Reached unreachable code", stderr);
  if (loc.len != 0) fprintf(stderr, " (%.*s)", reason.len, reason.buf);
  fprintf(stderr, ": %.*s", reason.len, reason.buf);
  AssertBreak();
  return NULL; // never reached
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

