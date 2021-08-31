#include "base.h"
#include "base.c"
#include "rpc.h"
#include "rpc.c"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

struct Wes_MessageField;
struct Wes_ResponseField;
struct Wes_Message;
struct Wes_Response;
struct Wes_Rpc;
struct Wes_File;

#define XM_WES_PRIMITIVE_TYPES \
  X(Bool)   \
  X(String) \
  X(U64)    \
  X(U32)    \
  X(U16)    \
  X(U8)     \
  X(S64)    \
  X(S32)    \
  X(S16)    \
  X(S8)     \
  X(F64)    \
  X(F32)

#define XM_WES_STATUSES \
  X(Ok,                 ok,                   0) \
  X(Cancelled,          cancelled,            1) \
  X(Unknown,            unknown,              2) \
  X(InvalidArgument,    invalid_argument,     3) \
  X(DeadlineExceeded,   deadline_exceeded,    4) \
  X(NotFound,           not_found,            5) \
  X(AlreadyExists,      already_exists,       6) \
  X(PermissionDenied,   permission_denied,    7) \
  X(ResourceExhausted,  resource_exhausted,   8) \
  X(FailedPrecondition, failed_precondition,  9) \
  X(Aborted,            aborted,             10) \
  X(OutOfRange,         out_of_range,        11) \
  X(Unimplemented,      unimplemented,       12) \
  X(Internal,           internal,            13) \
  X(Unavailable,        unavailable,         14) \
  X(DataLoss,           data_loss,           15)

typedef enum Wes_Status {
#define X(pc_name, _, code) Glue(Wes_Status_, pc_name) = code,
  XM_WES_STATUSES
#undef X
} Wes_Status;

typedef enum Wes_PrimitiveType {
#define X(type) Glue(Wes_PrimitiveType_, type),
  XM_WES_PRIMITIVE_TYPES
#undef X
  Wes_PrimitiveType_COUNT,
} Wes_PrimitiveType;

typedef enum Wes_TypeKind {
  Wes_TypeKind_Message,
  Wes_TypeKind_Enumeration,
  Wes_TypeKind_Response,
  Wes_TypeKind_Primitive,
  Wes_TypeKind_Alias,
  Wes_TypeKind_COUNT,
} Wes_TypeKind;

typedef struct Wes_Type {
  String name;
  union {
    struct Wes_MessageField  *message;
    struct Wes_Enumeration   *enumeration;
    struct Wes_ResponseField *response;
    enum   Wes_PrimitiveType  primitive;
    struct Wes_Type          *alias;
  } value;
  Wes_TypeKind kind;
  struct Wes_Type *next;
} Wes_Type;

global const Wes_Type primitive_types[Wes_PrimitiveType_COUNT] = {
#define X(type) [Glue(Wes_PrimitiveType_, type)] = { .name = Str(Stringify(type)), .value.primitive = Glue(Wes_PrimitiveType_, type), .kind = Wes_TypeKind_Primitive, .next = NULL },
  XM_WES_PRIMITIVE_TYPES
#undef X
};

typedef struct Wes_MessageField {
  String    name;
  u64       index;
  const  Wes_Type         *type;
  struct Wes_MessageField *next;
} Wes_MessageField;

typedef struct Wes_Message {
  Wes_MessageField *fields;
  usize n_fields;
} Wes_Message;

typedef struct Wes_ResponseField {
  // type.kind == Response -> index should be 'set', status can be ignored
  // otherwise -> status should be 'set', index can be ignored
  Wes_Status  status;
  u64         index;
  const  Wes_Type          *type;
  struct Wes_ResponseField *next;
} Wes_ResponseField;

typedef struct Wes_Rpc {
  String    name;
  u64       ident;
  const  Wes_Type *input_type;
  const  Wes_Type *output_type;
  struct Wes_Rpc  *next;
} Wes_Rpc;

typedef struct Wes_File {
  Wes_Message   *messages;
  Wes_Rpc       *rpcs;
} Wes_File;

typedef enum {
  LexState_Start,
  LexState_Ident,
  LexState_Number,
  LexState_COUNT,
} LexState;

typedef struct {
  Mem_Base *mb;
  String    file_contents;
  Wes_Type *types;
  Wes_Rpc  *rpcs;
  usize     i;
} CompileState;

function void
wes_message_push_field(CompileState *cs, Wes_Type *t, Wes_MessageField f) {
  if (t->kind != Wes_TypeKind_Message) return;
  Wes_MessageField *heapf = mem_reserve(cs->mb, sizeof(Wes_MessageField));
  *heapf = f;
  heapf->next = t->value.message;
  t->value.message = heapf;
}

function void
wes_response_push_field(CompileState *cs, Wes_Type *t, Wes_ResponseField f) {
  if (t->kind != Wes_TypeKind_Response) return;
  Wes_ResponseField *heapf = mem_reserve(cs->mb, sizeof(Wes_ResponseField));
  *heapf = f;
  heapf->next = t->value.response;
  t->value.response = heapf;
}

function void
cs_push_type(CompileState *cs, Wes_Type t) {
  Wes_Type *heapt = mem_reserve(cs->mb, sizeof(Wes_Type));
  *heapt = t;
  heapt->next = cs->types;
  cs->types = heapt;
}

function void
cs_push_rpc(CompileState *cs, Wes_Rpc c) {
  Wes_Rpc *heapc = mem_reserve(cs->mb, sizeof(Wes_Rpc));
  *heapc = c;
  heapc->next = cs->rpcs;
  cs->rpcs = heapc;
}

function rune
cs_next(CompileState *cs, u8 *len) {
  String sliced = cs->file_contents;
  sliced.len -= cs->i;
  sliced.buf += cs->i;
  u8 codepoint_len = 0;
  rune r = utf8_next_codepoint(sliced, &codepoint_len);
  cs->i += codepoint_len;
  if (len != NULL) *len = codepoint_len;
  return r;
}

function rune
cs_peek(CompileState *cs, u8 *len) {
  u8 codepoint_len = 0;
  rune r = cs_next(cs, &codepoint_len);
  if (len != NULL) *len = codepoint_len;
  cs->i -= codepoint_len;
  return r;
}

function bool
cs_try_ch(CompileState *cs, rune ch) {
  u8 len;
  rune r = cs_peek(cs, &len);
  if (r == ch) cs->i += len;
  // TODO(rutgerbrf): save error
  return r == ch;
}

function void
cs_skip_whitespace(CompileState *cs) {
MainLoop:
  while (true) {
    u8 codepoint_len = 0;
    rune r = cs_next(cs, &codepoint_len);
    // bruh we'll just skip comments here too
    if (r == '/' && cs_peek(cs, NULL) == '/') {
      while (true) {
        r = cs_next(cs, NULL);
        if (r == '\n') {
          goto MainLoop;
        } else if (r == INVALID_RUNE) { // TODO(rutgerbrf): don't catch INVALID_RUNE (can just happen randomly in text), instead, only catch EOF
          return;
        }
      }
    }
    if (!isspace(r)) {
      cs->i -= codepoint_len;
      break;
    }
  }
}

function bool
cs_try_ident(CompileState *cs, String *dest) {
  cs_skip_whitespace(cs);
  usize total_len = 0;
  u8 codepoint_len = 0;
  while (true) {
    rune r = cs_next(cs, &codepoint_len);
    if ((total_len == 0 && !isalpha(r)) || !(isalnum(r) || r == '_')) break;
    total_len += codepoint_len;
  }
  cs->i -= codepoint_len;
  if (total_len == 0) return false;
  *dest = string_slice(cs->file_contents, cs->i - total_len, total_len);
  return true;
}

typedef enum {
  Keyword_Import   = 1 << 0,
  Keyword_Response = 1 << 1,
  Keyword_Message  = 1 << 2,
  Keyword_Rpc      = 1 << 3,
  Keyword_ANY      = -1,
} Keyword;

function bool
cs_try_keyword(CompileState *cs, Keyword accept, Keyword *dest) {
  String str;
  if (!cs_try_ident(cs, &str)) return false;

  if ((accept & Keyword_Import) && StringCmp(str, ==, Str("import"))) {
    *dest = Keyword_Import;
    return true;
  }
  if ((accept & Keyword_Response) && StringCmp(str, ==, Str("response"))) {
    *dest = Keyword_Response;
    return true;
  }
  if ((accept & Keyword_Message) && StringCmp(str, ==, Str("message"))) {
    *dest = Keyword_Message;
    return true;
  }
  if ((accept & Keyword_Rpc) && StringCmp(str, ==, Str("rpc"))) {
    *dest = Keyword_Rpc;
    return true;
  }

  cs->i -= str.len;
  return false;
}

function bool
cs_resolve_type(CompileState *cs, String type_name, const Wes_Type **dest) {
#define X(name) Stmt(if (StringCmp(type_name, ==, Str(Stringify(name)))) { *dest = &primitive_types[Glue(Wes_PrimitiveType_, name)]; return true; });
  XM_WES_PRIMITIVE_TYPES
#undef X

  for (Wes_Type *t = cs->types; t != NULL; t = t->next) {
    if (StringCmp(t->name, ==, type_name)) {
      *dest = t;
      return true;
    }
  }
  return false;
}

#define PUNCT "~!@#$%^&*()_+{}[]:;\"'<,>.?/`"

function bool
is_punct(rune r) {
  for (usize i = 0; i < ArrayCount(PUNCT); i++) {
    rune p = PUNCT[i];
    if (r == p) return true;
  }
  return false;
}

function bool
cs_u64_dec_lit(CompileState *cs, u64 *dest) {
  *dest = 0;
  while (true) {
    u8 r_len;
    rune r = cs_next(cs, &r_len);
    if (r < '0' || r > '9') {
      if (r == '_') continue;
      if (!is_punct(r) && !isspace(r)) {
        cs->i -= r_len;
        // TODO(rutgerbrf): save an error
        return false;
      }
      cs->i -= r_len;
      return true;
    }

    *dest = (*dest * 10) + (r - '0');
  }
}

function bool
cs_u64_oct_lit(CompileState *cs, u64 *dest) {
  *dest = 0;
  while (true) {
    u8 r_len;
    rune r = cs_next(cs, &r_len);
    if (r < '0' || r > '7') {
      if (r == '_') continue;
      if (!is_punct(r) && !isspace(r)) {
        cs->i -= r_len;
        // TODO(rutgerbrf): save an error
        return false;
      }
      cs->i -= r_len;
      return true;
    }

    *dest = (*dest << 3) | (r - '0');
  }
}

function bool
cs_u64_hex_lit(CompileState *cs, u64 *dest) {
  *dest = 0;
  while (true) {
    u8 r_len;
    rune r = cs_next(cs, &r_len);
    if ((r < '0' || r > '9') && (r < 'A' || r > 'F')) {
      if (r == '_') continue;
      if (!is_punct(r) && !isspace(r)) {
        cs->i -= r_len;
        // TODO(rutgerbrf): save an error
        return false;
      }
      cs->i -= r_len;
      return true;
    }

    u8 r_val;
    if (r >= '0' && r <= '9') r_val = r - '0';
    else if (r >= 'A' && r <= 'F') r_val = r - 'A';
    else return false; // TODO(rutgerbrf): save an error
    *dest = (*dest << 4) | r_val;
  }
}

function bool
cs_u64_bin_lit(CompileState *cs, u64 *dest) {
  *dest = 0;
  while (true) {
    u8 r_len;
    rune r = cs_next(cs, &r_len);
    if (r < '0' || r > '1') {
      if (r == '_') continue;
      if (!is_punct(r) && !isspace(r)) {
        cs->i -= r_len;
        // TODO(rutgerbrf): save an error
        return false;
      }
      cs->i -= r_len;
      return true;
    }
    *dest = (*dest << 1) | (r - '0');
  }
}

function bool
cs_u64_lit(CompileState *cs, u64 *dest) {
  u8 r_len;
  rune r = cs_peek(cs, &r_len);
  if (r == '0') {
    cs->i += r_len;
    rune type = cs_next(cs, &r_len);

    switch (type) {
    case 'b':
      return cs_u64_bin_lit(cs, dest);
    case 'o':
      return cs_u64_oct_lit(cs, dest);
    case 'x':
      return cs_u64_hex_lit(cs, dest);
    default:
      cs->i -= r_len;
      if (is_punct(type) || isspace(type)) { // it's just a zero
        *dest = 0;
        return true;
      }
      return false;
    }
  }
  return cs_u64_dec_lit(cs, dest);
}

function bool
cs_msg_field(CompileState *cs, Wes_MessageField *f) {
  String type_name;
  if (!cs_try_ident(cs, &type_name)) return false;
  if (!cs_resolve_type(cs, type_name, &f->type)) return false;
  if (!cs_try_ident(cs, &f->name)) return false;
  cs_skip_whitespace(cs);
  if (!cs_try_ch(cs, '@')) return false;
  if (!cs_u64_lit(cs, &f->index)) return false;
  cs_skip_whitespace(cs);
  if (!cs_try_ch(cs, ';')) return false;
  return true;
}

function bool
cs_msg(CompileState *cs) {
  Wes_Type type = {
    .value = { .message = NULL },
    .kind = Wes_TypeKind_Message,
    .next = NULL,
  };

  if (!cs_try_ident(cs, &type.name)) return false;
  cs_skip_whitespace(cs);
  if (!cs_try_ch(cs, '{')) return false;
  printf("Message name: %.*s\n", type.name.len, type.name.buf);

  while (true) {
    cs_skip_whitespace(cs);
    if (cs_try_ch(cs, '}')) break;

    Wes_MessageField field;
    if (!cs_msg_field(cs, &field)) return false;

    puts("Read a message field");
    wes_message_push_field(cs, &type, field);
  }

  cs_push_type(cs, type);

  return true;
}

function bool
cs_import(CompileState *cs) {
  return false;
}

function bool
cs_rsp_field(CompileState *cs, Wes_ResponseField *f) {
  String type_name;
  String status;
  if (!cs_try_ident(cs, &type_name)) return false;
  if (!cs_resolve_type(cs, type_name, &f->type)) return false;
  if (cs_try_ident(cs, &status))  {
#define X(pc_name, sc_name, _) if (StringCmp(status, ==, Str(Stringify(sc_name)))) { f->status = Glue(Wes_Status_, pc_name); } else
  XM_WES_STATUSES
#undef X
    /* else */ return false;
  } else {
    if (!cs_try_ch(cs, '@')) return false;
    if (!cs_u64_lit(cs, &f->index)) return false;
    cs_skip_whitespace(cs);
  }
  cs_skip_whitespace(cs);
  if (!cs_try_ch(cs, ';')) return false;
  return true;
}

function bool
cs_rsp(CompileState *cs) {
  Wes_Type type = {
    .value = { .response = NULL },
    .kind = Wes_TypeKind_Response,
    .next = NULL,
  };

  if (!cs_try_ident(cs, &type.name)) return false;
  cs_skip_whitespace(cs);
  if (!cs_try_ch(cs, '{')) return false;
  printf("Response name: %.*s\n", type.name.len, type.name.buf);

  while (true) {
    cs_skip_whitespace(cs);
    if (cs_try_ch(cs, '}')) break;

    Wes_ResponseField field;
    if (!cs_rsp_field(cs, &field)) return false;

    puts("Read a response field");
    wes_response_push_field(cs, &type, field);
  }

  cs_push_type(cs, type);

  return true;
}

function bool
cs_rpc(CompileState *cs) {
  Wes_Rpc rpc;

  String output_type;
  if (!cs_try_ident(cs, &output_type)) return false;
  if (!cs_resolve_type(cs, output_type, &rpc.output_type)) return false;
  if (!cs_try_ident(cs, &rpc.name)) return false;
  cs_skip_whitespace(cs);
  if (!cs_try_ch(cs, '(')) return false;
  String input_type;
  if (!cs_try_ident(cs, &input_type)) return false;
  if (!cs_resolve_type(cs, input_type, &rpc.input_type)) return false;
  cs_skip_whitespace(cs);
  if (!cs_try_ch(cs, ')')) return false;
  cs_skip_whitespace(cs);
  if (!cs_try_ch(cs, '@')) return false;
  if (!cs_u64_lit(cs, &rpc.ident)) return false;

  printf("Read an RPC: %.*s (%.*s -> %.*s)\n", rpc.name.len, rpc.name.buf, rpc.input_type->name.len, rpc.input_type->name.buf, rpc.output_type->name.len, rpc.output_type->name.buf);

  cs_push_rpc(cs, rpc);

  return true;
}

function void
wes_destroy_message_fields(CompileState *cs, Wes_MessageField *fields) {
  Wes_MessageField *field = fields;
  while (field) {
    Wes_MessageField *next = field->next;
    mem_release(cs->mb, field, sizeof(Wes_ResponseField));
    field = next;
  }
}

function void
wes_destroy_response_fields(CompileState *cs, Wes_ResponseField *fields) {
  Wes_ResponseField *field = fields;
  while (field) {
    Wes_ResponseField *next = field->next;
    mem_release(cs->mb, field, sizeof(Wes_ResponseField));
    field = next;
  }
}

function void
wes_type_destroy(CompileState *cs, Wes_Type *t) {
  switch (t->kind) {
  case Wes_TypeKind_Message:
    wes_destroy_message_fields(cs, t->value.message);
    break;
  case Wes_TypeKind_Response:
    wes_destroy_response_fields(cs, t->value.response);
    break;
  }
}

function void
wes_rpc_destroy(CompileState *cs, Wes_Rpc *c) {}

function void
cs_destroy(CompileState *cs) {
  Wes_Type *t = cs->types;
  while (t) {
    Wes_Type *next = t->next;
    wes_type_destroy(cs, t);
    mem_release(cs->mb, t, sizeof(Wes_Type));
    t = next;
  }

  Wes_Rpc *c = cs->rpcs;
  while (c) {
    Wes_Rpc *next = c->next;
    wes_rpc_destroy(cs, c);
    mem_release(cs->mb, c, sizeof(Wes_Rpc));
    c = next;
  }
}

function s32
compile_source(Mem_Base *mb, String filename, String contents) {
  printf("Compiling source file %.*s\n", filename.len, filename.buf);

  CompileState cs = {
    .file_contents = contents,
    .i  = 0,
    .mb = mb,
  };

  while (true) {
    Keyword kw;
    if (cs_try_keyword(&cs, Keyword_Import|Keyword_Response|Keyword_Message|Keyword_Rpc, &kw)) {
      switch (kw) {
      case Keyword_Import:
        cs_import(&cs);
        break;
      case Keyword_Response:
        cs_rsp(&cs);
        break;
      case Keyword_Message:
        cs_msg(&cs);
        break;
      case Keyword_Rpc:
        cs_rpc(&cs);
        break;
      default:
        goto Cleanup;
      }
    } else break;
  }
Cleanup:
  cs_destroy(&cs);
  return 0;
}

function s32
process_file(Mem_Base *mb, String filename) {
  File *f FILE_AUTO_CLOSE = NULL;
  s32 ret = file_open(mb, filename, &f);
  if (ret != 0) return ret;
  Io_Reader r = file_reader(f);

  usize buf_size;
  ret = file_get_size(f, &buf_size);
  if (ret != 0) return ret;

  MemReserveAutoRelease(mb, u8 *, buf, buf_size);
  if (!buf) return ENOMEM;
  ret = io_read_all(&r, buf, buf_size);
  if (ret != 0) return ret;

  ret = compile_source(mb, filename, string_from_raw(buf, buf_size));
  return ret;
}

s32
main(s32 argc, char *argv[]) {
  Mem_Base *mb = mem_malloc_base();

  if (argc == 1) {
    fputs("Error: no files to compile provided\n", stderr);
    return 1;
  }

  for (s32 i = 1; i < argc; i++) {
    String filename = string_from_st(mb, argv[i], 0);
    s32 ret = process_file(mb, filename);
    if (ret != 0) perror("process_file");
    string_destroy(mb, filename);
  }

  return 0;
}
