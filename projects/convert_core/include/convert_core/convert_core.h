#ifndef CONVERT_CORE_CONVERT_CORE_H
#define CONVERT_CORE_CONVERT_CORE_H

#include "doc_model/doc_model.h"
#include "rt/types.h"

#define CONVERT_OK 0
#define CONVERT_ERR_INVALID (-1)
#define CONVERT_ERR_UNSUPPORTED (-2)
#define CONVERT_ERR_POLICY (-3)
#define CONVERT_ERR_DIAG_LIMIT (-4)
#define CONVERT_ERR_NOT_FOUND (-5)
#define CONVERT_ERR_LIMIT (-6)

#define CONVERT_MAX_DIAGNOSTICS 128
#define CONVERT_MAX_HANDLERS 16

typedef enum convert_policy_mode {
  CONVERT_POLICY_STRICT = 1,
  CONVERT_POLICY_LOSSY = 2,
  CONVERT_POLICY_ROUNDTRIP_SAFE = 3
} convert_policy_mode;

typedef enum convert_stage {
  CONVERT_STAGE_PARSE = 1,
  CONVERT_STAGE_NORMALIZE_IN = 2,
  CONVERT_STAGE_TRANSFORM = 3,
  CONVERT_STAGE_NORMALIZE_OUT = 4,
  CONVERT_STAGE_EMIT = 5
} convert_stage;

typedef enum convert_diag_severity {
  CONVERT_DIAG_INFO = 1,
  CONVERT_DIAG_WARN = 2,
  CONVERT_DIAG_ERROR = 3
} convert_diag_severity;

typedef enum convert_diag_code {
  CONVERT_DIAG_NONE = 0,
  CONVERT_DIAG_UNSUPPORTED_CONSTRUCT = 1,
  CONVERT_DIAG_LOSSY_DROP = 2,
  CONVERT_DIAG_VALIDATION_FAILURE = 3,
  CONVERT_DIAG_BAD_REQUEST = 4,
  CONVERT_DIAG_HANDLER_FAILURE = 5
} convert_diag_code;

typedef struct convert_diagnostic {
  convert_diag_severity severity;
  convert_diag_code code;
  convert_stage stage;
  const char* message;
} convert_diagnostic;

typedef struct convert_diagnostics {
  convert_diagnostic items[CONVERT_MAX_DIAGNOSTICS];
  usize count;
  usize dropped_count;
} convert_diagnostics;

typedef struct convert_doc_buffer {
  doc_model_document* doc;
} convert_doc_buffer;

typedef struct convert_request {
  const char* from_format;
  const char* to_format;
  const u8* input;
  usize input_len;
  u8* output;
  usize output_cap;
  usize* output_len;
  convert_policy_mode policy;
} convert_request;

typedef struct convert_format_handler convert_format_handler;

typedef int (*convert_import_fn)(const convert_format_handler* handler,
                                 const u8* input,
                                 usize input_len,
                                 convert_policy_mode policy,
                                 convert_doc_buffer* doc,
                                 convert_diagnostics* diags);

typedef int (*convert_export_fn)(const convert_format_handler* handler,
                                 const convert_doc_buffer* doc,
                                 convert_policy_mode policy,
                                 u8* output,
                                 usize output_cap,
                                 usize* output_len,
                                 convert_diagnostics* diags);

struct convert_format_handler {
  const char* name;
  convert_import_fn import_doc;
  convert_export_fn export_doc;
  void* user;
};

typedef struct convert_registry {
  const convert_format_handler* handlers[CONVERT_MAX_HANDLERS];
  usize count;
} convert_registry;

typedef struct convert_session {
  convert_doc_buffer doc;
  convert_diagnostics* diags;
} convert_session;

int convert_diagnostics_push(convert_diagnostics* diags,
                             convert_diag_severity severity,
                             convert_diag_code code,
                             convert_stage stage,
                             const char* message);
int convert_diagnostics_has_error(const convert_diagnostics* diags);

void convert_registry_init(convert_registry* registry);
int convert_registry_register(convert_registry* registry,
                              const convert_format_handler* handler);
const convert_format_handler* convert_registry_find(const convert_registry* registry,
                                                    const char* name);

int convert_session_init(convert_session* session,
                         doc_model_document* doc,
                         convert_diagnostics* diags);

int convert_core_run(const convert_request* req,
                     const convert_format_handler* importer,
                     const convert_format_handler* exporter,
                     convert_doc_buffer* doc,
                     convert_diagnostics* diags);

int convert_core_run_session(const convert_request* req,
                             const convert_format_handler* importer,
                             const convert_format_handler* exporter,
                             convert_session* session);

int convert_core_run_with_registry(const convert_request* req,
                                   const convert_registry* registry,
                                   convert_session* session);

#endif
