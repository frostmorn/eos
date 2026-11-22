#include "calc_logic.h"

#include <stdio.h>
#include <string.h>

void calc_logic_init(calc_engine_t *ce) {
  if (!ce) return;
  memset(ce, 0, sizeof(*ce));
}

static void calc_apply_pending(calc_engine_t *ce) {
  if (!ce->op) {
    ce->acc = ce->cur;
    return;
  }
  switch (ce->op) {
  case '+':
    ce->acc += ce->cur;
    break;
  case '-':
    ce->acc -= ce->cur;
    break;
  case '*':
    ce->acc *= ce->cur;
    break;
  case '/':
    if (ce->cur == 0) {
      ce->error = true;
      ce->acc = 0;
    } else {
      ce->acc /= ce->cur;
    }
    break;
  default:
    break;
  }
}

void calc_logic_press_token(calc_engine_t *ce, const char *tok) {
  if (!ce || !tok) return;

  char c = tok[0];
  if (c >= '0' && c <= '9') {
    if (ce->error) {
      ce->error = false;
      ce->acc = 0;
      ce->cur = 0;
      ce->op = 0;
    }
    if (!ce->entering) {
      ce->cur = 0;
      ce->entering = true;
    }
    if (ce->cur < CALC_MAX_INPUT_LIMIT && ce->cur > -CALC_MAX_INPUT_LIMIT) {
      ce->cur = ce->cur * 10 + (c - '0');
    }
    return;
  }

  if (c == 'C') {
    ce->acc = 0;
    ce->cur = 0;
    ce->op = 0;
    ce->entering = false;
    ce->error = false;
    return;
  }

  if (c == '=') {
    if (ce->op) {
      calc_apply_pending(ce);
      ce->cur = ce->acc;
      ce->op = 0;
      ce->entering = false;
    }
    return;
  }

  // Operator (+, -, *, /)
  if (ce->entering) {
    calc_apply_pending(ce);
    ce->cur = ce->acc;
    ce->entering = false;
  } else if (!ce->op) {
    ce->acc = ce->cur;
  }
  ce->op = c;
}

void calc_logic_backspace(calc_engine_t *ce) {
  if (ce && ce->entering) {
    ce->cur /= 10;
  }
}

int64_t calc_logic_get_display_val(const calc_engine_t *ce) {
  if (!ce) return 0;
  return ce->entering ? ce->cur : ce->acc;
}

void calc_logic_format_dec(const calc_engine_t *ce, char *buf, size_t buflen) {
  if (!buf || buflen == 0) return;
  if (!ce || ce->error) {
    snprintf(buf, buflen, "ERR: DIV BY 0");
    return;
  }
  int64_t val = calc_logic_get_display_val(ce);
  snprintf(buf, buflen, "%lld", (long long)val);
}

void calc_logic_format_hex(const calc_engine_t *ce, char *buf, size_t buflen) {
  if (!buf || buflen == 0) return;
  if (!ce || ce->error) {
    snprintf(buf, buflen, "HEX: 0x0");
    return;
  }
  int64_t val = calc_logic_get_display_val(ce);
  snprintf(buf, buflen, "HEX: 0x%llX", (unsigned long long)(uint64_t)val);
}

void calc_logic_format_expr(const calc_engine_t *ce, char *buf, size_t buflen) {
  if (!buf || buflen == 0) return;
  if (!ce) {
    buf[0] = '\0';
    return;
  }
  if (ce->op) {
    snprintf(buf, buflen, "%lld %c", (long long)ce->acc, ce->op);
  } else {
    snprintf(buf, buflen, "Ready");
  }
}
