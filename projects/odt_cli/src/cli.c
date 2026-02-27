#include "odt_cli/cli.h"

#include "convert_core/convert_core.h"
#include "fmt_markdown/fmt_markdown.h"
#include "fmt_odt/fmt_odt.h"
#include "odt_core/odt_core.h"
#include "rt/rt.h"
#include "zip/zip.h"

#ifndef CLI_MAX_FILE_BYTES
#define CLI_MAX_FILE_BYTES (32U * 1024U * 1024U)
#endif

static void write_str(const char* s) {
  rt_write_all(1, s, rt_strlen(s));
}

static void write_str_err(const char* s) {
  rt_write_all(2, s, rt_strlen(s));
}

static void write_u64_fd(int fd, u64 v) {
  char buf[32];
  usize n = rt_u64_to_dec(v, buf, sizeof(buf));
  rt_write_all(fd, buf, n);
}

static void write_u64(u64 v) {
  char buf[32];
  usize n = rt_u64_to_dec(v, buf, sizeof(buf));
  rt_write_all(1, buf, n);
}

static int cstr_eq(const char* a, const char* b) {
  usize i = 0;
  while (a[i] != '\0' && b[i] != '\0') {
    if (a[i] != b[i]) {
      return 0;
    }
    i++;
  }
  return a[i] == '\0' && b[i] == '\0';
}

static int cstr_starts_with(const char* s, const char* pfx) {
  usize i = 0;
  while (pfx[i] != '\0') {
    if (s[i] != pfx[i]) {
      return 0;
    }
    i++;
  }
  return 1;
}

static int bytes_eq_n(const char* a, const char* b, usize n) {
  usize i;
  for (i = 0; i < n; i++) {
    if (a[i] != b[i]) {
      return 0;
    }
  }
  return 1;
}

static int sv_eq_lit(const char* s, usize n, const char* lit) {
  usize ln = rt_strlen(lit);
  return n == ln && bytes_eq_n(s, lit, n);
}

static int path_has_suffix(const char* path, const char* suffix) {
  usize plen = rt_strlen(path);
  usize slen = rt_strlen(suffix);
  usize i;
  if (plen < slen) {
    return 0;
  }
  for (i = 0; i < slen; i++) {
    if (path[plen - slen + i] != suffix[i]) {
      return 0;
    }
  }
  return 1;
}

static int is_space_char(char c) {
  return c == ' ' || c == '\t';
}

static int is_role_char(char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' ||
         c == '-';
}

static usize trim_left(const char* s, usize n) {
  usize i = 0;
  while (i < n && is_space_char(s[i])) {
    i++;
  }
  return i;
}

static usize trim_right_len(const char* s, usize n) {
  while (n > 0 && is_space_char(s[n - 1])) {
    n--;
  }
  return n;
}

/* YAML-like inline comments: strip from '#' unless inside single/double quotes. */
static usize trim_inline_comment_len(const char* s, usize n) {
  usize i;
  int in_sq = 0;
  int in_dq = 0;
  for (i = 0; i < n; i++) {
    char c = s[i];
    if (c == '\\' && i + 1 < n) {
      i++;
      continue;
    }
    if (c == '\'' && !in_dq) {
      in_sq = !in_sq;
      continue;
    }
    if (c == '"' && !in_sq) {
      in_dq = !in_dq;
      continue;
    }
    if (c == '#' && !in_sq && !in_dq) {
      return trim_right_len(s, i);
    }
  }
  return n;
}

static int parse_u64_sv(const char* s, usize n, u64* out) {
  usize i;
  u64 v = 0;
  if (n == 0) {
    return 0;
  }
  for (i = 0; i < n; i++) {
    if (s[i] < '0' || s[i] > '9') {
      return 0;
    }
    v = v * 10 + (u64)(s[i] - '0');
  }
  *out = v;
  return 1;
}

static int quoted_nonempty(const char* s, usize n) {
  char q;
  if (n < 2) {
    return 0;
  }
  q = s[0];
  if (!((q == '"' || q == '\'') && s[n - 1] == q)) {
    return 0;
  }
  return n > 2;
}

static int valid_role_name(const char* s, usize n) {
  usize i;
  if (n == 0) {
    return 0;
  }
  for (i = 0; i < n; i++) {
    if (!is_role_char(s[i])) {
      return 0;
    }
  }
  return 1;
}

static int validate_match_expr(const char* s, usize n) {
  u64 level = 0;
  if (sv_eq_lit(s, n, "paragraph") || sv_eq_lit(s, n, "blockquote") || sv_eq_lit(s, n, "list") ||
      sv_eq_lit(s, n, "table") || sv_eq_lit(s, n, "codeblock")) {
    return 1;
  }
  if (n > 15 && cstr_starts_with(s, "heading(level=") && s[n - 1] == ')') {
    if (parse_u64_sv(s + 14, n - 15, &level) && level >= 1 && level <= 6) {
      return 1;
    }
  }
  return 0;
}

static int validate_where_predicate(const char* s, usize n) {
  usize off;
  if (n >= 11 && cstr_starts_with(s, "startswith ")) {
    off = 11;
    return quoted_nonempty(s + off, n - off);
  }
  if (n >= 7 && cstr_starts_with(s, "equals ")) {
    off = 7;
    return quoted_nonempty(s + off, n - off);
  }
  if (n >= 9 && cstr_starts_with(s, "endswith ")) {
    off = 9;
    return quoted_nonempty(s + off, n - off);
  }
  if (n >= 6 && cstr_starts_with(s, "regex ")) {
    off = 6;
    return quoted_nonempty(s + off, n - off);
  }
  if (n >= 7 && cstr_starts_with(s, "regex: ")) {
    off = 7;
    return quoted_nonempty(s + off, n - off);
  }
  if (sv_eq_lit(s, n, "uppercase == true") || sv_eq_lit(s, n, "uppercase == false")) {
    return 1;
  }
  if (sv_eq_lit(s, n, "position == first") || sv_eq_lit(s, n, "position == last")) {
    return 1;
  }
  if (n > 7 && cstr_starts_with(s, "before ") && valid_role_name(s + 7, n - 7)) {
    return 1;
  }
  if (n > 6 && cstr_starts_with(s, "after ") && valid_role_name(s + 6, n - 6)) {
    return 1;
  }
  if (n > 8 && cstr_starts_with(s, "follows ") && valid_role_name(s + 8, n - 8)) {
    return 1;
  }
  if (n > 9 && cstr_starts_with(s, "precedes ") && valid_role_name(s + 9, n - 9)) {
    return 1;
  }
  if (n > 3 && cstr_starts_with(s, "in ") && valid_role_name(s + 3, n - 3)) {
    return 1;
  }
  return 0;
}

static void set_parse_error(char* err, usize err_cap, usize line_no, const char* msg) {
  char num[32];
  usize n = 0;
  usize d;
  usize i;
  if (err_cap == 0) {
    return;
  }
  d = rt_u64_to_dec((u64)line_no, num, sizeof(num));
  {
    const char* pfx = "line ";
    for (i = 0; pfx[i] != '\0' && n + 1 < err_cap; i++) {
      err[n++] = pfx[i];
    }
  }
  for (i = 0; i < d && n + 1 < err_cap; i++) {
    err[n++] = num[i];
  }
  if (n + 2 < err_cap) {
    err[n++] = ':';
    err[n++] = ' ';
  }
  for (i = 0; msg[i] != '\0' && n + 1 < err_cap; i++) {
    err[n++] = msg[i];
  }
  err[n] = '\0';
}

static int parse_meltdown_profile(const u8* data, usize len, char* err, usize err_cap) {
  enum {
    SEC_NONE = 0,
    SEC_ROLES = 1,
    SEC_RECOGNIZE = 2,
    SEC_LAYOUT = 3,
    SEC_REGIONS = 4,
    SEC_PRESENT = 5,
  };
  enum {
    REC_NONE = 0,
    REC_MATCH = 1,
    REC_WHERE = 2,
  };
  enum {
    REG_NONE = 0,
    REG_START = 1,
  };
  enum {
    PRE_NONE = 0,
    PRE_PAGE = 1,
    PRE_FONTS = 2,
    PRE_MASTER = 3,
    PRE_STYLES = 4,
  };
  enum {
    PRE_MASTER_NONE = 0,
    PRE_MASTER_HEADER = 1,
    PRE_MASTER_FOOTER = 2,
  };
  usize i = 0;
  usize line_no = 0;
  int section = SEC_NONE;
  int rec_sub = REC_NONE;
  int seen_profile = 0;
  int seen_version = 0;
  int seen_roles = 0;
  int seen_recognize = 0;
  int seen_layout = 0;
  int in_rec_role = 0;
  int in_layout_role = 0;
  int in_region = 0;
  int reg_sub = REG_NONE;
  int pre_sub = PRE_NONE;
  int pre_page_margins = 0;
  int pre_master_item = 0;
  int pre_master_sub = PRE_MASTER_NONE;
  int pre_style_item = 0;

  while (i < len) {
    usize ls = i;
    usize le = i;
    usize indent = 0;
    usize tl;
    const char* line;
    line_no++;

    while (le < len && data[le] != (u8)'\n') {
      le++;
    }
    i = (le < len) ? (le + 1) : le;

    while (indent < le - ls && data[ls + indent] == (u8)' ') {
      indent++;
    }
    if (indent < le - ls && data[ls + indent] == (u8)'\t') {
      set_parse_error(err, err_cap, line_no, "tab indentation is not allowed");
      return -1;
    }

    line = (const char*)(data + ls + indent);
    tl = trim_right_len(line, le - ls - indent);
    tl = trim_inline_comment_len(line, tl);
    tl = trim_right_len(line, tl);
    tl = trim_inline_comment_len(line, tl);
    tl = trim_right_len(line, tl);
    if (tl == 0) {
      continue;
    }
    if (line[0] == '#') {
      continue;
    }

    if (indent == 0) {
      usize k = 0;
      while (k < tl && line[k] != ':') {
        k++;
      }
      if (k == tl) {
        set_parse_error(err, err_cap, line_no, "expected top-level key");
        return -1;
      }
      {
        const char* v = line + k + 1;
        usize vl = trim_left(v, tl - (k + 1));
        v += vl;
        vl = trim_right_len(v, tl - (k + 1) - vl);

        if (k == 7 && bytes_eq_n(line, "profile", 7)) {
          if (vl == 0) {
            set_parse_error(err, err_cap, line_no, "profile value is required");
            return -1;
          }
          seen_profile = 1;
          section = SEC_NONE;
        } else if (k == 7 && bytes_eq_n(line, "version", 7)) {
          u64 ver = 0;
          if (!parse_u64_sv(v, vl, &ver) || ver != 1) {
            set_parse_error(err, err_cap, line_no, "version must be integer 1");
            return -1;
          }
          seen_version = 1;
          section = SEC_NONE;
        } else if (k == 5 && bytes_eq_n(line, "roles", 5)) {
          if (vl != 0) {
            set_parse_error(err, err_cap, line_no, "roles key must not have inline value");
            return -1;
          }
          seen_roles = 1;
          section = SEC_ROLES;
          in_rec_role = 0;
          in_layout_role = 0;
        } else if (k == 9 && bytes_eq_n(line, "recognize", 9)) {
          if (vl != 0) {
            set_parse_error(err, err_cap, line_no, "recognize key must not have inline value");
            return -1;
          }
          seen_recognize = 1;
          section = SEC_RECOGNIZE;
          in_rec_role = 0;
          in_layout_role = 0;
          rec_sub = REC_NONE;
        } else if (k == 6 && bytes_eq_n(line, "layout", 6)) {
          if (vl != 0) {
            set_parse_error(err, err_cap, line_no, "layout key must not have inline value");
            return -1;
          }
          seen_layout = 1;
          section = SEC_LAYOUT;
          in_rec_role = 0;
          in_layout_role = 0;
        } else if (k == 7 && bytes_eq_n(line, "regions", 7)) {
          if (vl != 0) {
            set_parse_error(err, err_cap, line_no, "regions key must not have inline value");
            return -1;
          }
          section = SEC_REGIONS;
          in_region = 0;
          reg_sub = REG_NONE;
        } else if (k == 7 && bytes_eq_n(line, "present", 7)) {
          if (vl != 0) {
            set_parse_error(err, err_cap, line_no, "present key must not have inline value");
            return -1;
          }
          section = SEC_PRESENT;
          pre_sub = PRE_NONE;
          pre_page_margins = 0;
          pre_master_item = 0;
          pre_master_sub = PRE_MASTER_NONE;
          pre_style_item = 0;
        } else {
          set_parse_error(err, err_cap, line_no, "unknown top-level key");
          return -1;
        }
      }
      continue;
    }

    if (section == SEC_ROLES) {
      if (indent != 2 || line[tl - 1] != ':') {
        set_parse_error(err, err_cap, line_no, "roles entries must be '  Role:'");
        return -1;
      }
      if (!valid_role_name(line, tl - 1)) {
        set_parse_error(err, err_cap, line_no, "invalid role name in roles section");
        return -1;
      }
      continue;
    }

    if (section == SEC_RECOGNIZE) {
      if (indent == 2) {
        if (line[tl - 1] != ':' || !valid_role_name(line, tl - 1)) {
          set_parse_error(err, err_cap, line_no, "invalid role header in recognize section");
          return -1;
        }
        in_rec_role = 1;
        rec_sub = REC_NONE;
        continue;
      }
      if (!in_rec_role) {
        set_parse_error(err, err_cap, line_no, "recognize rule content without role header");
        return -1;
      }
      if (indent == 4) {
        usize k = 0;
        while (k < tl && line[k] != ':') {
          k++;
        }
        if (k == tl) {
          set_parse_error(err, err_cap, line_no, "expected match/where/priority key");
          return -1;
        }
        if (k == 5 && bytes_eq_n(line, "match", 5)) {
          rec_sub = REC_MATCH;
          continue;
        }
        if (k == 5 && bytes_eq_n(line, "where", 5)) {
          rec_sub = REC_WHERE;
          continue;
        }
        if (k == 8 && bytes_eq_n(line, "priority", 8)) {
          const char* v = line + k + 1;
          usize vl = trim_left(v, tl - (k + 1));
          u64 pv = 0;
          v += vl;
          vl = trim_right_len(v, tl - (k + 1) - vl);
          if (!parse_u64_sv(v, vl, &pv)) {
            set_parse_error(err, err_cap, line_no, "priority must be an integer");
            return -1;
          }
          rec_sub = REC_NONE;
          continue;
        }
        set_parse_error(err, err_cap, line_no, "unknown recognize rule key");
        return -1;
      }
      if (indent == 6) {
        const char* expr = 0;
        usize el = 0;
        usize off;
        if (!(tl >= 2 && line[0] == '-' && line[1] == ' ')) {
          set_parse_error(err, err_cap, line_no, "expected list item '- ...' in recognize rule");
          return -1;
        }
        off = trim_left(line + 2, tl - 2);
        expr = line + 2 + off;
        el = trim_right_len(expr, tl - 2 - off);
        if (rec_sub == REC_MATCH) {
          if (!validate_match_expr(expr, el)) {
            set_parse_error(err, err_cap, line_no, "invalid match expression");
            return -1;
          }
          continue;
        }
        if (rec_sub == REC_WHERE) {
          if (!validate_where_predicate(expr, el)) {
            set_parse_error(err, err_cap, line_no, "invalid where predicate");
            return -1;
          }
          continue;
        }
        set_parse_error(err, err_cap, line_no, "list item outside match/where section");
        return -1;
      }
      set_parse_error(err, err_cap, line_no, "unsupported indentation in recognize section");
      return -1;
    }

    if (section == SEC_LAYOUT) {
      if (indent == 2) {
        if (line[tl - 1] != ':' || !valid_role_name(line, tl - 1)) {
          set_parse_error(err, err_cap, line_no, "invalid role header in layout section");
          return -1;
        }
        in_layout_role = 1;
        continue;
      }
      if (!in_layout_role) {
        set_parse_error(err, err_cap, line_no, "layout content without role header");
        return -1;
      }
      if (indent == 4) {
        usize k = 0;
        const char* v;
        usize vl;
        while (k < tl && line[k] != ':') {
          k++;
        }
        if (!(k == 5 && bytes_eq_n(line, "style", 5))) {
          set_parse_error(err, err_cap, line_no, "layout supports only 'style:' in v0.1");
          return -1;
        }
        v = line + k + 1;
        vl = trim_left(v, tl - (k + 1));
        v += vl;
        vl = trim_right_len(v, tl - (k + 1) - vl);
        if (vl == 0) {
          set_parse_error(err, err_cap, line_no, "style value is required");
          return -1;
        }
        continue;
      }
      set_parse_error(err, err_cap, line_no, "unsupported indentation in layout section");
      return -1;
    }

    if (section == SEC_REGIONS) {
      if (indent == 2) {
        if (line[tl - 1] != ':' || !valid_role_name(line, tl - 1)) {
          set_parse_error(err, err_cap, line_no, "invalid region header in regions section");
          return -1;
        }
        in_region = 1;
        reg_sub = REG_NONE;
        continue;
      }
      if (!in_region) {
        set_parse_error(err, err_cap, line_no, "regions content without region header");
        return -1;
      }
      if (indent == 4) {
        if (!(tl == 6 && bytes_eq_n(line, "start:", 6))) {
          set_parse_error(err, err_cap, line_no, "regions supports only 'start:' in v0.1");
          return -1;
        }
        reg_sub = REG_START;
        continue;
      }
      if (indent == 6) {
        const char* expr;
        usize el;
        usize off;
        if (reg_sub != REG_START || !(tl >= 2 && line[0] == '-' && line[1] == ' ')) {
          set_parse_error(err, err_cap, line_no, "invalid regions start list item");
          return -1;
        }
        off = trim_left(line + 2, tl - 2);
        expr = line + 2 + off;
        el = trim_right_len(expr, tl - 2 - off);
        if (!valid_role_name(expr, el)) {
          set_parse_error(err, err_cap, line_no, "invalid role reference in region start list");
          return -1;
        }
        continue;
      }
      set_parse_error(err, err_cap, line_no, "unsupported indentation in regions section");
      return -1;
    }

    if (section == SEC_PRESENT) {
      if (indent == 2) {
        usize k = 0;
        const char* v;
        usize vl;
        while (k < tl && line[k] != ':') {
          k++;
        }
        if (k == tl) {
          set_parse_error(err, err_cap, line_no, "invalid present section key");
          return -1;
        }
        v = line + k + 1;
        vl = trim_left(v, tl - (k + 1));
        v += vl;
        vl = trim_right_len(v, tl - (k + 1) - vl);
        if (vl != 0) {
          set_parse_error(err, err_cap, line_no, "present subsection must not have inline value");
          return -1;
        }
        if (k == 4 && bytes_eq_n(line, "page", 4)) {
          pre_sub = PRE_PAGE;
          pre_page_margins = 0;
          continue;
        }
        if (k == 5 && bytes_eq_n(line, "fonts", 5)) {
          pre_sub = PRE_FONTS;
          continue;
        }
        if (k == 6 && bytes_eq_n(line, "master", 6)) {
          pre_sub = PRE_MASTER;
          pre_master_item = 0;
          pre_master_sub = PRE_MASTER_NONE;
          continue;
        }
        if (k == 6 && bytes_eq_n(line, "styles", 6)) {
          pre_sub = PRE_STYLES;
          pre_style_item = 0;
          continue;
        }
        set_parse_error(err, err_cap, line_no, "unknown present subsection");
        return -1;
      }

      if (pre_sub == PRE_PAGE) {
        if (indent == 4) {
          usize k = 0;
          const char* v;
          usize vl;
          while (k < tl && line[k] != ':') {
            k++;
          }
          if (k == tl) {
            set_parse_error(err, err_cap, line_no, "invalid present.page key");
            return -1;
          }
          v = line + k + 1;
          vl = trim_left(v, tl - (k + 1));
          v += vl;
          vl = trim_right_len(v, tl - (k + 1) - vl);

          if (k == 7 && bytes_eq_n(line, "margins", 7)) {
            if (vl != 0) {
              set_parse_error(err, err_cap, line_no, "present.page.margins must not have inline value");
              return -1;
            }
            pre_page_margins = 1;
            continue;
          }

          if (!((k == 4 && bytes_eq_n(line, "size", 4)) ||
                (k == 11 && bytes_eq_n(line, "orientation", 11)))) {
            set_parse_error(err, err_cap, line_no, "unknown present.page key");
            return -1;
          }
          if (vl == 0) {
            set_parse_error(err, err_cap, line_no, "present.page value is required");
            return -1;
          }
          pre_page_margins = 0;
          continue;
        }
        if (indent == 6) {
          usize k = 0;
          const char* v;
          usize vl;
          if (!pre_page_margins) {
            set_parse_error(err, err_cap, line_no, "unexpected present.page nested content");
            return -1;
          }
          while (k < tl && line[k] != ':') {
            k++;
          }
          if (k == tl) {
            set_parse_error(err, err_cap, line_no, "invalid present.page.margins key");
            return -1;
          }
          if (!((k == 3 && bytes_eq_n(line, "top", 3)) ||
                (k == 5 && bytes_eq_n(line, "right", 5)) ||
                (k == 6 && bytes_eq_n(line, "bottom", 6)) ||
                (k == 4 && bytes_eq_n(line, "left", 4)))) {
            set_parse_error(err, err_cap, line_no, "unknown present.page.margins key");
            return -1;
          }
          v = line + k + 1;
          vl = trim_left(v, tl - (k + 1));
          v += vl;
          vl = trim_right_len(v, tl - (k + 1) - vl);
          if (vl == 0) {
            set_parse_error(err, err_cap, line_no, "present.page.margins value is required");
            return -1;
          }
          continue;
        }
        set_parse_error(err, err_cap, line_no, "unsupported indentation in present.page");
        return -1;
      }

      if (pre_sub == PRE_FONTS) {
        if (indent == 4) {
          usize k = 0;
          const char* v;
          usize vl;
          while (k < tl && line[k] != ':') {
            k++;
          }
          if (k == tl) {
            set_parse_error(err, err_cap, line_no, "invalid present.fonts key");
            return -1;
          }
          if (!((k == 4 && bytes_eq_n(line, "body", 4)) ||
                (k == 7 && bytes_eq_n(line, "heading", 7)) ||
                (k == 4 && bytes_eq_n(line, "mono", 4)))) {
            set_parse_error(err, err_cap, line_no, "unknown present.fonts key");
            return -1;
          }
          v = line + k + 1;
          vl = trim_left(v, tl - (k + 1));
          v += vl;
          vl = trim_right_len(v, tl - (k + 1) - vl);
          if (vl == 0) {
            set_parse_error(err, err_cap, line_no, "present.fonts value is required");
            return -1;
          }
          continue;
        }
        set_parse_error(err, err_cap, line_no, "unsupported indentation in present.fonts");
        return -1;
      }

      if (pre_sub == PRE_MASTER) {
        if (indent == 4) {
          const char* v;
          usize vl;
          if (tl < 2 || line[tl - 1] != ':' || !valid_role_name(line, tl - 1)) {
            set_parse_error(err, err_cap, line_no, "invalid present.master page key");
            return -1;
          }
          v = line + (tl - 1) + 1;
          vl = trim_left(v, 0);
          (void)vl;
          pre_master_item = 1;
          pre_master_sub = PRE_MASTER_NONE;
          continue;
        }
        if (indent == 6) {
          usize k = 0;
          if (!pre_master_item) {
            set_parse_error(err, err_cap, line_no, "present.master content without page key");
            return -1;
          }
          while (k < tl && line[k] != ':') {
            k++;
          }
          if (k == tl) {
            set_parse_error(err, err_cap, line_no, "invalid present.master key");
            return -1;
          }
          if (k == 6 && bytes_eq_n(line, "header", 6)) {
            pre_master_sub = PRE_MASTER_HEADER;
            continue;
          }
          if (k == 6 && bytes_eq_n(line, "footer", 6)) {
            pre_master_sub = PRE_MASTER_FOOTER;
            continue;
          }
          set_parse_error(err, err_cap, line_no, "unknown present.master key");
          return -1;
        }
        if (indent == 8) {
          usize k = 0;
          const char* v;
          usize vl;
          if (pre_master_sub == PRE_MASTER_NONE) {
            set_parse_error(err, err_cap, line_no, "present.master nested key without header/footer");
            return -1;
          }
          while (k < tl && line[k] != ':') {
            k++;
          }
          if (k == tl) {
            set_parse_error(err, err_cap, line_no, "invalid present.master header/footer key");
            return -1;
          }
          if (!((k == 4 && bytes_eq_n(line, "left", 4)) ||
                (k == 5 && bytes_eq_n(line, "right", 5)))) {
            set_parse_error(err, err_cap, line_no, "unknown present.master header/footer key");
            return -1;
          }
          v = line + k + 1;
          vl = trim_left(v, tl - (k + 1));
          v += vl;
          vl = trim_right_len(v, tl - (k + 1) - vl);
          if (vl == 0) {
            set_parse_error(err, err_cap, line_no, "present.master header/footer value is required");
            return -1;
          }
          continue;
        }
        set_parse_error(err, err_cap, line_no, "unsupported indentation in present.master");
        return -1;
      }

      if (pre_sub == PRE_STYLES) {
        if (indent == 4) {
          if (tl < 2 || line[tl - 1] != ':' || !valid_role_name(line, tl - 1)) {
            set_parse_error(err, err_cap, line_no, "invalid present.styles style key");
            return -1;
          }
          pre_style_item = 1;
          continue;
        }
        if (indent == 6) {
          usize k = 0;
          const char* v;
          usize vl;
          if (!pre_style_item) {
            set_parse_error(err, err_cap, line_no, "present.styles property without style key");
            return -1;
          }
          while (k < tl && line[k] != ':') {
            k++;
          }
          if (k == tl) {
            set_parse_error(err, err_cap, line_no, "invalid present.styles property");
            return -1;
          }
          if (!((k == 6 && bytes_eq_n(line, "family", 6)) ||
                (k == 8 && bytes_eq_n(line, "based-on", 8)) ||
                (k == 9 && bytes_eq_n(line, "font-size", 9)) ||
                (k == 11 && bytes_eq_n(line, "font-weight", 11)) ||
                (k == 10 && bytes_eq_n(line, "font-style", 10)) ||
                (k == 5 && bytes_eq_n(line, "align", 5)) ||
                (k == 10 && bytes_eq_n(line, "margin-top", 10)) ||
                (k == 13 && bytes_eq_n(line, "margin-bottom", 13)) ||
                (k == 11 && bytes_eq_n(line, "margin-left", 11)) ||
                (k == 12 && bytes_eq_n(line, "margin-right", 12)) ||
                (k == 11 && bytes_eq_n(line, "text-indent", 11)) ||
                (k == 11 && bytes_eq_n(line, "line-height", 11)))) {
            set_parse_error(err, err_cap, line_no, "unknown present.styles property");
            return -1;
          }
          v = line + k + 1;
          vl = trim_left(v, tl - (k + 1));
          v += vl;
          vl = trim_right_len(v, tl - (k + 1) - vl);
          if (vl == 0) {
            set_parse_error(err, err_cap, line_no, "present.styles property value is required");
            return -1;
          }
          continue;
        }
        set_parse_error(err, err_cap, line_no, "unsupported indentation in present.styles");
        return -1;
      }

      set_parse_error(err, err_cap, line_no, "present content without subsection");
      return -1;

      continue;
    }

    set_parse_error(err, err_cap, line_no, "indented content outside known section");
    return -1;
  }

  if (!seen_profile || !seen_version || !seen_roles || !seen_recognize || !seen_layout) {
    set_parse_error(err, err_cap, line_no + 1, "profile must contain profile/version/roles/recognize/layout");
    return -1;
  }
  return 0;
}

typedef enum meltdown_block_kind {
  MELTDOWN_BLOCK_HEADING = 1,
  MELTDOWN_BLOCK_PARAGRAPH = 2,
  MELTDOWN_BLOCK_BLOCKQUOTE = 3,
  MELTDOWN_BLOCK_LIST = 4,
  MELTDOWN_BLOCK_TABLE = 5,
  MELTDOWN_BLOCK_CODEBLOCK = 6,
} meltdown_block_kind;

typedef struct meltdown_block_entry {
  meltdown_block_kind kind;
  u8 heading_level;
} meltdown_block_entry;

#define MELTDOWN_MAX_STREAM_BLOCKS 32768

typedef struct meltdown_block_stream {
  meltdown_block_entry blocks[MELTDOWN_MAX_STREAM_BLOCKS];
  usize count;
  usize dropped;
} meltdown_block_stream;

#define MELTDOWN_MAX_ROLES 128
#define MELTDOWN_MAX_RULES 256
#define MELTDOWN_MAX_RULE_PREDS 12
#define MELTDOWN_MAX_REGIONS 64
#define MELTDOWN_MAX_REGION_START_ROLES 16
#define MELTDOWN_MAX_MASTER_PAGES 8
#define MELTDOWN_MAX_PRESENT_STYLES 128
#define MELTDOWN_MAX_STYLE_PROPS 24

typedef enum meltdown_pred_kind {
  MELT_PRED_POSITION_FIRST = 1,
  MELT_PRED_POSITION_LAST = 2,
  MELT_PRED_AFTER_ROLE = 3,
  MELT_PRED_FOLLOWS_ROLE = 4,
  MELT_PRED_BEFORE_ROLE = 5,
  MELT_PRED_PRECEDES_ROLE = 6,
  MELT_PRED_IN_REGION = 7,
  MELT_PRED_UNSUPPORTED_TEXT = 8,
} meltdown_pred_kind;

typedef struct meltdown_predicate {
  meltdown_pred_kind kind;
  i32 role_idx;
  i32 region_idx;
} meltdown_predicate;

typedef struct meltdown_region {
  doc_model_sv name;
  i32 start_role_idxs[MELTDOWN_MAX_REGION_START_ROLES];
  usize start_role_count;
} meltdown_region;

typedef struct meltdown_rule {
  i32 role_idx;
  meltdown_block_kind match_kind;
  u8 heading_level;
  u64 priority;
  meltdown_predicate preds[MELTDOWN_MAX_RULE_PREDS];
  usize pred_count;
} meltdown_rule;

typedef enum meltdown_present_style_prop_kind {
  MELT_STYLE_PROP_FAMILY = 1,
  MELT_STYLE_PROP_BASED_ON = 2,
  MELT_STYLE_PROP_FONT_SIZE = 3,
  MELT_STYLE_PROP_FONT_WEIGHT = 4,
  MELT_STYLE_PROP_FONT_STYLE = 5,
  MELT_STYLE_PROP_ALIGN = 6,
  MELT_STYLE_PROP_MARGIN_TOP = 7,
  MELT_STYLE_PROP_MARGIN_BOTTOM = 8,
  MELT_STYLE_PROP_MARGIN_LEFT = 9,
  MELT_STYLE_PROP_MARGIN_RIGHT = 10,
  MELT_STYLE_PROP_TEXT_INDENT = 11,
  MELT_STYLE_PROP_LINE_HEIGHT = 12,
} meltdown_present_style_prop_kind;

typedef struct meltdown_present_page {
  doc_model_sv size;
  doc_model_sv orientation;
  doc_model_sv margin_top;
  doc_model_sv margin_right;
  doc_model_sv margin_bottom;
  doc_model_sv margin_left;
} meltdown_present_page;

typedef struct meltdown_present_fonts {
  doc_model_sv body;
  doc_model_sv heading;
  doc_model_sv mono;
} meltdown_present_fonts;

typedef struct meltdown_present_master_page {
  doc_model_sv name;
  doc_model_sv header_left;
  doc_model_sv header_right;
  doc_model_sv footer_left;
  doc_model_sv footer_right;
} meltdown_present_master_page;

typedef struct meltdown_present_style_prop {
  meltdown_present_style_prop_kind kind;
  doc_model_sv value;
} meltdown_present_style_prop;

typedef struct meltdown_present_style {
  doc_model_sv name;
  meltdown_present_style_prop props[MELTDOWN_MAX_STYLE_PROPS];
  usize prop_count;
} meltdown_present_style;

typedef struct meltdown_present_model {
  meltdown_present_page page;
  meltdown_present_fonts fonts;
  meltdown_present_master_page master_pages[MELTDOWN_MAX_MASTER_PAGES];
  usize master_page_count;
  meltdown_present_style styles[MELTDOWN_MAX_PRESENT_STYLES];
  usize style_count;
} meltdown_present_model;

typedef struct meltdown_profile_runtime {
  doc_model_sv roles[MELTDOWN_MAX_ROLES];
  usize role_count;
  doc_model_sv role_styles[MELTDOWN_MAX_ROLES];
  u8 role_style_set[MELTDOWN_MAX_ROLES];
  meltdown_region regions[MELTDOWN_MAX_REGIONS];
  usize region_count;
  meltdown_rule rules[MELTDOWN_MAX_RULES];
  usize rule_count;
  meltdown_present_model present;
} meltdown_profile_runtime;

static int sv_eq(doc_model_sv a, doc_model_sv b);

static void meltdown_present_model_init(meltdown_present_model* pm) {
  usize i;
  pm->page.size.data = 0;
  pm->page.size.len = 0;
  pm->page.orientation.data = 0;
  pm->page.orientation.len = 0;
  pm->page.margin_top.data = 0;
  pm->page.margin_top.len = 0;
  pm->page.margin_right.data = 0;
  pm->page.margin_right.len = 0;
  pm->page.margin_bottom.data = 0;
  pm->page.margin_bottom.len = 0;
  pm->page.margin_left.data = 0;
  pm->page.margin_left.len = 0;

  pm->fonts.body.data = 0;
  pm->fonts.body.len = 0;
  pm->fonts.heading.data = 0;
  pm->fonts.heading.len = 0;
  pm->fonts.mono.data = 0;
  pm->fonts.mono.len = 0;

  pm->master_page_count = 0;
  for (i = 0; i < MELTDOWN_MAX_MASTER_PAGES; i++) {
    pm->master_pages[i].name.data = 0;
    pm->master_pages[i].name.len = 0;
    pm->master_pages[i].header_left.data = 0;
    pm->master_pages[i].header_left.len = 0;
    pm->master_pages[i].header_right.data = 0;
    pm->master_pages[i].header_right.len = 0;
    pm->master_pages[i].footer_left.data = 0;
    pm->master_pages[i].footer_left.len = 0;
    pm->master_pages[i].footer_right.data = 0;
    pm->master_pages[i].footer_right.len = 0;
  }

  pm->style_count = 0;
  for (i = 0; i < MELTDOWN_MAX_PRESENT_STYLES; i++) {
    pm->styles[i].name.data = 0;
    pm->styles[i].name.len = 0;
    pm->styles[i].prop_count = 0;
  }
}

static int meltdown_present_has_data(const meltdown_present_model* pm) {
  return pm->page.size.len != 0 || pm->page.orientation.len != 0 || pm->page.margin_top.len != 0 ||
         pm->page.margin_right.len != 0 || pm->page.margin_bottom.len != 0 || pm->page.margin_left.len != 0 ||
         pm->fonts.body.len != 0 || pm->fonts.heading.len != 0 || pm->fonts.mono.len != 0 ||
         pm->master_page_count != 0 || pm->style_count != 0;
}

static i32 meltdown_present_find_master_page(const meltdown_present_model* pm, doc_model_sv name) {
  usize i;
  for (i = 0; i < pm->master_page_count; i++) {
    if (sv_eq(pm->master_pages[i].name, name)) {
      return (i32)i;
    }
  }
  return -1;
}

static i32 meltdown_present_add_master_page(meltdown_present_model* pm, doc_model_sv name) {
  i32 existing = meltdown_present_find_master_page(pm, name);
  if (existing >= 0) {
    return existing;
  }
  if (pm->master_page_count >= MELTDOWN_MAX_MASTER_PAGES) {
    return -1;
  }
  pm->master_pages[pm->master_page_count].name = name;
  pm->master_page_count++;
  return (i32)(pm->master_page_count - 1);
}

static i32 meltdown_present_find_style(const meltdown_present_model* pm, doc_model_sv name) {
  usize i;
  for (i = 0; i < pm->style_count; i++) {
    if (sv_eq(pm->styles[i].name, name)) {
      return (i32)i;
    }
  }
  return -1;
}

static i32 meltdown_present_add_style(meltdown_present_model* pm, doc_model_sv name) {
  i32 existing = meltdown_present_find_style(pm, name);
  if (existing >= 0) {
    return existing;
  }
  if (pm->style_count >= MELTDOWN_MAX_PRESENT_STYLES) {
    return -1;
  }
  pm->styles[pm->style_count].name = name;
  pm->styles[pm->style_count].prop_count = 0;
  pm->style_count++;
  return (i32)(pm->style_count - 1);
}

static int meltdown_present_style_prop_kind_from_key(const char* key,
                                                     usize n,
                                                     meltdown_present_style_prop_kind* out) {
  if (n == 6 && bytes_eq_n(key, "family", 6)) {
    *out = MELT_STYLE_PROP_FAMILY;
    return 1;
  }
  if (n == 8 && bytes_eq_n(key, "based-on", 8)) {
    *out = MELT_STYLE_PROP_BASED_ON;
    return 1;
  }
  if (n == 9 && bytes_eq_n(key, "font-size", 9)) {
    *out = MELT_STYLE_PROP_FONT_SIZE;
    return 1;
  }
  if (n == 11 && bytes_eq_n(key, "font-weight", 11)) {
    *out = MELT_STYLE_PROP_FONT_WEIGHT;
    return 1;
  }
  if (n == 10 && bytes_eq_n(key, "font-style", 10)) {
    *out = MELT_STYLE_PROP_FONT_STYLE;
    return 1;
  }
  if (n == 5 && bytes_eq_n(key, "align", 5)) {
    *out = MELT_STYLE_PROP_ALIGN;
    return 1;
  }
  if (n == 10 && bytes_eq_n(key, "margin-top", 10)) {
    *out = MELT_STYLE_PROP_MARGIN_TOP;
    return 1;
  }
  if (n == 13 && bytes_eq_n(key, "margin-bottom", 13)) {
    *out = MELT_STYLE_PROP_MARGIN_BOTTOM;
    return 1;
  }
  if (n == 11 && bytes_eq_n(key, "margin-left", 11)) {
    *out = MELT_STYLE_PROP_MARGIN_LEFT;
    return 1;
  }
  if (n == 12 && bytes_eq_n(key, "margin-right", 12)) {
    *out = MELT_STYLE_PROP_MARGIN_RIGHT;
    return 1;
  }
  if (n == 11 && bytes_eq_n(key, "text-indent", 11)) {
    *out = MELT_STYLE_PROP_TEXT_INDENT;
    return 1;
  }
  if (n == 11 && bytes_eq_n(key, "line-height", 11)) {
    *out = MELT_STYLE_PROP_LINE_HEIGHT;
    return 1;
  }
  return 0;
}

static int sv_eq(doc_model_sv a, doc_model_sv b) {
  usize i;
  if (a.len != b.len) {
    return 0;
  }
  for (i = 0; i < a.len; i++) {
    if (a.data[i] != b.data[i]) {
      return 0;
    }
  }
  return 1;
}

static i32 meltdown_find_role(const meltdown_profile_runtime* p, doc_model_sv name) {
  usize i;
  for (i = 0; i < p->role_count; i++) {
    if (sv_eq(p->roles[i], name)) {
      return (i32)i;
    }
  }
  return -1;
}

static i32 meltdown_add_role(meltdown_profile_runtime* p, doc_model_sv name) {
  i32 existing = meltdown_find_role(p, name);
  if (existing >= 0) {
    return existing;
  }
  if (p->role_count >= MELTDOWN_MAX_ROLES) {
    return -1;
  }
  p->roles[p->role_count] = name;
  p->role_count++;
  return (i32)(p->role_count - 1);
}

static i32 meltdown_find_region(const meltdown_profile_runtime* p, doc_model_sv name) {
  usize i;
  for (i = 0; i < p->region_count; i++) {
    if (sv_eq(p->regions[i].name, name)) {
      return (i32)i;
    }
  }
  return -1;
}

static i32 meltdown_add_region(meltdown_profile_runtime* p, doc_model_sv name) {
  i32 existing = meltdown_find_region(p, name);
  if (existing >= 0) {
    return existing;
  }
  if (p->region_count >= MELTDOWN_MAX_REGIONS) {
    return -1;
  }
  p->regions[p->region_count].name = name;
  p->regions[p->region_count].start_role_count = 0;
  p->region_count++;
  return (i32)(p->region_count - 1);
}

static int parse_role_ref_sv(const char* s, usize n, usize off, doc_model_sv* out) {
  usize l = trim_left(s + off, n - off);
  const char* p = s + off + l;
  usize pl = trim_right_len(p, n - off - l);
  if (!valid_role_name(p, pl)) {
    return 0;
  }
  out->data = p;
  out->len = pl;
  return 1;
}

static int parse_meltdown_runtime_profile(const u8* data,
                                          usize len,
                                          meltdown_profile_runtime* out,
                                          char* err,
                                          usize err_cap) {
  enum {
    SEC_NONE = 0,
    SEC_ROLES = 1,
    SEC_RECOGNIZE = 2,
    SEC_REGIONS = 3,
    SEC_LAYOUT = 4,
    SEC_PRESENT = 5,
  };
  enum {
    REC_NONE = 0,
    REC_MATCH = 1,
    REC_WHERE = 2,
  };
  enum {
    REG_NONE = 0,
    REG_START = 1,
  };
  enum {
    PRE_NONE = 0,
    PRE_PAGE = 1,
    PRE_FONTS = 2,
    PRE_MASTER = 3,
    PRE_STYLES = 4,
  };
  enum {
    PRE_MASTER_NONE = 0,
    PRE_MASTER_HEADER = 1,
    PRE_MASTER_FOOTER = 2,
  };
  usize i = 0;
  usize line_no = 0;
  int section = SEC_NONE;
  int rec_sub = REC_NONE;
  int reg_sub = REG_NONE;
  int pre_sub = PRE_NONE;
  int pre_page_margins = 0;
  int pre_master_sub = PRE_MASTER_NONE;
  i32 cur_rec_role = -1;
  i32 cur_rule = -1;
  i32 cur_region = -1;
  i32 cur_layout_role = -1;
  i32 cur_master_page = -1;
  i32 cur_style = -1;
  usize ri;

  out->role_count = 0;
  for (ri = 0; ri < MELTDOWN_MAX_ROLES; ri++) {
    out->role_style_set[ri] = 0;
    out->role_styles[ri].data = 0;
    out->role_styles[ri].len = 0;
  }
  out->region_count = 0;
  out->rule_count = 0;
  meltdown_present_model_init(&out->present);

  while (i < len) {
    usize ls = i;
    usize le = i;
    usize indent = 0;
    const char* line;
    usize tl;
    line_no++;

    while (le < len && data[le] != (u8)'\n') {
      le++;
    }
    i = (le < len) ? (le + 1) : le;

    while (indent < le - ls && data[ls + indent] == (u8)' ') {
      indent++;
    }
    line = (const char*)(data + ls + indent);
    tl = trim_right_len(line, le - ls - indent);
    if (tl == 0 || line[0] == '#') {
      continue;
    }

    if (indent == 0) {
      if (tl >= 6 && bytes_eq_n(line, "roles", 5) && line[5] == ':') {
        section = SEC_ROLES;
      } else if (tl >= 10 && bytes_eq_n(line, "recognize", 9) && line[9] == ':') {
        section = SEC_RECOGNIZE;
        rec_sub = REC_NONE;
        cur_rec_role = -1;
        cur_rule = -1;
      } else if (tl >= 8 && bytes_eq_n(line, "regions", 7) && line[7] == ':') {
        section = SEC_REGIONS;
        reg_sub = REG_NONE;
        cur_region = -1;
      } else if (tl >= 7 && bytes_eq_n(line, "layout", 6) && line[6] == ':') {
        section = SEC_LAYOUT;
        cur_layout_role = -1;
      } else if (tl >= 8 && bytes_eq_n(line, "present", 7) && line[7] == ':') {
        section = SEC_PRESENT;
        pre_sub = PRE_NONE;
        pre_page_margins = 0;
        pre_master_sub = PRE_MASTER_NONE;
        cur_master_page = -1;
        cur_style = -1;
      } else {
        section = SEC_NONE;
      }
      continue;
    }

    if (section == SEC_REGIONS) {
      if (indent == 2 && tl > 1 && line[tl - 1] == ':') {
        doc_model_sv rg;
        rg.data = line;
        rg.len = tl - 1;
        if (!valid_role_name(rg.data, rg.len)) {
          set_parse_error(err, err_cap, line_no, "invalid region name");
          return -1;
        }
        cur_region = meltdown_add_region(out, rg);
        if (cur_region < 0) {
          set_parse_error(err, err_cap, line_no, "too many regions");
          return -1;
        }
        reg_sub = REG_NONE;
        continue;
      }
      if (cur_region < 0) {
        continue;
      }
      if (indent == 4) {
        if (tl == 6 && bytes_eq_n(line, "start:", 6)) {
          reg_sub = REG_START;
        }
        continue;
      }
      if (indent == 6 && tl >= 2 && line[0] == '-' && line[1] == ' ' && reg_sub == REG_START) {
        doc_model_sv rs;
        meltdown_region* r = &out->regions[cur_region];
        usize off = trim_left(line + 2, tl - 2);
        const char* expr = line + 2 + off;
        usize el = trim_right_len(expr, tl - 2 - off);
        i32 ridx;
        if (!valid_role_name(expr, el)) {
          set_parse_error(err, err_cap, line_no, "invalid role name in region start list");
          return -1;
        }
        rs.data = expr;
        rs.len = el;
        ridx = meltdown_find_role(out, rs);
        if (ridx < 0) {
          set_parse_error(err, err_cap, line_no, "unknown role in region start list");
          return -1;
        }
        if (r->start_role_count >= MELTDOWN_MAX_REGION_START_ROLES) {
          set_parse_error(err, err_cap, line_no, "too many start roles in region");
          return -1;
        }
        r->start_role_idxs[r->start_role_count++] = ridx;
      }
      continue;
    }

    if (section == SEC_PRESENT) {
      if (indent == 2) {
        usize k = 0;
        while (k < tl && line[k] != ':') {
          k++;
        }
        if (k == 4 && bytes_eq_n(line, "page", 4)) {
          pre_sub = PRE_PAGE;
          pre_page_margins = 0;
          continue;
        }
        if (k == 5 && bytes_eq_n(line, "fonts", 5)) {
          pre_sub = PRE_FONTS;
          continue;
        }
        if (k == 6 && bytes_eq_n(line, "master", 6)) {
          pre_sub = PRE_MASTER;
          pre_master_sub = PRE_MASTER_NONE;
          cur_master_page = -1;
          continue;
        }
        if (k == 6 && bytes_eq_n(line, "styles", 6)) {
          pre_sub = PRE_STYLES;
          cur_style = -1;
          continue;
        }
        continue;
      }

      if (pre_sub == PRE_PAGE) {
        if (indent == 4) {
          usize k = 0;
          const char* v;
          usize vl;
          while (k < tl && line[k] != ':') {
            k++;
          }
          if (k == tl) {
            continue;
          }
          v = line + k + 1;
          vl = trim_left(v, tl - (k + 1));
          v += vl;
          vl = trim_right_len(v, tl - (k + 1) - vl);

          if (k == 7 && bytes_eq_n(line, "margins", 7)) {
            pre_page_margins = 1;
            continue;
          }
          if (vl == 0) {
            continue;
          }
          if (k == 4 && bytes_eq_n(line, "size", 4)) {
            out->present.page.size.data = v;
            out->present.page.size.len = vl;
            continue;
          }
          if (k == 11 && bytes_eq_n(line, "orientation", 11)) {
            out->present.page.orientation.data = v;
            out->present.page.orientation.len = vl;
            continue;
          }
          continue;
        }
        if (indent == 6 && pre_page_margins) {
          usize k = 0;
          const char* v;
          usize vl;
          while (k < tl && line[k] != ':') {
            k++;
          }
          if (k == tl) {
            continue;
          }
          v = line + k + 1;
          vl = trim_left(v, tl - (k + 1));
          v += vl;
          vl = trim_right_len(v, tl - (k + 1) - vl);
          if (vl == 0) {
            continue;
          }
          if (k == 3 && bytes_eq_n(line, "top", 3)) {
            out->present.page.margin_top.data = v;
            out->present.page.margin_top.len = vl;
            continue;
          }
          if (k == 5 && bytes_eq_n(line, "right", 5)) {
            out->present.page.margin_right.data = v;
            out->present.page.margin_right.len = vl;
            continue;
          }
          if (k == 6 && bytes_eq_n(line, "bottom", 6)) {
            out->present.page.margin_bottom.data = v;
            out->present.page.margin_bottom.len = vl;
            continue;
          }
          if (k == 4 && bytes_eq_n(line, "left", 4)) {
            out->present.page.margin_left.data = v;
            out->present.page.margin_left.len = vl;
            continue;
          }
        }
        continue;
      }

      if (pre_sub == PRE_FONTS) {
        if (indent == 4) {
          usize k = 0;
          const char* v;
          usize vl;
          while (k < tl && line[k] != ':') {
            k++;
          }
          if (k == tl) {
            continue;
          }
          v = line + k + 1;
          vl = trim_left(v, tl - (k + 1));
          v += vl;
          vl = trim_right_len(v, tl - (k + 1) - vl);
          if (vl == 0) {
            continue;
          }
          if (k == 4 && bytes_eq_n(line, "body", 4)) {
            out->present.fonts.body.data = v;
            out->present.fonts.body.len = vl;
            continue;
          }
          if (k == 7 && bytes_eq_n(line, "heading", 7)) {
            out->present.fonts.heading.data = v;
            out->present.fonts.heading.len = vl;
            continue;
          }
          if (k == 4 && bytes_eq_n(line, "mono", 4)) {
            out->present.fonts.mono.data = v;
            out->present.fonts.mono.len = vl;
            continue;
          }
        }
        continue;
      }

      if (pre_sub == PRE_MASTER) {
        if (indent == 4 && tl > 1 && line[tl - 1] == ':') {
          doc_model_sv n;
          n.data = line;
          n.len = tl - 1;
          cur_master_page = meltdown_present_add_master_page(&out->present, n);
          pre_master_sub = PRE_MASTER_NONE;
          if (cur_master_page < 0) {
            set_parse_error(err, err_cap, line_no, "too many present.master pages");
            return -1;
          }
          continue;
        }
        if (indent == 6 && cur_master_page >= 0) {
          usize k = 0;
          while (k < tl && line[k] != ':') {
            k++;
          }
          if (k == 6 && bytes_eq_n(line, "header", 6)) {
            pre_master_sub = PRE_MASTER_HEADER;
            continue;
          }
          if (k == 6 && bytes_eq_n(line, "footer", 6)) {
            pre_master_sub = PRE_MASTER_FOOTER;
            continue;
          }
          continue;
        }
        if (indent == 8 && cur_master_page >= 0 && pre_master_sub != PRE_MASTER_NONE) {
          usize k = 0;
          const char* v;
          usize vl;
          meltdown_present_master_page* mp = &out->present.master_pages[cur_master_page];
          while (k < tl && line[k] != ':') {
            k++;
          }
          if (k == tl) {
            continue;
          }
          v = line + k + 1;
          vl = trim_left(v, tl - (k + 1));
          v += vl;
          vl = trim_right_len(v, tl - (k + 1) - vl);
          if (vl == 0) {
            continue;
          }
          if (k == 4 && bytes_eq_n(line, "left", 4)) {
            if (pre_master_sub == PRE_MASTER_HEADER) {
              mp->header_left.data = v;
              mp->header_left.len = vl;
            } else {
              mp->footer_left.data = v;
              mp->footer_left.len = vl;
            }
            continue;
          }
          if (k == 5 && bytes_eq_n(line, "right", 5)) {
            if (pre_master_sub == PRE_MASTER_HEADER) {
              mp->header_right.data = v;
              mp->header_right.len = vl;
            } else {
              mp->footer_right.data = v;
              mp->footer_right.len = vl;
            }
            continue;
          }
        }
        continue;
      }

      if (pre_sub == PRE_STYLES) {
        if (indent == 4 && tl > 1 && line[tl - 1] == ':') {
          doc_model_sv n;
          n.data = line;
          n.len = tl - 1;
          cur_style = meltdown_present_add_style(&out->present, n);
          if (cur_style < 0) {
            set_parse_error(err, err_cap, line_no, "too many present.styles entries");
            return -1;
          }
          continue;
        }
        if (indent == 6 && cur_style >= 0) {
          usize k = 0;
          const char* v;
          usize vl;
          meltdown_present_style_prop_kind pk;
          meltdown_present_style* st = &out->present.styles[cur_style];
          while (k < tl && line[k] != ':') {
            k++;
          }
          if (k == tl) {
            continue;
          }
          if (!meltdown_present_style_prop_kind_from_key(line, k, &pk)) {
            continue;
          }
          v = line + k + 1;
          vl = trim_left(v, tl - (k + 1));
          v += vl;
          vl = trim_right_len(v, tl - (k + 1) - vl);
          if (vl == 0) {
            continue;
          }
          if (st->prop_count >= MELTDOWN_MAX_STYLE_PROPS) {
            set_parse_error(err, err_cap, line_no, "too many properties in present.styles entry");
            return -1;
          }
          st->props[st->prop_count].kind = pk;
          st->props[st->prop_count].value.data = v;
          st->props[st->prop_count].value.len = vl;
          st->prop_count++;
        }
        continue;
      }

      continue;
    }

    if (section == SEC_LAYOUT) {
      if (indent == 2 && tl > 1 && line[tl - 1] == ':') {
        doc_model_sv rn;
        rn.data = line;
        rn.len = tl - 1;
        cur_layout_role = meltdown_find_role(out, rn);
        if (cur_layout_role < 0) {
          set_parse_error(err, err_cap, line_no, "layout role not declared in roles");
          return -1;
        }
        continue;
      }
      if (cur_layout_role < 0) {
        continue;
      }
      if (indent == 4) {
        usize k = 0;
        const char* v;
        usize vl;
        while (k < tl && line[k] != ':') {
          k++;
        }
        if (!(k == 5 && bytes_eq_n(line, "style", 5))) {
          continue;
        }
        v = line + k + 1;
        vl = trim_left(v, tl - (k + 1));
        v += vl;
        vl = trim_right_len(v, tl - (k + 1) - vl);
        if (vl == 0) {
          set_parse_error(err, err_cap, line_no, "layout style value is required");
          return -1;
        }
        out->role_styles[cur_layout_role].data = v;
        out->role_styles[cur_layout_role].len = vl;
        out->role_style_set[cur_layout_role] = 1;
      }
      continue;
    }

    if (section == SEC_ROLES) {
      if (indent == 2 && tl > 1 && line[tl - 1] == ':') {
        doc_model_sv rn;
        rn.data = line;
        rn.len = tl - 1;
        if (!valid_role_name(rn.data, rn.len) || meltdown_add_role(out, rn) < 0) {
          set_parse_error(err, err_cap, line_no, "failed to register role");
          return -1;
        }
      }
      continue;
    }

    if (section == SEC_RECOGNIZE) {
      if (indent == 2 && tl > 1 && line[tl - 1] == ':') {
        doc_model_sv rn;
        rn.data = line;
        rn.len = tl - 1;
        cur_rec_role = meltdown_find_role(out, rn);
        if (cur_rec_role < 0) {
          set_parse_error(err, err_cap, line_no, "recognize role not declared in roles");
          return -1;
        }
        if (out->rule_count >= MELTDOWN_MAX_RULES) {
          set_parse_error(err, err_cap, line_no, "too many recognize rules");
          return -1;
        }
        cur_rule = (i32)out->rule_count;
        out->rules[out->rule_count].role_idx = cur_rec_role;
        out->rules[out->rule_count].match_kind = 0;
        out->rules[out->rule_count].heading_level = 0;
        out->rules[out->rule_count].priority = 0;
        out->rules[out->rule_count].pred_count = 0;
        out->rule_count++;
        rec_sub = REC_NONE;
        continue;
      }
      if (cur_rule < 0) {
        continue;
      }
      if (indent == 4) {
        usize k = 0;
        while (k < tl && line[k] != ':') {
          k++;
        }
        if (k == 5 && bytes_eq_n(line, "match", 5)) {
          rec_sub = REC_MATCH;
          continue;
        }
        if (k == 5 && bytes_eq_n(line, "where", 5)) {
          rec_sub = REC_WHERE;
          continue;
        }
        if (k == 8 && bytes_eq_n(line, "priority", 8)) {
          const char* v = line + k + 1;
          usize vl = trim_left(v, tl - (k + 1));
          u64 pv = 0;
          v += vl;
          vl = trim_right_len(v, tl - (k + 1) - vl);
          if (!parse_u64_sv(v, vl, &pv)) {
            set_parse_error(err, err_cap, line_no, "invalid priority value");
            return -1;
          }
          out->rules[cur_rule].priority = pv;
          continue;
        }
        continue;
      }
      if (indent == 6 && tl >= 2 && line[0] == '-' && line[1] == ' ') {
        const char* expr;
        usize el;
        usize off = trim_left(line + 2, tl - 2);
        expr = line + 2 + off;
        el = trim_right_len(expr, tl - 2 - off);
        if (rec_sub == REC_MATCH) {
          u64 lv = 0;
          if (sv_eq_lit(expr, el, "paragraph")) {
            out->rules[cur_rule].match_kind = MELTDOWN_BLOCK_PARAGRAPH;
          } else if (sv_eq_lit(expr, el, "blockquote")) {
            out->rules[cur_rule].match_kind = MELTDOWN_BLOCK_BLOCKQUOTE;
          } else if (sv_eq_lit(expr, el, "list")) {
            out->rules[cur_rule].match_kind = MELTDOWN_BLOCK_LIST;
          } else if (sv_eq_lit(expr, el, "table")) {
            out->rules[cur_rule].match_kind = MELTDOWN_BLOCK_TABLE;
          } else if (sv_eq_lit(expr, el, "codeblock")) {
            out->rules[cur_rule].match_kind = MELTDOWN_BLOCK_CODEBLOCK;
          } else if (el > 15 && cstr_starts_with(expr, "heading(level=") && expr[el - 1] == ')' &&
                     parse_u64_sv(expr + 14, el - 15, &lv) && lv >= 1 && lv <= 6) {
            out->rules[cur_rule].match_kind = MELTDOWN_BLOCK_HEADING;
            out->rules[cur_rule].heading_level = (u8)lv;
          }
          continue;
        }
        if (rec_sub == REC_WHERE) {
          meltdown_rule* r = &out->rules[cur_rule];
          meltdown_predicate* p;
          if (r->pred_count >= MELTDOWN_MAX_RULE_PREDS) {
            set_parse_error(err, err_cap, line_no, "too many where predicates in rule");
            return -1;
          }
          p = &r->preds[r->pred_count];
          p->role_idx = -1;
          p->region_idx = -1;
          if (sv_eq_lit(expr, el, "position == first")) {
            p->kind = MELT_PRED_POSITION_FIRST;
          } else if (sv_eq_lit(expr, el, "position == last")) {
            p->kind = MELT_PRED_POSITION_LAST;
          } else if (el > 7 && cstr_starts_with(expr, "before ")) {
            doc_model_sv rs;
            p->kind = MELT_PRED_BEFORE_ROLE;
            if (!parse_role_ref_sv(expr, el, 7, &rs)) {
              set_parse_error(err, err_cap, line_no, "invalid role reference in before predicate");
              return -1;
            }
            p->role_idx = meltdown_find_role(out, rs);
            if (p->role_idx < 0) {
              set_parse_error(err, err_cap, line_no, "unknown role in before predicate");
              return -1;
            }
          } else if (el > 6 && cstr_starts_with(expr, "after ")) {
            doc_model_sv rs;
            p->kind = MELT_PRED_AFTER_ROLE;
            if (!parse_role_ref_sv(expr, el, 6, &rs)) {
              set_parse_error(err, err_cap, line_no, "invalid role reference in after predicate");
              return -1;
            }
            p->role_idx = meltdown_find_role(out, rs);
            if (p->role_idx < 0) {
              set_parse_error(err, err_cap, line_no, "unknown role in after predicate");
              return -1;
            }
          } else if (el > 8 && cstr_starts_with(expr, "follows ")) {
            doc_model_sv rs;
            p->kind = MELT_PRED_FOLLOWS_ROLE;
            if (!parse_role_ref_sv(expr, el, 8, &rs)) {
              set_parse_error(err, err_cap, line_no, "invalid role reference in follows predicate");
              return -1;
            }
            p->role_idx = meltdown_find_role(out, rs);
            if (p->role_idx < 0) {
              set_parse_error(err, err_cap, line_no, "unknown role in follows predicate");
              return -1;
            }
          } else if (el > 9 && cstr_starts_with(expr, "precedes ")) {
            doc_model_sv rs;
            p->kind = MELT_PRED_PRECEDES_ROLE;
            if (!parse_role_ref_sv(expr, el, 9, &rs)) {
              set_parse_error(err, err_cap, line_no, "invalid role reference in precedes predicate");
              return -1;
            }
            p->role_idx = meltdown_find_role(out, rs);
            if (p->role_idx < 0) {
              set_parse_error(err, err_cap, line_no, "unknown role in precedes predicate");
              return -1;
            }
          } else if (el > 3 && cstr_starts_with(expr, "in ")) {
            doc_model_sv rg;
            p->kind = MELT_PRED_IN_REGION;
            if (!parse_role_ref_sv(expr, el, 3, &rg)) {
              set_parse_error(err, err_cap, line_no, "invalid region reference in in predicate");
              return -1;
            }
            p->region_idx = meltdown_find_region(out, rg);
            if (p->region_idx < 0) {
              set_parse_error(err, err_cap, line_no, "unknown region in in predicate");
              return -1;
            }
          } else if (cstr_starts_with(expr, "startswith ") || cstr_starts_with(expr, "equals ") ||
                     cstr_starts_with(expr, "endswith ") || cstr_starts_with(expr, "regex ") ||
                     cstr_starts_with(expr, "regex: ") || sv_eq_lit(expr, el, "uppercase == true") ||
                     sv_eq_lit(expr, el, "uppercase == false")) {
            p->kind = MELT_PRED_UNSUPPORTED_TEXT;
          } else {
            continue;
          }
          r->pred_count++;
        }
      }
    }
  }

  return 0;
}

static int meltdown_rule_matches(const meltdown_rule* r,
                                 const meltdown_block_stream* stream,
                                 usize block_idx,
                                 const u8* seen_roles,
                                 const u8* active_regions,
                                 const i32* assigned_roles) {
  const meltdown_block_entry* b = &stream->blocks[block_idx];
  usize i;
  if (r->match_kind == 0 || b->kind != r->match_kind) {
    return 0;
  }
  if (r->match_kind == MELTDOWN_BLOCK_HEADING && r->heading_level != 0 && b->heading_level != r->heading_level) {
    return 0;
  }
  for (i = 0; i < r->pred_count; i++) {
    const meltdown_predicate* p = &r->preds[i];
    if (p->kind == MELT_PRED_POSITION_FIRST) {
      if (block_idx != 0) {
        return 0;
      }
    } else if (p->kind == MELT_PRED_POSITION_LAST) {
      if (block_idx + 1 != stream->count) {
        return 0;
      }
    } else if (p->kind == MELT_PRED_AFTER_ROLE) {
      if (p->role_idx < 0 || !seen_roles[p->role_idx]) {
        return 0;
      }
    } else if (p->kind == MELT_PRED_BEFORE_ROLE) {
      if (p->role_idx < 0 || seen_roles[p->role_idx]) {
        return 0;
      }
    } else if (p->kind == MELT_PRED_FOLLOWS_ROLE) {
      if (block_idx == 0 || p->role_idx < 0 || assigned_roles[block_idx - 1] != p->role_idx) {
        return 0;
      }
    } else if (p->kind == MELT_PRED_PRECEDES_ROLE) {
      if (p->role_idx < 0 || seen_roles[p->role_idx]) {
        return 0;
      }
    } else if (p->kind == MELT_PRED_IN_REGION) {
      if (p->region_idx < 0 || !active_regions[p->region_idx]) {
        return 0;
      }
    } else {
      return 0;
    }
  }
  return 1;
}

static int meltdown_classify_stream(const meltdown_profile_runtime* profile,
                                    const meltdown_block_stream* stream,
                                    i32* out_roles,
                                    usize out_roles_cap,
                                    usize* out_assigned_non_body) {
  u8 seen_roles[MELTDOWN_MAX_ROLES];
  u8 active_regions[MELTDOWN_MAX_REGIONS];
  usize i;
  usize assigned = 0;
  for (i = 0; i < profile->role_count && i < MELTDOWN_MAX_ROLES; i++) {
    seen_roles[i] = 0;
  }
  for (i = 0; i < profile->region_count && i < MELTDOWN_MAX_REGIONS; i++) {
    active_regions[i] = 0;
  }
  if (out_roles_cap < stream->count) {
    return CONVERT_ERR_LIMIT;
  }
  for (i = 0; i < stream->count; i++) {
    usize r;
    i32 best_role = -1;
    u64 best_prio = 0;
    int have = 0;
    for (r = 0; r < profile->rule_count; r++) {
      const meltdown_rule* rule = &profile->rules[r];
      if (!meltdown_rule_matches(rule, stream, i, seen_roles, active_regions, out_roles)) {
        continue;
      }
      if (!have || rule->priority > best_prio) {
        have = 1;
        best_prio = rule->priority;
        best_role = rule->role_idx;
      }
    }
    out_roles[i] = best_role;
    if (best_role >= 0) {
      usize rg;
      seen_roles[best_role] = 1;
      for (rg = 0; rg < profile->region_count; rg++) {
        usize sr;
        const meltdown_region* region = &profile->regions[rg];
        for (sr = 0; sr < region->start_role_count; sr++) {
          if (region->start_role_idxs[sr] == best_role) {
            active_regions[rg] = 1;
            break;
          }
        }
      }
      assigned++;
    }
  }
  *out_assigned_non_body = assigned;
  return CONVERT_OK;
}

static usize meltdown_count_unsupported_predicates(const meltdown_profile_runtime* profile) {
  usize i;
  usize j;
  usize count = 0;
  for (i = 0; i < profile->rule_count; i++) {
    const meltdown_rule* r = &profile->rules[i];
    for (j = 0; j < r->pred_count; j++) {
      if (r->preds[j].kind == MELT_PRED_UNSUPPORTED_TEXT) {
        count++;
      }
    }
  }
  return count;
}

static int bytes_find_substr(const u8* hay, usize hay_len, const char* needle, usize* out_pos) {
  usize i;
  usize nl = rt_strlen(needle);
  if (nl == 0 || hay_len < nl) {
    return 0;
  }
  for (i = 0; i + nl <= hay_len; i++) {
    usize j;
    int match = 1;
    for (j = 0; j < nl; j++) {
      if (hay[i + j] != (u8)needle[j]) {
        match = 0;
        break;
      }
    }
    if (match) {
      *out_pos = i;
      return 1;
    }
  }
  return 0;
}

static int append_bytes(u8* dst, usize cap, usize* pos, const u8* src, usize n) {
  usize i;
  if (*pos + n > cap) {
    return CONVERT_ERR_LIMIT;
  }
  for (i = 0; i < n; i++) {
    dst[*pos + i] = src[i];
  }
  *pos += n;
  return CONVERT_OK;
}

static int append_cstr_bytes(u8* dst, usize cap, usize* pos, const char* s) {
  return append_bytes(dst, cap, pos, (const u8*)s, rt_strlen(s));
}

static int append_sv_escaped_xml(u8* dst, usize cap, usize* pos, doc_model_sv sv) {
  usize i;
  for (i = 0; i < sv.len; i++) {
    char c = sv.data[i];
    if (c == '&') {
      if (append_cstr_bytes(dst, cap, pos, "&amp;") != CONVERT_OK) {
        return CONVERT_ERR_LIMIT;
      }
    } else if (c == '"') {
      if (append_cstr_bytes(dst, cap, pos, "&quot;") != CONVERT_OK) {
        return CONVERT_ERR_LIMIT;
      }
    } else if (c == '<') {
      if (append_cstr_bytes(dst, cap, pos, "&lt;") != CONVERT_OK) {
        return CONVERT_ERR_LIMIT;
      }
    } else if (c == '>') {
      if (append_cstr_bytes(dst, cap, pos, "&gt;") != CONVERT_OK) {
        return CONVERT_ERR_LIMIT;
      }
    } else {
      if (append_bytes(dst, cap, pos, (const u8*)&c, 1) != CONVERT_OK) {
        return CONVERT_ERR_LIMIT;
      }
    }
  }
  return CONVERT_OK;
}

static int patch_attr_value(const u8* in,
                            usize in_len,
                            const char* attr_prefix,
                            doc_model_sv value,
                            u8* out,
                            usize out_cap,
                            usize* out_len) {
  usize pos;
  usize pfx_len = rt_strlen(attr_prefix);
  usize end;
  usize w = 0;
  if (value.len == 0) {
    return CONVERT_ERR_INVALID;
  }
  if (!bytes_find_substr(in, in_len, attr_prefix, &pos)) {
    return CONVERT_ERR_NOT_FOUND;
  }
  end = pos + pfx_len;
  while (end < in_len && in[end] != (u8)'"') {
    end++;
  }
  if (end >= in_len) {
    return CONVERT_ERR_INVALID;
  }
  if (append_bytes(out, out_cap, &w, in, pos + pfx_len) != CONVERT_OK) {
    return CONVERT_ERR_LIMIT;
  }
  if (append_sv_escaped_xml(out, out_cap, &w, value) != CONVERT_OK) {
    return CONVERT_ERR_LIMIT;
  }
  if (append_bytes(out, out_cap, &w, in + end, in_len - end) != CONVERT_OK) {
    return CONVERT_ERR_LIMIT;
  }
  *out_len = w;
  return CONVERT_OK;
}

static int render_present_style_entry(const meltdown_present_style* st,
                                      u8* dst,
                                      usize cap,
                                      usize* pos) {
  usize i;
  doc_model_sv based_on = {0, 0};
  doc_model_sv family = {0, 0};
  doc_model_sv align = {0, 0};
  doc_model_sv margin_top = {0, 0};
  doc_model_sv margin_bottom = {0, 0};
  doc_model_sv margin_left = {0, 0};
  doc_model_sv margin_right = {0, 0};
  doc_model_sv text_indent = {0, 0};
  doc_model_sv line_height = {0, 0};
  doc_model_sv font_size = {0, 0};
  doc_model_sv font_weight = {0, 0};
  doc_model_sv font_style = {0, 0};

  for (i = 0; i < st->prop_count; i++) {
    const meltdown_present_style_prop* p = &st->props[i];
    if (p->kind == MELT_STYLE_PROP_BASED_ON) {
      based_on = p->value;
    } else if (p->kind == MELT_STYLE_PROP_FAMILY) {
      family = p->value;
    } else if (p->kind == MELT_STYLE_PROP_ALIGN) {
      align = p->value;
    } else if (p->kind == MELT_STYLE_PROP_MARGIN_TOP) {
      margin_top = p->value;
    } else if (p->kind == MELT_STYLE_PROP_MARGIN_BOTTOM) {
      margin_bottom = p->value;
    } else if (p->kind == MELT_STYLE_PROP_MARGIN_LEFT) {
      margin_left = p->value;
    } else if (p->kind == MELT_STYLE_PROP_MARGIN_RIGHT) {
      margin_right = p->value;
    } else if (p->kind == MELT_STYLE_PROP_TEXT_INDENT) {
      text_indent = p->value;
    } else if (p->kind == MELT_STYLE_PROP_LINE_HEIGHT) {
      line_height = p->value;
    } else if (p->kind == MELT_STYLE_PROP_FONT_SIZE) {
      font_size = p->value;
    } else if (p->kind == MELT_STYLE_PROP_FONT_WEIGHT) {
      font_weight = p->value;
    } else if (p->kind == MELT_STYLE_PROP_FONT_STYLE) {
      font_style = p->value;
    }
  }

  if (append_cstr_bytes(dst, cap, pos, "<style:style style:name=\"") != CONVERT_OK ||
      append_sv_escaped_xml(dst, cap, pos, st->name) != CONVERT_OK ||
      append_cstr_bytes(dst, cap, pos, "\" style:family=\"") != CONVERT_OK ||
      append_sv_escaped_xml(dst, cap, pos, (family.len ? family : (doc_model_sv){"paragraph", 9})) !=
          CONVERT_OK) {
    return CONVERT_ERR_LIMIT;
  }
  if (based_on.len != 0) {
    if (append_cstr_bytes(dst, cap, pos, "\" style:parent-style-name=\"") != CONVERT_OK ||
        append_sv_escaped_xml(dst, cap, pos, based_on) != CONVERT_OK) {
      return CONVERT_ERR_LIMIT;
    }
  }
  if (append_cstr_bytes(dst, cap, pos, "\">") != CONVERT_OK) {
    return CONVERT_ERR_LIMIT;
  }

  if (align.len || margin_top.len || margin_bottom.len || margin_left.len || margin_right.len ||
      text_indent.len || line_height.len) {
    if (append_cstr_bytes(dst, cap, pos, "<style:paragraph-properties") != CONVERT_OK) {
      return CONVERT_ERR_LIMIT;
    }
    if (align.len && (append_cstr_bytes(dst, cap, pos, " fo:text-align=\"") != CONVERT_OK ||
                      append_sv_escaped_xml(dst, cap, pos, align) != CONVERT_OK ||
                      append_cstr_bytes(dst, cap, pos, "\"") != CONVERT_OK)) {
      return CONVERT_ERR_LIMIT;
    }
    if (margin_top.len && (append_cstr_bytes(dst, cap, pos, " fo:margin-top=\"") != CONVERT_OK ||
                           append_sv_escaped_xml(dst, cap, pos, margin_top) != CONVERT_OK ||
                           append_cstr_bytes(dst, cap, pos, "\"") != CONVERT_OK)) {
      return CONVERT_ERR_LIMIT;
    }
    if (margin_bottom.len && (append_cstr_bytes(dst, cap, pos, " fo:margin-bottom=\"") != CONVERT_OK ||
                              append_sv_escaped_xml(dst, cap, pos, margin_bottom) != CONVERT_OK ||
                              append_cstr_bytes(dst, cap, pos, "\"") != CONVERT_OK)) {
      return CONVERT_ERR_LIMIT;
    }
    if (margin_left.len && (append_cstr_bytes(dst, cap, pos, " fo:margin-left=\"") != CONVERT_OK ||
                            append_sv_escaped_xml(dst, cap, pos, margin_left) != CONVERT_OK ||
                            append_cstr_bytes(dst, cap, pos, "\"") != CONVERT_OK)) {
      return CONVERT_ERR_LIMIT;
    }
    if (margin_right.len && (append_cstr_bytes(dst, cap, pos, " fo:margin-right=\"") != CONVERT_OK ||
                             append_sv_escaped_xml(dst, cap, pos, margin_right) != CONVERT_OK ||
                             append_cstr_bytes(dst, cap, pos, "\"") != CONVERT_OK)) {
      return CONVERT_ERR_LIMIT;
    }
    if (text_indent.len && (append_cstr_bytes(dst, cap, pos, " fo:text-indent=\"") != CONVERT_OK ||
                            append_sv_escaped_xml(dst, cap, pos, text_indent) != CONVERT_OK ||
                            append_cstr_bytes(dst, cap, pos, "\"") != CONVERT_OK)) {
      return CONVERT_ERR_LIMIT;
    }
    if (line_height.len && (append_cstr_bytes(dst, cap, pos, " fo:line-height=\"") != CONVERT_OK ||
                            append_sv_escaped_xml(dst, cap, pos, line_height) != CONVERT_OK ||
                            append_cstr_bytes(dst, cap, pos, "\"") != CONVERT_OK)) {
      return CONVERT_ERR_LIMIT;
    }
    if (append_cstr_bytes(dst, cap, pos, "/>") != CONVERT_OK) {
      return CONVERT_ERR_LIMIT;
    }
  }

  if (font_size.len || font_weight.len || font_style.len) {
    if (append_cstr_bytes(dst, cap, pos, "<style:text-properties") != CONVERT_OK) {
      return CONVERT_ERR_LIMIT;
    }
    if (font_size.len && (append_cstr_bytes(dst, cap, pos, " fo:font-size=\"") != CONVERT_OK ||
                          append_sv_escaped_xml(dst, cap, pos, font_size) != CONVERT_OK ||
                          append_cstr_bytes(dst, cap, pos, "\"") != CONVERT_OK)) {
      return CONVERT_ERR_LIMIT;
    }
    if (font_weight.len && (append_cstr_bytes(dst, cap, pos, " fo:font-weight=\"") != CONVERT_OK ||
                            append_sv_escaped_xml(dst, cap, pos, font_weight) != CONVERT_OK ||
                            append_cstr_bytes(dst, cap, pos, "\"") != CONVERT_OK)) {
      return CONVERT_ERR_LIMIT;
    }
    if (font_style.len && (append_cstr_bytes(dst, cap, pos, " fo:font-style=\"") != CONVERT_OK ||
                           append_sv_escaped_xml(dst, cap, pos, font_style) != CONVERT_OK ||
                           append_cstr_bytes(dst, cap, pos, "\"") != CONVERT_OK)) {
      return CONVERT_ERR_LIMIT;
    }
    if (append_cstr_bytes(dst, cap, pos, "/>") != CONVERT_OK) {
      return CONVERT_ERR_LIMIT;
    }
  }

  if (append_cstr_bytes(dst, cap, pos, "</style:style>") != CONVERT_OK) {
    return CONVERT_ERR_LIMIT;
  }
  return CONVERT_OK;
}

static int materialize_present_into_styles_xml(const meltdown_present_model* pm,
                                               const u8* src,
                                               usize src_len,
                                               u8* dst,
                                               usize dst_cap,
                                               usize* dst_len,
                                               usize* out_changes) {
  static u8 tmp_a[CLI_MAX_FILE_BYTES];
  static u8 tmp_b[CLI_MAX_FILE_BYTES];
  static u8 tmp_c[CLI_MAX_FILE_BYTES];
  const u8* cur = src;
  usize cur_len = src_len;
  usize changes = 0;
  int patched = 0;

  if (pm->page.orientation.len != 0) {
    if (patch_attr_value(cur,
                         cur_len,
                         "style:print-orientation=\"",
                         pm->page.orientation,
                         tmp_a,
                         sizeof(tmp_a),
                         &cur_len) == CONVERT_OK) {
      cur = tmp_a;
      changes++;
      patched = 1;
    }
  }
  if (pm->page.margin_top.len != 0) {
    if (patch_attr_value(cur, cur_len, "fo:margin-top=\"", pm->page.margin_top, tmp_b, sizeof(tmp_b), &cur_len) ==
        CONVERT_OK) {
      cur = tmp_b;
      changes++;
      patched = 1;
    }
  }
  if (pm->page.margin_right.len != 0) {
    if (patch_attr_value(cur,
                         cur_len,
                         "fo:margin-right=\"",
                         pm->page.margin_right,
                         tmp_a,
                         sizeof(tmp_a),
                         &cur_len) == CONVERT_OK) {
      cur = tmp_a;
      changes++;
      patched = 1;
    }
  }
  if (pm->page.margin_bottom.len != 0) {
    if (patch_attr_value(cur,
                         cur_len,
                         "fo:margin-bottom=\"",
                         pm->page.margin_bottom,
                         tmp_b,
                         sizeof(tmp_b),
                         &cur_len) == CONVERT_OK) {
      cur = tmp_b;
      changes++;
      patched = 1;
    }
  }
  if (pm->page.margin_left.len != 0) {
    if (patch_attr_value(cur, cur_len, "fo:margin-left=\"", pm->page.margin_left, tmp_a, sizeof(tmp_a), &cur_len) ==
        CONVERT_OK) {
      cur = tmp_a;
      changes++;
      patched = 1;
    }
  }

  {
    usize insert_pos = 0;
    if (pm->style_count > 0 && bytes_find_substr(cur, cur_len, "</office:styles>", &insert_pos)) {
      usize i;
      usize w = 0;
      if (append_bytes(tmp_c, sizeof(tmp_c), &w, cur, insert_pos) != CONVERT_OK) {
        return CONVERT_ERR_LIMIT;
      }
      for (i = 0; i < pm->style_count; i++) {
        if (render_present_style_entry(&pm->styles[i], tmp_c, sizeof(tmp_c), &w) != CONVERT_OK) {
          return CONVERT_ERR_LIMIT;
        }
      }
      if (append_bytes(tmp_c, sizeof(tmp_c), &w, cur + insert_pos, cur_len - insert_pos) != CONVERT_OK) {
        return CONVERT_ERR_LIMIT;
      }
      cur = tmp_c;
      cur_len = w;
      changes += pm->style_count;
      patched = 1;
    }
  }

  {
    int need_page = (pm->page.orientation.len != 0 || pm->page.margin_top.len != 0 ||
                     pm->page.margin_right.len != 0 || pm->page.margin_bottom.len != 0 ||
                     pm->page.margin_left.len != 0);
    usize pos_doc_end = 0;
    if (need_page && !bytes_find_substr(cur, cur_len, "style:print-orientation=\"", &pos_doc_end) &&
        bytes_find_substr(cur, cur_len, "</office:document-styles>", &pos_doc_end)) {
      usize w = 0;
      if (append_bytes(tmp_a, sizeof(tmp_a), &w, cur, pos_doc_end) != CONVERT_OK ||
          append_cstr_bytes(tmp_a,
                            sizeof(tmp_a),
                            &w,
                            "<office:automatic-styles><style:page-layout style:name=\"Mpm1\"><style:page-layout-properties") !=
              CONVERT_OK) {
        return CONVERT_ERR_LIMIT;
      }
      if (pm->page.orientation.len != 0) {
        if (append_cstr_bytes(tmp_a, sizeof(tmp_a), &w, " style:print-orientation=\"") != CONVERT_OK ||
            append_sv_escaped_xml(tmp_a, sizeof(tmp_a), &w, pm->page.orientation) != CONVERT_OK ||
            append_cstr_bytes(tmp_a, sizeof(tmp_a), &w, "\"") != CONVERT_OK) {
          return CONVERT_ERR_LIMIT;
        }
      }
      if (pm->page.margin_top.len != 0) {
        if (append_cstr_bytes(tmp_a, sizeof(tmp_a), &w, " fo:margin-top=\"") != CONVERT_OK ||
            append_sv_escaped_xml(tmp_a, sizeof(tmp_a), &w, pm->page.margin_top) != CONVERT_OK ||
            append_cstr_bytes(tmp_a, sizeof(tmp_a), &w, "\"") != CONVERT_OK) {
          return CONVERT_ERR_LIMIT;
        }
      }
      if (pm->page.margin_right.len != 0) {
        if (append_cstr_bytes(tmp_a, sizeof(tmp_a), &w, " fo:margin-right=\"") != CONVERT_OK ||
            append_sv_escaped_xml(tmp_a, sizeof(tmp_a), &w, pm->page.margin_right) != CONVERT_OK ||
            append_cstr_bytes(tmp_a, sizeof(tmp_a), &w, "\"") != CONVERT_OK) {
          return CONVERT_ERR_LIMIT;
        }
      }
      if (pm->page.margin_bottom.len != 0) {
        if (append_cstr_bytes(tmp_a, sizeof(tmp_a), &w, " fo:margin-bottom=\"") != CONVERT_OK ||
            append_sv_escaped_xml(tmp_a, sizeof(tmp_a), &w, pm->page.margin_bottom) != CONVERT_OK ||
            append_cstr_bytes(tmp_a, sizeof(tmp_a), &w, "\"") != CONVERT_OK) {
          return CONVERT_ERR_LIMIT;
        }
      }
      if (pm->page.margin_left.len != 0) {
        if (append_cstr_bytes(tmp_a, sizeof(tmp_a), &w, " fo:margin-left=\"") != CONVERT_OK ||
            append_sv_escaped_xml(tmp_a, sizeof(tmp_a), &w, pm->page.margin_left) != CONVERT_OK ||
            append_cstr_bytes(tmp_a, sizeof(tmp_a), &w, "\"") != CONVERT_OK) {
          return CONVERT_ERR_LIMIT;
        }
      }
      if (append_cstr_bytes(tmp_a,
                            sizeof(tmp_a),
                            &w,
                            "/></style:page-layout-properties></style:page-layout></office:automatic-styles>") !=
              CONVERT_OK ||
          append_bytes(tmp_a, sizeof(tmp_a), &w, cur + pos_doc_end, cur_len - pos_doc_end) != CONVERT_OK) {
        return CONVERT_ERR_LIMIT;
      }
      cur = tmp_a;
      cur_len = w;
      changes++;
      patched = 1;
    }
  }

  if (!patched) {
    *dst_len = 0;
    if (append_bytes(dst, dst_cap, dst_len, src, src_len) != CONVERT_OK) {
      return CONVERT_ERR_LIMIT;
    }
    *out_changes = 0;
    return CONVERT_OK;
  }

  *dst_len = 0;
  if (append_bytes(dst, dst_cap, dst_len, cur, cur_len) != CONVERT_OK) {
    return CONVERT_ERR_LIMIT;
  }
  *out_changes = changes;
  return CONVERT_OK;
}

static int meltdown_materialize_present_on_odt(const meltdown_present_model* pm,
                                               u8* odt_buf,
                                               usize odt_len,
                                               usize odt_cap,
                                               usize* out_len,
                                               usize* out_changes) {
  static u8 out_buf[CLI_MAX_FILE_BYTES];
  static u8 styles_xml[CLI_MAX_FILE_BYTES / 2];
  static u8 patched_styles[CLI_MAX_FILE_BYTES / 2];
  static zip_writer zw;
  zip_archive za;
  u16 entry_count = 0;
  u16 i;
  usize styles_len = 0;
  usize patched_len = 0;
  usize changes = 0;

  if (odt_cap > sizeof(out_buf)) {
    return CONVERT_ERR_LIMIT;
  }
  if (zip_archive_open(&za, odt_buf, odt_len) != ZIP_OK) {
    return CONVERT_ERR_INVALID;
  }
  if (zip_archive_entry_count(&za, &entry_count) != ZIP_OK) {
    return CONVERT_ERR_INVALID;
  }

  zip_writer_init(&zw, out_buf, odt_cap);
  for (i = 0; i < entry_count; i++) {
    zip_entry_view ze;
    char name_buf[ZIP_WRITER_MAX_NAME + 1];
    usize j;
    if (zip_archive_get_entry(&za, i, &ze) != ZIP_OK) {
      return CONVERT_ERR_INVALID;
    }
    if (ze.name_len > ZIP_WRITER_MAX_NAME) {
      return CONVERT_ERR_LIMIT;
    }
    for (j = 0; j < ze.name_len; j++) {
      name_buf[j] = ze.name[j];
    }
    name_buf[ze.name_len] = '\0';

    if (cstr_eq(name_buf, "styles.xml")) {
      if (zip_entry_extract(&ze, styles_xml, sizeof(styles_xml), &styles_len) != ZIP_OK) {
        return CONVERT_ERR_INVALID;
      }
      if (materialize_present_into_styles_xml(pm,
                                              styles_xml,
                                              styles_len,
                                              patched_styles,
                                              sizeof(patched_styles),
                                              &patched_len,
                                              &changes) != CONVERT_OK) {
        return CONVERT_ERR_INVALID;
      }
      if (zip_writer_add_entry(&zw,
                               "styles.xml",
                               patched_styles,
                               patched_len,
                               ZIP_METHOD_DEFLATE) != ZIP_OK) {
        return CONVERT_ERR_INVALID;
      }
      continue;
    }

    if (zip_writer_add_raw_entry(&zw,
                                 name_buf,
                                 ze.method,
                                 ze.crc32,
                                 ze.comp_size,
                                 ze.uncomp_size,
                                 ze.archive_data + ze.data_pos,
                                 (usize)ze.comp_size) != ZIP_OK) {
      return CONVERT_ERR_INVALID;
    }
  }

  if (zip_writer_finish(&zw, out_len) != ZIP_OK) {
    return CONVERT_ERR_INVALID;
  }
  if (*out_len > odt_cap) {
    return CONVERT_ERR_LIMIT;
  }
  {
    usize k;
    for (k = 0; k < *out_len; k++) {
      odt_buf[k] = out_buf[k];
    }
  }
  *out_changes = changes;
  return CONVERT_OK;
}

static void meltdown_apply_layout_styles(doc_model_document* doc,
                                         const meltdown_profile_runtime* profile,
                                         const i32* role_assignments,
                                         usize role_count,
                                         usize* out_applied) {
  doc_model_block* blocks = (doc_model_block*)doc->blocks.items;
  usize i;
  usize si = 0;
  usize applied = 0;
  for (i = 0; i < doc->blocks.count && si < role_count; i++) {
    doc_model_block* b = &blocks[i];
    int streamable = (b->kind == DOC_MODEL_BLOCK_HEADING || b->kind == DOC_MODEL_BLOCK_PARAGRAPH ||
                      b->kind == DOC_MODEL_BLOCK_QUOTE || b->kind == DOC_MODEL_BLOCK_LIST ||
                      b->kind == DOC_MODEL_BLOCK_TABLE || b->kind == DOC_MODEL_BLOCK_CODE_BLOCK);
    if (!streamable) {
      continue;
    }
    if (role_assignments[si] >= 0 && (usize)role_assignments[si] < profile->role_count &&
        profile->role_style_set[role_assignments[si]]) {
      b->style_id = profile->role_styles[role_assignments[si]];
      applied++;
    }
    si++;
  }
  *out_applied = applied;
}

static int meltdown_stream_push(meltdown_block_stream* s, meltdown_block_kind kind, u8 heading_level) {
  if (s->count >= MELTDOWN_MAX_STREAM_BLOCKS) {
    s->dropped++;
    return CONVERT_ERR_LIMIT;
  }
  s->blocks[s->count].kind = kind;
  s->blocks[s->count].heading_level = heading_level;
  s->count++;
  return CONVERT_OK;
}

static int meltdown_build_block_stream(const doc_model_document* doc, meltdown_block_stream* out) {
  usize i;
  if (doc == 0 || out == 0) {
    return CONVERT_ERR_INVALID;
  }
  out->count = 0;
  out->dropped = 0;
  for (i = 0; i < doc->blocks.count; i++) {
    const doc_model_block* b = &doc->blocks.items[i];
    if (b->kind == DOC_MODEL_BLOCK_HEADING) {
      (void)meltdown_stream_push(out, MELTDOWN_BLOCK_HEADING, b->as.heading.level);
    } else if (b->kind == DOC_MODEL_BLOCK_PARAGRAPH) {
      (void)meltdown_stream_push(out, MELTDOWN_BLOCK_PARAGRAPH, 0);
    } else if (b->kind == DOC_MODEL_BLOCK_QUOTE) {
      (void)meltdown_stream_push(out, MELTDOWN_BLOCK_BLOCKQUOTE, 0);
    } else if (b->kind == DOC_MODEL_BLOCK_LIST) {
      (void)meltdown_stream_push(out, MELTDOWN_BLOCK_LIST, 0);
    } else if (b->kind == DOC_MODEL_BLOCK_TABLE) {
      (void)meltdown_stream_push(out, MELTDOWN_BLOCK_TABLE, 0);
    } else if (b->kind == DOC_MODEL_BLOCK_CODE_BLOCK) {
      (void)meltdown_stream_push(out, MELTDOWN_BLOCK_CODEBLOCK, 0);
    }
  }
  return out->dropped == 0 ? CONVERT_OK : CONVERT_ERR_LIMIT;
}

static int read_file_all(const char* path, u8* dst, usize cap, usize* out_len) {
  long fd = rt_openat(-100, path, RT_O_RDONLY, 0);
  usize off = 0;
  if (fd < 0) {
    return -1;
  }
  for (;;) {
    long n = rt_read((int)fd, dst + off, cap - off);
    if (n < 0) {
      rt_close((int)fd);
      return -2;
    }
    if (n == 0) {
      break;
    }
    off += (usize)n;
    if (off == cap) {
      rt_close((int)fd);
      return -3;
    }
  }
  rt_close((int)fd);
  *out_len = off;
  return 0;
}

static int write_file_all(const char* path, const u8* src, usize n) {
  long fd = rt_openat(-100, path, RT_O_WRONLY | RT_O_CREAT | RT_O_TRUNC, 0644);
  if (fd < 0) {
    return -1;
  }
  if (rt_write_all((int)fd, src, n) != (long)n) {
    rt_close((int)fd);
    return -2;
  }
  rt_close((int)fd);
  return 0;
}

static void print_usage(void) {
  write_str("usage:\n");
  write_str("  odt_cli validate <in.odt>\n");
  write_str("  odt_cli inspect <in.odt>\n");
  write_str("  odt_cli extract-text <in.odt> [out.txt]\n");
  write_str("  odt_cli create <in.txt> <out.odt>\n");
  write_str("  odt_cli repack <in.odt> <out.odt>\n");
  write_str("  odt_cli convert --from <fmt> --to <fmt> <in> <out>\n");
  write_str("  odt_cli convert --from <fmt> --to odt --template <template.odt> <in> <out>\n");
  write_str(
      "  odt_cli convert --from <fmt> --to <fmt> [--template <template.odt|profile.meltdown>] [--diag-json] [--strict] <in> <out>\n");
}

static const char* diag_severity_name(convert_diag_severity s) {
  if (s == CONVERT_DIAG_ERROR) {
    return "error";
  }
  if (s == CONVERT_DIAG_WARN) {
    return "warn";
  }
  return "info";
}

static void print_json_string_err(const char* s) {
  usize i = 0;
  if (s == (const char*)0) {
    rt_write_all(2, "null", 4);
    return;
  }
  rt_write_all(2, "\"", 1);
  while (s[i] != '\0') {
    char c = s[i++];
    if (c == '\\') {
      rt_write_all(2, "\\\\", 2);
    } else if (c == '"') {
      rt_write_all(2, "\\\"", 2);
    } else if (c == '\n') {
      rt_write_all(2, "\\n", 2);
    } else if (c == '\r') {
      rt_write_all(2, "\\r", 2);
    } else if (c == '\t') {
      rt_write_all(2, "\\t", 2);
    } else if ((unsigned char)c < 32U) {
      rt_write_all(2, "?", 1);
    } else {
      rt_write_all(2, &c, 1);
    }
  }
  rt_write_all(2, "\"", 1);
}

static void print_diagnostics_json(const convert_diagnostics* diags) {
  usize i;
  usize errors = 0;
  usize warns = 0;
  usize infos = 0;

  if (diags == 0) {
    write_str_err("{\"errors\":0,\"warnings\":0,\"infos\":0,\"count\":0,\"dropped\":0,\"items\":[]}\n");
    return;
  }

  for (i = 0; i < diags->count; i++) {
    if (diags->items[i].severity == CONVERT_DIAG_ERROR) {
      errors++;
    } else if (diags->items[i].severity == CONVERT_DIAG_WARN) {
      warns++;
    } else {
      infos++;
    }
  }

  write_str_err("{\"errors\":");
  write_u64_fd(2, (u64)errors);
  write_str_err(",\"warnings\":");
  write_u64_fd(2, (u64)warns);
  write_str_err(",\"infos\":");
  write_u64_fd(2, (u64)infos);
  write_str_err(",\"count\":");
  write_u64_fd(2, (u64)diags->count);
  write_str_err(",\"dropped\":");
  write_u64_fd(2, (u64)diags->dropped_count);
  write_str_err(",\"items\":[");

  for (i = 0; i < diags->count; i++) {
    if (i > 0) {
      write_str_err(",");
    }
    write_str_err("{\"severity\":");
    print_json_string_err(diag_severity_name(diags->items[i].severity));
    write_str_err(",\"stage\":");
    write_u64_fd(2, (u64)diags->items[i].stage);
    write_str_err(",\"code\":");
    write_u64_fd(2, (u64)diags->items[i].code);
    write_str_err(",\"message\":");
    print_json_string_err(diags->items[i].message);
    write_str_err("}");
  }
  write_str_err("]}\n");
}

static void print_diagnostics(const convert_diagnostics* diags) {
  usize i;
  if (diags == 0) {
    return;
  }

  for (i = 0; i < diags->count; i++) {
    write_str_err("diag: ");
    if (diags->items[i].severity == CONVERT_DIAG_ERROR) {
      write_str_err("error");
    } else if (diags->items[i].severity == CONVERT_DIAG_WARN) {
      write_str_err("warn");
    } else {
      write_str_err("info");
    }
    write_str_err(": ");
    if (diags->items[i].message != (const char*)0) {
      write_str_err(diags->items[i].message);
    } else {
      write_str_err("(no message)");
    }
    write_str_err("\n");
  }
}

static int cmd_validate(const char* in_path) {
  static u8 odt[CLI_MAX_FILE_BYTES];
  usize n = 0;
  int rc;
  rc = read_file_all(in_path, odt, sizeof(odt), &n);
  if (rc != 0) {
    if (rc == -3) {
      write_str_err("error: input file exceeds configured CLI_MAX_FILE_BYTES\n");
    } else {
      write_str_err("error: failed to read input file\n");
    }
    return 2;
  }
  rc = odt_core_validate_package(odt, n);
  if (rc != ODT_OK) {
    if (rc == ODT_ERR_TOO_LARGE) {
      write_str_err("invalid: package metadata too large for current limits\n");
      return 3;
    }
    write_str_err("invalid: package validation failed\n");
    return 3;
  }
  write_str("ok\n");
  return 0;
}

static int cmd_inspect(const char* in_path) {
  static u8 odt[CLI_MAX_FILE_BYTES];
  zip_archive za;
  u16 n = 0;
  u16 i;
  usize len = 0;

  if (read_file_all(in_path, odt, sizeof(odt), &len) != 0) {
    write_str_err("error: failed to read input file\n");
    return 2;
  }
  if (zip_archive_open(&za, odt, len) != ZIP_OK) {
    write_str_err("error: not a valid zip/odt\n");
    return 3;
  }

  write_str("file: ");
  write_str(in_path);
  write_str("\nsize: ");
  write_u64((u64)len);
  write_str("\n");

  if (odt_core_validate_package(odt, len) == ODT_OK) {
    write_str("package: valid odt core rules\n");
  } else {
    write_str("package: invalid odt core rules\n");
  }

  zip_archive_entry_count(&za, &n);
  write_str("entries: ");
  write_u64((u64)n);
  write_str("\n");

  for (i = 0; i < n; i++) {
    zip_entry_view ze;
    if (zip_archive_get_entry(&za, i, &ze) != ZIP_OK) {
      write_str_err("error: failed while scanning entries\n");
      return 4;
    }
    rt_write_all(1, "  - ", 4);
    rt_write_all(1, ze.name, ze.name_len);
    rt_write_all(1, " (method=", 9);
    write_u64((u64)ze.method);
    rt_write_all(1, ", size=", 7);
    write_u64((u64)ze.uncomp_size);
    rt_write_all(1, ")\n", 2);
  }

  return 0;
}

static int cmd_extract_text(const char* in_path, const char* out_path_or_null) {
  static u8 odt[CLI_MAX_FILE_BYTES];
  static char txt[CLI_MAX_FILE_BYTES];
  usize odt_len = 0;
  usize txt_len = 0;

  if (read_file_all(in_path, odt, sizeof(odt), &odt_len) != 0) {
    write_str_err("error: failed to read input file\n");
    return 2;
  }
  {
    int rc = odt_core_extract_plain_text(odt, odt_len, txt, sizeof(txt), &txt_len);
    if (rc == ODT_ERR_TOO_LARGE) {
      write_str_err("error: content.xml too large for current limits\n");
      return 3;
    }
    if (rc != ODT_OK) {
      write_str_err("error: failed to extract text\n");
      return 3;
    }
  }

  if (out_path_or_null != (const char*)0) {
    if (write_file_all(out_path_or_null, (const u8*)txt, txt_len) != 0) {
      write_str_err("error: failed to write output text\n");
      return 4;
    }
    write_str("ok\n");
    return 0;
  }

  rt_write_all(1, txt, txt_len);
  return 0;
}

static int cmd_create(const char* in_txt, const char* out_odt) {
  static u8 txt[CLI_MAX_FILE_BYTES];
  static u8 odt[CLI_MAX_FILE_BYTES];
  usize txt_len = 0;
  usize odt_len = 0;

  if (read_file_all(in_txt, txt, sizeof(txt), &txt_len) != 0) {
    write_str_err("error: failed to read input text\n");
    return 2;
  }

  if (odt_core_build_minimal((const char*)txt, txt_len, odt, sizeof(odt), &odt_len) != ODT_OK) {
    write_str_err("error: failed to build odt\n");
    return 3;
  }

  if (write_file_all(out_odt, odt, odt_len) != 0) {
    write_str_err("error: failed to write odt output\n");
    return 4;
  }

  write_str("ok\n");
  return 0;
}

static int cmd_repack(const char* in_odt, const char* out_odt) {
  static u8 in_buf[CLI_MAX_FILE_BYTES];
  static u8 out_buf[CLI_MAX_FILE_BYTES];
  static zip_writer zw;
  zip_archive za;
  usize in_len = 0;
  usize out_len = 0;
  u16 entry_count = 0;
  u16 i;

  if (read_file_all(in_odt, in_buf, sizeof(in_buf), &in_len) != 0) {
    write_str_err("error: failed to read input odt\n");
    return 2;
  }
  if (odt_core_validate_package(in_buf, in_len) != ODT_OK) {
    write_str_err("error: input is not a valid odt package\n");
    return 3;
  }
  if (zip_archive_open(&za, in_buf, in_len) != ZIP_OK) {
    write_str_err("error: failed to open input archive\n");
    return 4;
  }
  if (zip_archive_entry_count(&za, &entry_count) != ZIP_OK) {
    write_str_err("error: failed to read entry count\n");
    return 5;
  }

  zip_writer_init(&zw, out_buf, sizeof(out_buf));
  for (i = 0; i < entry_count; i++) {
    zip_entry_view ze;
    char name_buf[ZIP_WRITER_MAX_NAME + 1];
    usize j;

    if (zip_archive_get_entry(&za, i, &ze) != ZIP_OK) {
      write_str_err("error: failed to read input entry\n");
      return 6;
    }
    if (zip_entry_name_is_safe(&ze) != ZIP_OK) {
      write_str_err("error: unsafe input entry path\n");
      return 7;
    }
    if (ze.name_len > ZIP_WRITER_MAX_NAME) {
      write_str_err("error: entry name too long\n");
      return 8;
    }

    for (j = 0; j < ze.name_len; j++) {
      name_buf[j] = ze.name[j];
    }
    name_buf[ze.name_len] = '\0';

    if (zip_writer_add_raw_entry(&zw,
                                 name_buf,
                                 ze.method,
                                 ze.crc32,
                                 ze.comp_size,
                                 ze.uncomp_size,
                                 ze.archive_data + ze.data_pos,
                                 (usize)ze.comp_size) != ZIP_OK) {
      write_str_err("error: failed to write output entry\n");
      return 10;
    }
  }

  if (zip_writer_finish(&zw, &out_len) != ZIP_OK) {
    write_str_err("error: failed to finalize output archive\n");
    return 11;
  }
  if (odt_core_validate_package(out_buf, out_len) != ODT_OK) {
    write_str_err("error: repacked output failed validation\n");
    return 12;
  }
  if (write_file_all(out_odt, out_buf, out_len) != 0) {
    write_str_err("error: failed to write output odt\n");
    return 13;
  }

  write_str("ok\n");
  return 0;
}

static int cmd_convert(const char* from_fmt,
                       const char* to_fmt,
                       const char* template_path_or_null,
                       int diag_json,
                       convert_policy_mode policy,
                       const char* in_path,
                       const char* out_path) {
  static u8 in_buf[CLI_MAX_FILE_BYTES];
  static u8 out_buf[CLI_MAX_FILE_BYTES];
  static u8 template_buf[CLI_MAX_FILE_BYTES];
  static fmt_markdown_state md_state;
  static fmt_odt_state odt_state;
  convert_registry registry;
  convert_session session;
  convert_diagnostics diags;
  doc_model_document doc;
  convert_request req;
  meltdown_profile_runtime meltdown_profile;
  i32 meltdown_roles[MELTDOWN_MAX_STREAM_BLOCKS];
  meltdown_block_stream meltdown_stream;
  const convert_format_handler* importer = 0;
  const convert_format_handler* exporter = 0;
  usize in_len = 0;
  usize out_len = 0;
  usize template_len = 0;
  int template_is_meltdown = 0;
  int rc;

  if (template_path_or_null != (const char*)0) {
    if (!cstr_eq(to_fmt, "odt")) {
      write_str_err("error: --template is only valid with --to odt\n");
      return 7;
    }

    rc = read_file_all(template_path_or_null, template_buf, sizeof(template_buf), &template_len);
    if (rc != 0) {
      if (rc == -3) {
        write_str_err("error: template file exceeds configured CLI_MAX_FILE_BYTES\n");
      } else {
        write_str_err("error: failed to read template file\n");
      }
      return 7;
    }

    if (path_has_suffix(template_path_or_null, ".meltdown")) {
      char perr[256];
      convert_diagnostics early_diags;
      early_diags.count = 0;
      early_diags.dropped_count = 0;
      template_is_meltdown = 1;
      if (parse_meltdown_profile(template_buf, template_len, perr, sizeof(perr)) != 0) {
        write_str_err("error: invalid meltdown template: ");
        write_str_err(perr);
        write_str_err("\n");
        (void)convert_diagnostics_push(&early_diags,
                                       CONVERT_DIAG_ERROR,
                                       CONVERT_DIAG_VALIDATION_FAILURE,
                                       CONVERT_STAGE_PARSE,
                                       "meltdown/parse: invalid profile syntax or schema");
        if (diag_json) {
          print_diagnostics_json(&early_diags);
        }
        return 8;
      }
    }
    if (template_is_meltdown && !cstr_eq(from_fmt, "md")) {
      convert_diagnostics early_diags;
      early_diags.count = 0;
      early_diags.dropped_count = 0;
      write_str_err("error: meltdown template currently supports only --from md\n");
      (void)convert_diagnostics_push(&early_diags,
                                     CONVERT_DIAG_ERROR,
                                     CONVERT_DIAG_BAD_REQUEST,
                                     CONVERT_STAGE_PARSE,
                                     "meltdown/runtime: only md->odt is supported in v0.1");
      if (diag_json) {
        print_diagnostics_json(&early_diags);
      }
      return 9;
    }

    if (!template_is_meltdown && odt_core_validate_package(template_buf, template_len) != ODT_OK) {
      write_str_err("error: template file is not a valid odt package\n");
      return 7;
    }
  }

  if (read_file_all(in_path, in_buf, sizeof(in_buf), &in_len) != 0) {
    write_str_err("error: failed to read conversion input\n");
    return 2;
  }

  convert_registry_init(&registry);
  if (fmt_markdown_register(&registry, &md_state) != CONVERT_OK ||
      fmt_odt_register(&registry, &odt_state) != CONVERT_OK) {
    write_str_err("error: failed to initialize conversion adapters\n");
    return 3;
  }

  if (template_path_or_null != (const char*)0 && !template_is_meltdown) {
    fmt_odt_set_template(&odt_state, template_buf, template_len);
  } else {
    fmt_odt_clear_template(&odt_state);
  }

  if (convert_session_init(&session, &doc, &diags) != CONVERT_OK) {
    write_str_err("error: failed to initialize conversion session\n");
    return 4;
  }

  importer = convert_registry_find(&registry, from_fmt);
  exporter = convert_registry_find(&registry, to_fmt);
  if (importer == 0 || exporter == 0 || importer->import_doc == 0 || exporter->export_doc == 0) {
    write_str_err("error: format handler not found\n");
    return 5;
  }

  if (template_is_meltdown) {
    char perr[256];
    usize assigned_non_body = 0;
    usize applied_styles = 0;
    usize present_changes = 0;
    doc_model_validation_error model_err;

    rc = importer->import_doc(
        importer, in_buf, in_len, CONVERT_POLICY_LOSSY, &session.doc, session.diags);
    if (rc != CONVERT_OK) {
      write_str_err("error: conversion failed\n");
      if (diag_json) {
        print_diagnostics_json(session.diags);
      } else {
        print_diagnostics(session.diags);
      }
      return 5;
    }

    rc = meltdown_build_block_stream(session.doc.doc, &meltdown_stream);
    if (rc == CONVERT_ERR_LIMIT) {
      (void)convert_diagnostics_push(session.diags,
                                     CONVERT_DIAG_WARN,
                                     CONVERT_DIAG_LOSSY_DROP,
                                     CONVERT_STAGE_NORMALIZE_IN,
                                     "meltdown: block stream truncated at MELTDOWN_MAX_STREAM_BLOCKS");
      rc = CONVERT_OK;
    }
    if (rc != CONVERT_OK) {
      write_str_err("error: failed to build meltdown markdown block stream\n");
      return 10;
    }

    if (parse_meltdown_runtime_profile(
            template_buf, template_len, &meltdown_profile, perr, sizeof(perr)) != 0) {
      write_str_err("error: failed to parse meltdown runtime profile: ");
      write_str_err(perr);
      write_str_err("\n");
      (void)convert_diagnostics_push(session.diags,
                                     CONVERT_DIAG_ERROR,
                                     CONVERT_DIAG_VALIDATION_FAILURE,
                                     CONVERT_STAGE_PARSE,
                                     "meltdown/parse: runtime profile parse failed");
      if (diag_json) {
        print_diagnostics_json(session.diags);
      }
      return 8;
    }

    if (meltdown_present_has_data(&meltdown_profile.present)) {
      (void)convert_diagnostics_push(session.diags,
                                     CONVERT_DIAG_INFO,
                                     CONVERT_DIAG_NONE,
                                     CONVERT_STAGE_PARSE,
                                     "meltdown/present: runtime model parsed");
    }

    {
      usize unsupported = meltdown_count_unsupported_predicates(&meltdown_profile);
      if (unsupported > 0) {
        (void)convert_diagnostics_push(session.diags,
                                       CONVERT_DIAG_WARN,
                                       CONVERT_DIAG_UNSUPPORTED_CONSTRUCT,
                                       CONVERT_STAGE_PARSE,
                                       "meltdown/schema: unsupported text predicates ignored in v0.1");
        if (policy == CONVERT_POLICY_STRICT) {
          (void)convert_diagnostics_push(session.diags,
                                         CONVERT_DIAG_ERROR,
                                         CONVERT_DIAG_BAD_REQUEST,
                                         CONVERT_STAGE_PARSE,
                                         "meltdown/strict: unsupported predicates are not allowed");
          if (diag_json) {
            print_diagnostics_json(session.diags);
          }
          return 11;
        }
      }
    }
    if (meltdown_classify_stream(&meltdown_profile,
                                 &meltdown_stream,
                                 meltdown_roles,
                                 MELTDOWN_MAX_STREAM_BLOCKS,
                                 &assigned_non_body) != CONVERT_OK) {
      write_str_err("error: meltdown classification failed\n");
      return 10;
    }

    meltdown_apply_layout_styles(
        session.doc.doc, &meltdown_profile, meltdown_roles, meltdown_stream.count, &applied_styles);

    model_err.code = DOC_MODEL_VALIDATION_NONE;
    model_err.field = 0;
    model_err.index = 0;
    if (doc_model_validate_document(session.doc.doc, &model_err) != DOC_MODEL_OK) {
      write_str_err("error: canonical document validation failed\n");
      return 5;
    }

    rc = exporter->export_doc(exporter,
                              &session.doc,
                              CONVERT_POLICY_LOSSY,
                              out_buf,
                              sizeof(out_buf),
                              &out_len,
                              session.diags);
    if (rc != CONVERT_OK) {
      write_str_err("error: conversion failed\n");
      if (diag_json) {
        print_diagnostics_json(session.diags);
      } else {
        print_diagnostics(session.diags);
      }
      return 5;
    }

    if (meltdown_present_has_data(&meltdown_profile.present)) {
      rc = meltdown_materialize_present_on_odt(
          &meltdown_profile.present, out_buf, out_len, sizeof(out_buf), &out_len, &present_changes);
      if (rc != CONVERT_OK) {
        write_str_err("error: failed to materialize meltdown present model\n");
        return 10;
      }
    }

    (void)convert_diagnostics_push(session.diags,
                                   CONVERT_DIAG_INFO,
                                   CONVERT_DIAG_NONE,
                                   CONVERT_STAGE_NORMALIZE_IN,
                                   "meltdown: markdown block stream prepared");
    (void)convert_diagnostics_push(session.diags,
                                   CONVERT_DIAG_INFO,
                                   CONVERT_DIAG_NONE,
                                   CONVERT_STAGE_TRANSFORM,
                                   "meltdown: single-pass role classification complete");
    if (applied_styles > 0) {
      (void)convert_diagnostics_push(session.diags,
                                     CONVERT_DIAG_INFO,
                                     CONVERT_DIAG_NONE,
                                     CONVERT_STAGE_NORMALIZE_OUT,
                                     "meltdown: layout styles applied");
    }
    if (present_changes > 0) {
      (void)convert_diagnostics_push(session.diags,
                                     CONVERT_DIAG_INFO,
                                     CONVERT_DIAG_NONE,
                                     CONVERT_STAGE_EMIT,
                                     "meltdown/present: styles.xml materialized");
    }

    if (diag_json) {
      print_diagnostics_json(session.diags);
    }

    if (write_file_all(out_path, out_buf, out_len) != 0) {
      write_str_err("error: failed to write conversion output\n");
      return 6;
    }

    write_str("ok\n");
    return 0;
  }

  req.from_format = from_fmt;
  req.to_format = to_fmt;
  req.input = in_buf;
  req.input_len = in_len;
  req.output = out_buf;
  req.output_cap = sizeof(out_buf);
  req.output_len = &out_len;
  req.policy = policy;

  rc = convert_core_run_with_registry(&req, &registry, &session);
  if (rc != CONVERT_OK) {
    write_str_err("error: conversion failed\n");
    if (diag_json) {
      print_diagnostics_json(&diags);
    } else {
      print_diagnostics(&diags);
    }
    return 5;
  }

  if (diag_json) {
    print_diagnostics_json(&diags);
  }

  if (write_file_all(out_path, out_buf, out_len) != 0) {
    write_str_err("error: failed to write conversion output\n");
    return 6;
  }

  write_str("ok\n");
  return 0;
}

int odt_cli_run(int argc, const char** argv) {
  if (argc < 2) {
    print_usage();
    return 1;
  }

  if (cstr_eq(argv[1], "validate")) {
    if (argc != 3) {
      print_usage();
      return 1;
    }
    return cmd_validate(argv[2]);
  }

  if (cstr_eq(argv[1], "inspect")) {
    if (argc != 3) {
      print_usage();
      return 1;
    }
    return cmd_inspect(argv[2]);
  }

  if (cstr_eq(argv[1], "extract-text")) {
    if (argc == 3) {
      return cmd_extract_text(argv[2], (const char*)0);
    }
    if (argc == 4) {
      return cmd_extract_text(argv[2], argv[3]);
    }
    print_usage();
    return 1;
  }

  if (cstr_eq(argv[1], "create")) {
    if (argc != 4) {
      print_usage();
      return 1;
    }
    return cmd_create(argv[2], argv[3]);
  }

  if (cstr_eq(argv[1], "repack")) {
    if (argc != 4) {
      print_usage();
      return 1;
    }
    return cmd_repack(argv[2], argv[3]);
  }

  if (cstr_eq(argv[1], "convert")) {
    const char* from_fmt = (const char*)0;
    const char* to_fmt = (const char*)0;
    const char* template_path = (const char*)0;
    const char* in_path = (const char*)0;
    const char* out_path = (const char*)0;
    int diag_json = 0;
    convert_policy_mode policy = CONVERT_POLICY_LOSSY;
    int i = 2;

    if (argc < 8) {
      print_usage();
      return 1;
    }

    if (!cstr_eq(argv[i], "--from") || i + 1 >= argc) {
      print_usage();
      return 1;
    }
    from_fmt = argv[i + 1];
    i += 2;

    if (i + 1 >= argc || !cstr_eq(argv[i], "--to")) {
      print_usage();
      return 1;
    }
    to_fmt = argv[i + 1];
    i += 2;

    while (i < argc - 2) {
      if (cstr_eq(argv[i], "--template")) {
        if (i + 1 >= argc - 1 || template_path != (const char*)0) {
          print_usage();
          return 1;
        }
        template_path = argv[i + 1];
        i += 2;
        continue;
      }
      if (cstr_eq(argv[i], "--diag-json")) {
        diag_json = 1;
        i += 1;
        continue;
      }
      if (cstr_eq(argv[i], "--strict")) {
        policy = CONVERT_POLICY_STRICT;
        i += 1;
        continue;
      }
      print_usage();
      return 1;
    }

    if (i + 2 != argc) {
      print_usage();
      return 1;
    }

    in_path = argv[i];
    out_path = argv[i + 1];
    return cmd_convert(from_fmt, to_fmt, template_path, diag_json, policy, in_path, out_path);
  }

  print_usage();
  return 1;
}
