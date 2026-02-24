#include "convert_core/convert_core.h"

static int streq(const char* a, const char* b) {
  usize i = 0;

  if (a == 0 || b == 0) {
    return 0;
  }

  while (a[i] != '\0' && b[i] != '\0') {
    if (a[i] != b[i]) {
      return 0;
    }
    i++;
  }

  return a[i] == '\0' && b[i] == '\0';
}

static int req_is_valid(const convert_request* req) {
  if (req == 0) {
    return 0;
  }
  if (req->from_format == 0 || req->to_format == 0) {
    return 0;
  }
  if (req->input == 0 && req->input_len != 0) {
    return 0;
  }
  if (req->output == 0 || req->output_len == 0) {
    return 0;
  }
  if (req->policy != CONVERT_POLICY_STRICT && req->policy != CONVERT_POLICY_LOSSY &&
      req->policy != CONVERT_POLICY_ROUNDTRIP_SAFE) {
    return 0;
  }
  return 1;
}

int convert_diagnostics_push(convert_diagnostics* diags,
                             convert_diag_severity severity,
                             convert_diag_code code,
                             convert_stage stage,
                             const char* message) {
  if (diags == 0) {
    return CONVERT_ERR_INVALID;
  }

  if (diags->count >= CONVERT_MAX_DIAGNOSTICS) {
    diags->dropped_count++;
    return CONVERT_ERR_DIAG_LIMIT;
  }

  diags->items[diags->count].severity = severity;
  diags->items[diags->count].code = code;
  diags->items[diags->count].stage = stage;
  diags->items[diags->count].message = message;
  diags->count++;
  return CONVERT_OK;
}

int convert_diagnostics_has_error(const convert_diagnostics* diags) {
  usize i;
  if (diags == 0) {
    return 1;
  }
  for (i = 0; i < diags->count; i++) {
    if (diags->items[i].severity == CONVERT_DIAG_ERROR) {
      return 1;
    }
  }
  return 0;
}

void convert_registry_init(convert_registry* registry) {
  usize i;
  if (registry == 0) {
    return;
  }
  registry->count = 0;
  for (i = 0; i < CONVERT_MAX_HANDLERS; i++) {
    registry->handlers[i] = 0;
  }
}

int convert_registry_register(convert_registry* registry,
                              const convert_format_handler* handler) {
  usize i;

  if (registry == 0 || handler == 0 || handler->name == 0 || handler->name[0] == '\0') {
    return CONVERT_ERR_INVALID;
  }

  for (i = 0; i < registry->count; i++) {
    if (streq(registry->handlers[i]->name, handler->name)) {
      return CONVERT_ERR_INVALID;
    }
  }

  if (registry->count >= CONVERT_MAX_HANDLERS) {
    return CONVERT_ERR_LIMIT;
  }

  registry->handlers[registry->count] = handler;
  registry->count++;
  return CONVERT_OK;
}

const convert_format_handler* convert_registry_find(const convert_registry* registry,
                                                    const char* name) {
  usize i;

  if (registry == 0 || name == 0) {
    return 0;
  }

  for (i = 0; i < registry->count; i++) {
    const convert_format_handler* h = registry->handlers[i];
    if (h != 0 && streq(h->name, name)) {
      return h;
    }
  }

  return 0;
}

int convert_session_init(convert_session* session,
                         doc_model_document* doc,
                         convert_diagnostics* diags) {
  if (session == 0 || doc == 0 || diags == 0) {
    return CONVERT_ERR_INVALID;
  }

  session->doc.doc = doc;
  session->diags = diags;
  session->diags->count = 0;
  session->diags->dropped_count = 0;
  return CONVERT_OK;
}

int convert_core_run(const convert_request* req,
                     const convert_format_handler* importer,
                     const convert_format_handler* exporter,
                     convert_doc_buffer* doc,
                     convert_diagnostics* diags) {
  int rc;
  doc_model_validation_error model_err;

  if (diags != 0) {
    diags->count = 0;
    diags->dropped_count = 0;
  }

  if (!req_is_valid(req) || importer == 0 || exporter == 0 || doc == 0 || doc->doc == 0 ||
      importer->import_doc == 0 || exporter->export_doc == 0 || !streq(req->from_format, importer->name) ||
      !streq(req->to_format, exporter->name)) {
    (void)convert_diagnostics_push(diags,
                                   CONVERT_DIAG_ERROR,
                                   CONVERT_DIAG_BAD_REQUEST,
                                   CONVERT_STAGE_PARSE,
                                   "invalid conversion request");
    return CONVERT_ERR_INVALID;
  }

  rc = importer->import_doc(importer, req->input, req->input_len, req->policy, doc, diags);
  if (rc != CONVERT_OK) {
    (void)convert_diagnostics_push(diags,
                                   CONVERT_DIAG_ERROR,
                                   CONVERT_DIAG_HANDLER_FAILURE,
                                   CONVERT_STAGE_NORMALIZE_IN,
                                   "import handler failed");
    return rc;
  }

  model_err.code = DOC_MODEL_VALIDATION_NONE;
  model_err.field = 0;
  model_err.index = 0;
  rc = doc_model_validate_document(doc->doc, &model_err);
  if (rc != DOC_MODEL_OK) {
    (void)convert_diagnostics_push(diags,
                                   CONVERT_DIAG_ERROR,
                                   CONVERT_DIAG_VALIDATION_FAILURE,
                                   CONVERT_STAGE_TRANSFORM,
                                   "canonical document validation failed");
    return CONVERT_ERR_INVALID;
  }

  if (req->policy == CONVERT_POLICY_STRICT && convert_diagnostics_has_error(diags)) {
    return CONVERT_ERR_POLICY;
  }

  rc = exporter->export_doc(
      exporter, doc, req->policy, req->output, req->output_cap, req->output_len, diags);
  if (rc != CONVERT_OK) {
    (void)convert_diagnostics_push(diags,
                                   CONVERT_DIAG_ERROR,
                                   CONVERT_DIAG_HANDLER_FAILURE,
                                   CONVERT_STAGE_EMIT,
                                   "export handler failed");
    return rc;
  }

  if (req->policy == CONVERT_POLICY_STRICT && convert_diagnostics_has_error(diags)) {
    return CONVERT_ERR_POLICY;
  }

  return CONVERT_OK;
}

int convert_core_run_session(const convert_request* req,
                             const convert_format_handler* importer,
                             const convert_format_handler* exporter,
                             convert_session* session) {
  if (session == 0) {
    return CONVERT_ERR_INVALID;
  }
  return convert_core_run(req, importer, exporter, &session->doc, session->diags);
}

int convert_core_run_with_registry(const convert_request* req,
                                   const convert_registry* registry,
                                   convert_session* session) {
  const convert_format_handler* importer;
  const convert_format_handler* exporter;

  if (req == 0 || registry == 0 || session == 0) {
    return CONVERT_ERR_INVALID;
  }

  importer = convert_registry_find(registry, req->from_format);
  exporter = convert_registry_find(registry, req->to_format);
  if (importer == 0 || exporter == 0) {
    if (session->diags != 0) {
      (void)convert_diagnostics_push(session->diags,
                                     CONVERT_DIAG_ERROR,
                                     CONVERT_DIAG_BAD_REQUEST,
                                     CONVERT_STAGE_PARSE,
                                     "format handler not found");
    }
    return CONVERT_ERR_NOT_FOUND;
  }

  return convert_core_run_session(req, importer, exporter, session);
}
