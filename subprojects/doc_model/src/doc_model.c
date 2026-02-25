#include "doc_model/doc_model.h"

static void set_err(doc_model_validation_error* err,
                    doc_model_validation_code code,
                    const char* field,
                    usize index) {
  if (err == 0) {
    return;
  }
  err->code = code;
  err->field = field;
  err->index = index;
}

static int sv_is_non_empty(doc_model_sv sv) {
  return sv.data != 0 && sv.len > 0;
}

static int validate_inline_list(const doc_model_inline* inlines,
                                usize count,
                                usize depth,
                                doc_model_validation_error* err);

static int validate_block_list(const doc_model_block* blocks,
                               usize count,
                               usize depth,
                               doc_model_validation_error* err);

static int validate_inline(const doc_model_inline* inl,
                           usize index,
                           usize depth,
                           doc_model_validation_error* err) {
  if (inl == 0) {
    set_err(err, DOC_MODEL_VALIDATION_NULL_POINTER, "inline", index);
    return DOC_MODEL_ERR_INVALID;
  }

  if (depth > DOC_MODEL_MAX_VALIDATE_DEPTH) {
    set_err(err, DOC_MODEL_VALIDATION_DEPTH_LIMIT, "inline.depth", index);
    return DOC_MODEL_ERR_LIMIT;
  }

  switch (inl->kind) {
    case DOC_MODEL_INLINE_TEXT:
      if (inl->as.text.text.data == 0 && inl->as.text.text.len != 0) {
        set_err(err, DOC_MODEL_VALIDATION_NULL_POINTER, "inline.text", index);
        return DOC_MODEL_ERR_INVALID;
      }
      return DOC_MODEL_OK;

    case DOC_MODEL_INLINE_EMPHASIS:
      return validate_inline_list(inl->as.emphasis.children.items,
                                  inl->as.emphasis.children.count,
                                  depth + 1,
                                  err);

    case DOC_MODEL_INLINE_STRONG:
      return validate_inline_list(inl->as.strong.children.items,
                                  inl->as.strong.children.count,
                                  depth + 1,
                                  err);

    case DOC_MODEL_INLINE_CODE_SPAN:
      if (!sv_is_non_empty(inl->as.code_span.text)) {
        set_err(err, DOC_MODEL_VALIDATION_EMPTY_REQUIRED, "inline.code_span", index);
        return DOC_MODEL_ERR_INVALID;
      }
      return DOC_MODEL_OK;

    case DOC_MODEL_INLINE_LINK:
      if (!sv_is_non_empty(inl->as.link.href)) {
        set_err(err, DOC_MODEL_VALIDATION_BAD_LINK, "inline.link.href", index);
        return DOC_MODEL_ERR_INVALID;
      }
      return validate_inline_list(inl->as.link.children.items,
                                  inl->as.link.children.count,
                                  depth + 1,
                                  err);

    case DOC_MODEL_INLINE_IMAGE:
      if (!sv_is_non_empty(inl->as.image.asset_id)) {
        set_err(err, DOC_MODEL_VALIDATION_BAD_IMAGE, "inline.image.asset_id", index);
        return DOC_MODEL_ERR_INVALID;
      }
      return DOC_MODEL_OK;

    case DOC_MODEL_INLINE_LINE_BREAK:
      return DOC_MODEL_OK;

    default:
      set_err(err, DOC_MODEL_VALIDATION_BAD_ENUM, "inline.kind", index);
      return DOC_MODEL_ERR_INVALID;
  }
}

static int validate_inline_list(const doc_model_inline* inlines,
                                usize count,
                                usize depth,
                                doc_model_validation_error* err) {
  usize i;

  if (count > 0 && inlines == 0) {
    set_err(err, DOC_MODEL_VALIDATION_NULL_POINTER, "inline.list", 0);
    return DOC_MODEL_ERR_INVALID;
  }

  for (i = 0; i < count; i++) {
    int rc = validate_inline(&inlines[i], i, depth, err);
    if (rc != DOC_MODEL_OK) {
      return rc;
    }
  }

  return DOC_MODEL_OK;
}

static int validate_table_rows(const doc_model_table_row* rows,
                               usize row_count,
                               usize depth,
                               doc_model_validation_error* err) {
  usize r;
  usize c;

  if (row_count == 0 || rows == 0) {
    set_err(err, DOC_MODEL_VALIDATION_BAD_TABLE, "block.table.rows", 0);
    return DOC_MODEL_ERR_INVALID;
  }

  for (r = 0; r < row_count; r++) {
    const doc_model_table_row* row = &rows[r];
    if (row->cell_count == 0 || row->cells == 0) {
      set_err(err, DOC_MODEL_VALIDATION_BAD_TABLE, "block.table.cells", r);
      return DOC_MODEL_ERR_INVALID;
    }
    for (c = 0; c < row->cell_count; c++) {
      const doc_model_table_cell* cell = &row->cells[c];
      int rc = validate_block_list(cell->blocks.items, cell->blocks.count, depth + 1, err);
      if (rc != DOC_MODEL_OK) {
        return rc;
      }
    }
  }

  return DOC_MODEL_OK;
}

static int validate_block(const doc_model_block* block,
                          usize index,
                          usize depth,
                          doc_model_validation_error* err) {
  if (block == 0) {
    set_err(err, DOC_MODEL_VALIDATION_NULL_POINTER, "block", index);
    return DOC_MODEL_ERR_INVALID;
  }

  if (depth > DOC_MODEL_MAX_VALIDATE_DEPTH) {
    set_err(err, DOC_MODEL_VALIDATION_DEPTH_LIMIT, "block.depth", index);
    return DOC_MODEL_ERR_LIMIT;
  }

  switch (block->kind) {
    case DOC_MODEL_BLOCK_PARAGRAPH:
      return validate_inline_list(block->as.paragraph.inlines.items,
                                  block->as.paragraph.inlines.count,
                                  depth + 1,
                                  err);

    case DOC_MODEL_BLOCK_HEADING:
      if (block->as.heading.level < 1 || block->as.heading.level > 6) {
        set_err(err, DOC_MODEL_VALIDATION_BAD_HEADING_LEVEL, "block.heading.level", index);
        return DOC_MODEL_ERR_INVALID;
      }
      return validate_inline_list(block->as.heading.inlines.items,
                                  block->as.heading.inlines.count,
                                  depth + 1,
                                  err);

    case DOC_MODEL_BLOCK_LIST:
      return validate_block_list(block->as.list.items.items,
                                 block->as.list.items.count,
                                 depth + 1,
                                 err);

    case DOC_MODEL_BLOCK_LIST_ITEM:
      if (block->as.list_item.blocks.count == 0) {
        set_err(err, DOC_MODEL_VALIDATION_EMPTY_REQUIRED, "block.list_item.blocks", index);
        return DOC_MODEL_ERR_INVALID;
      }
      return validate_block_list(block->as.list_item.blocks.items,
                                 block->as.list_item.blocks.count,
                                 depth + 1,
                                 err);

    case DOC_MODEL_BLOCK_QUOTE:
      return validate_block_list(block->as.quote.blocks.items,
                                 block->as.quote.blocks.count,
                                 depth + 1,
                                 err);

    case DOC_MODEL_BLOCK_CODE_BLOCK:
      if (block->as.code_block.text.data == 0 && block->as.code_block.text.len != 0) {
        set_err(err, DOC_MODEL_VALIDATION_NULL_POINTER, "block.code_block.text", index);
        return DOC_MODEL_ERR_INVALID;
      }
      return DOC_MODEL_OK;

    case DOC_MODEL_BLOCK_TABLE:
      return validate_table_rows(block->as.table.rows, block->as.table.row_count, depth + 1, err);

    case DOC_MODEL_BLOCK_ADMONITION:
      if (!sv_is_non_empty(block->as.admonition.kind)) {
        set_err(err, DOC_MODEL_VALIDATION_EMPTY_REQUIRED, "block.admonition.kind", index);
        return DOC_MODEL_ERR_INVALID;
      }
      return validate_block_list(block->as.admonition.blocks.items,
                                 block->as.admonition.blocks.count,
                                 depth + 1,
                                 err);

    case DOC_MODEL_BLOCK_FIGURE:
      if (!sv_is_non_empty(block->as.figure.asset_id)) {
        set_err(err, DOC_MODEL_VALIDATION_BAD_IMAGE, "block.figure.asset_id", index);
        return DOC_MODEL_ERR_INVALID;
      }
      if (block->as.figure.caption.data == 0 && block->as.figure.caption.len != 0) {
        set_err(err, DOC_MODEL_VALIDATION_NULL_POINTER, "block.figure.caption", index);
        return DOC_MODEL_ERR_INVALID;
      }
      return DOC_MODEL_OK;

    default:
      set_err(err, DOC_MODEL_VALIDATION_BAD_ENUM, "block.kind", index);
      return DOC_MODEL_ERR_INVALID;
  }
}

static int validate_block_list(const doc_model_block* blocks,
                               usize count,
                               usize depth,
                               doc_model_validation_error* err) {
  usize i;

  if (count > 0 && blocks == 0) {
    set_err(err, DOC_MODEL_VALIDATION_NULL_POINTER, "block.list", 0);
    return DOC_MODEL_ERR_INVALID;
  }

  for (i = 0; i < count; i++) {
    int rc = validate_block(&blocks[i], i, depth, err);
    if (rc != DOC_MODEL_OK) {
      return rc;
    }
  }

  return DOC_MODEL_OK;
}

int doc_model_validate_metadata(const doc_model_metadata* metadata,
                                doc_model_validation_error* err) {
  if (metadata == 0) {
    set_err(err, DOC_MODEL_VALIDATION_NULL_POINTER, "metadata", 0);
    return DOC_MODEL_ERR_INVALID;
  }

  if (metadata->title.data == 0 && metadata->title.len != 0) {
    set_err(err, DOC_MODEL_VALIDATION_NULL_POINTER, "metadata.title", 0);
    return DOC_MODEL_ERR_INVALID;
  }
  if (metadata->author.data == 0 && metadata->author.len != 0) {
    set_err(err, DOC_MODEL_VALIDATION_NULL_POINTER, "metadata.author", 0);
    return DOC_MODEL_ERR_INVALID;
  }
  if (metadata->language.data == 0 && metadata->language.len != 0) {
    set_err(err, DOC_MODEL_VALIDATION_NULL_POINTER, "metadata.language", 0);
    return DOC_MODEL_ERR_INVALID;
  }

  return DOC_MODEL_OK;
}

int doc_model_validate_blocks(const doc_model_block* blocks,
                              usize count,
                              doc_model_validation_error* err) {
  return validate_block_list(blocks, count, 0, err);
}

int doc_model_validate_styles(const doc_model_style* styles,
                              usize count,
                              doc_model_validation_error* err) {
  usize i;

  if (count > 0 && styles == 0) {
    set_err(err, DOC_MODEL_VALIDATION_NULL_POINTER, "styles", 0);
    return DOC_MODEL_ERR_INVALID;
  }

  for (i = 0; i < count; i++) {
    const doc_model_style* st = &styles[i];
    if (!sv_is_non_empty(st->id)) {
      set_err(err, DOC_MODEL_VALIDATION_BAD_STYLE, "styles.id", i);
      return DOC_MODEL_ERR_INVALID;
    }
    if (st->target != DOC_MODEL_STYLE_BLOCK && st->target != DOC_MODEL_STYLE_INLINE) {
      set_err(err, DOC_MODEL_VALIDATION_BAD_ENUM, "styles.target", i);
      return DOC_MODEL_ERR_INVALID;
    }
  }

  return DOC_MODEL_OK;
}

int doc_model_validate_assets(const doc_model_asset* assets,
                              usize count,
                              doc_model_validation_error* err) {
  usize i;

  if (count > 0 && assets == 0) {
    set_err(err, DOC_MODEL_VALIDATION_NULL_POINTER, "assets", 0);
    return DOC_MODEL_ERR_INVALID;
  }

  for (i = 0; i < count; i++) {
    const doc_model_asset* asset = &assets[i];
    if (!sv_is_non_empty(asset->id)) {
      set_err(err, DOC_MODEL_VALIDATION_EMPTY_REQUIRED, "assets.id", i);
      return DOC_MODEL_ERR_INVALID;
    }
    if (asset->kind != DOC_MODEL_ASSET_EMBEDDED && asset->kind != DOC_MODEL_ASSET_EXTERNAL) {
      set_err(err, DOC_MODEL_VALIDATION_BAD_ENUM, "assets.kind", i);
      return DOC_MODEL_ERR_INVALID;
    }
    if (!sv_is_non_empty(asset->mime_type)) {
      set_err(err, DOC_MODEL_VALIDATION_EMPTY_REQUIRED, "assets.mime_type", i);
      return DOC_MODEL_ERR_INVALID;
    }
    if (asset->kind == DOC_MODEL_ASSET_EMBEDDED) {
      if (asset->bytes == 0 && asset->bytes_len != 0) {
        set_err(err, DOC_MODEL_VALIDATION_NULL_POINTER, "assets.bytes", i);
        return DOC_MODEL_ERR_INVALID;
      }
    } else {
      if (!sv_is_non_empty(asset->source_uri)) {
        set_err(err, DOC_MODEL_VALIDATION_EMPTY_REQUIRED, "assets.source_uri", i);
        return DOC_MODEL_ERR_INVALID;
      }
    }
  }

  return DOC_MODEL_OK;
}

int doc_model_validate_document(const doc_model_document* doc,
                                doc_model_validation_error* err) {
  int rc;

  if (doc == 0) {
    set_err(err, DOC_MODEL_VALIDATION_NULL_POINTER, "document", 0);
    return DOC_MODEL_ERR_INVALID;
  }

  rc = doc_model_validate_metadata(&doc->metadata, err);
  if (rc != DOC_MODEL_OK) {
    return rc;
  }

  rc = doc_model_validate_blocks(doc->blocks.items, doc->blocks.count, err);
  if (rc != DOC_MODEL_OK) {
    return rc;
  }

  rc = doc_model_validate_styles(doc->styles, doc->style_count, err);
  if (rc != DOC_MODEL_OK) {
    return rc;
  }

  rc = doc_model_validate_assets(doc->assets, doc->asset_count, err);
  if (rc != DOC_MODEL_OK) {
    return rc;
  }

  if (doc->extension_count > 0 && doc->extensions == 0) {
    set_err(err, DOC_MODEL_VALIDATION_NULL_POINTER, "extensions", 0);
    return DOC_MODEL_ERR_INVALID;
  }

  return DOC_MODEL_OK;
}
