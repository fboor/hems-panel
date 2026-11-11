// Minimal JSON value lookup for the Home Energy Manager panel.
//
// The device answers with a single JSON object. This scanner finds a key at any
// nesting depth and reads its value, so it works for a flat response as well as
// for one module object inside a larger document. It does not build a tree and
// does not allocate.
//
// SPDX-License-Identifier: MIT
#pragma once

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

namespace warpjson {

inline const char *skipWs(const char *p, const char *e) {
  while (p < e && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) {
    ++p;
  }
  return p;
}

// p points at the opening quote. Returns the position after the closing quote.
// Copies the unescaped text into out when out is not null.
inline const char *readString(const char *p, const char *e, char *out,
                              size_t cap) {
  if (p < e && *p == '"') {
    ++p;
  }
  size_t n = 0;
  while (p < e && *p != '"') {
    char c = *p++;
    if (c == '\\' && p < e) {
      const char esc = *p++;
      switch (esc) {
        case 'n': c = '\n'; break;
        case 't': c = '\t'; break;
        case 'r': c = '\r'; break;
        case 'b': c = '\b'; break;
        case 'f': c = '\f'; break;
        case 'u':
          // Surrogate pair handling is unnecessary for the keys and short
          // labels this panel reads; consume the four hex digits.
          for (int i = 0; i < 4 && p < e; ++i) {
            ++p;
          }
          c = '?';
          break;
        default: c = esc; break;
      }
    }
    if (out != nullptr && n + 1 < cap) {
      out[n++] = c;
    }
  }
  if (out != nullptr && cap > 0) {
    out[n] = '\0';
  }
  return (p < e) ? p + 1 : p;
}

// Finds `"key" :` anywhere in the document. outValue points at the first
// character of the value.
inline bool find(const char *json, size_t len, const char *key,
                 const char **outValue) {
  if (json == nullptr || key == nullptr || outValue == nullptr) {
    return false;
  }
  const char *p = json;
  const char *e = json + len;
  const size_t keyLen = strlen(key);
  char name[64];

  while (p < e) {
    if (*p != '"') {
      ++p;
      continue;
    }
    const char *after = readString(p, e, name, sizeof(name));
    const char *q = skipWs(after, e);
    if (q < e && *q == ':') {
      if (strlen(name) == keyLen && memcmp(name, key, keyLen) == 0) {
        *outValue = skipWs(q + 1, e);
        return true;
      }
      p = q + 1;
    } else {
      // A string value, not a key. Its content cannot be a key.
      p = after;
    }
  }
  return false;
}

inline bool number(const char *json, size_t len, const char *key, double &out) {
  const char *v = nullptr;
  if (!find(json, len, key, &v)) {
    return false;
  }
  const char *e = json + len;
  char tmp[48];
  size_t n = 0;
  if (v < e && (*v == '-' || *v == '+')) {
    tmp[n++] = *v++;
  }
  while (v < e && ((*v >= '0' && *v <= '9') || *v == '.' || *v == 'e' ||
                   *v == 'E' || *v == '+' || *v == '-')) {
    if (n + 1 < sizeof(tmp)) {
      tmp[n++] = *v;
    }
    ++v;
  }
  if (n == 0) {
    return false;
  }
  tmp[n] = '\0';
  char *end = nullptr;
  const double d = strtod(tmp, &end);
  if (end == tmp) {
    return false;
  }
  out = d;
  return true;
}

inline bool boolean(const char *json, size_t len, const char *key, bool &out) {
  const char *v = nullptr;
  if (!find(json, len, key, &v)) {
    return false;
  }
  const char *e = json + len;
  if (v < e && (*v == 't' || *v == 'T')) {
    out = true;
    return true;
  }
  if (v < e && (*v == 'f' || *v == 'F')) {
    out = false;
    return true;
  }
  double d = 0.0;
  if (number(json, len, key, d)) {
    out = d != 0.0;
    return true;
  }
  return false;
}

inline bool string(const char *json, size_t len, const char *key, char *out,
                   size_t cap) {
  const char *v = nullptr;
  if (!find(json, len, key, &v) || v >= json + len || *v != '"') {
    return false;
  }
  readString(v, json + len, out, cap);
  return true;
}

inline bool integer(const char *json, size_t len, const char *key, int &out) {
  double d = 0.0;
  if (!number(json, len, key, d)) {
    return false;
  }
  out = static_cast<int>(d);
  return true;
}

// Parses a JSON array of numbers at p. null entries become NaN so callers can
// keep the array index aligned with a value_ids array. The parser tolerates a
// trailing comma and whitespace, but does not allocate.
inline bool numberArray(const char *p, const char *e, double *out, size_t cap,
                        size_t &count) {
  count = 0;
  p = skipWs(p, e);
  if (p >= e || *p != '[') {
    return false;
  }
  ++p;

  for (;;) {
    p = skipWs(p, e);
    if (p >= e) {
      return false;
    }
    if (*p == ']') {
      return true;
    }

    double value = NAN;
    if (*p == 'n') {
      if (e - p < 4 || memcmp(p, "null", 4) != 0) {
        return false;
      }
      p += 4;
    } else {
      char tmp[48];
      size_t n = 0;
      if (*p == '-' || *p == '+') {
        tmp[n++] = *p++;
      }
      while (p < e && ((*p >= '0' && *p <= '9') || *p == '.' || *p == 'e' ||
                       *p == 'E' || *p == '+' || *p == '-')) {
        if (n + 1 < sizeof(tmp)) {
          tmp[n++] = *p;
        }
        ++p;
      }
      if (n == 0) {
        return false;
      }
      tmp[n] = '\0';
      char *end = nullptr;
      value = strtod(tmp, &end);
      if (end == tmp) {
        return false;
      }
    }

    if (count < cap) {
      out[count] = value;
    }
    ++count;

    p = skipWs(p, e);
    if (p < e && *p == ',') {
      ++p;
      continue;
    }
    if (p < e && *p == ']') {
      return true;
    }
    return false;
  }
}

// Calls fn(begin, length) for every object in the array stored under `key`.
// The span includes the braces. Stops early when fn returns false. This is used
// for /charge_manager/state, whose `chargers` array holds one object per box.
template <typename Fn>
inline bool forEachObject(const char *json, size_t len, const char *key,
                          Fn fn) {
  const char *v = nullptr;
  if (json == nullptr || key == nullptr || !find(json, len, key, &v)) {
    return false;
  }
  const char *e = json + len;
  v = skipWs(v, e);
  if (v >= e || *v != '[') {
    return false;
  }
  ++v;

  for (;;) {
    v = skipWs(v, e);
    if (v >= e) {
      return false;
    }
    if (*v == ']') {
      return true;
    }
    if (*v != '{') {
      return false;
    }

    const char *start = v;
    int depth = 0;
    bool inString = false;
    bool escaped = false;
    while (v < e) {
      const char c = *v;
      if (inString) {
        if (escaped) {
          escaped = false;
        } else if (c == '\\') {
          escaped = true;
        } else if (c == '"') {
          inString = false;
        }
      } else if (c == '"') {
        inString = true;
      } else if (c == '{') {
        ++depth;
      } else if (c == '}') {
        --depth;
        if (depth == 0) {
          ++v;
          break;
        }
      }
      ++v;
    }
    if (depth != 0) {
      return false;
    }
    if (!fn(start, static_cast<size_t>(v - start))) {
      return false;
    }

    v = skipWs(v, e);
    if (v < e && *v == ',') {
      ++v;
      continue;
    }
    if (v < e && *v == ']') {
      return true;
    }
    return false;
  }
}

// Array at the top level, e.g. the response of /meters/X/values.
inline bool topNumbers(const char *json, size_t len, double *out, size_t cap,
                       size_t &count) {
  if (json == nullptr) {
    return false;
  }
  return numberArray(json, json + len, out, cap, count);
}

// Array after "key":, e.g. {"value_ids":[...]}.
inline bool arrayNumbers(const char *json, size_t len, const char *key,
                         double *out, size_t cap, size_t &count) {
  const char *v = nullptr;
  if (json == nullptr || !find(json, len, key, &v)) {
    return false;
  }
  return numberArray(v, json + len, out, cap, count);
}

// Boolean array after "key":, e.g. {"inputs":[true,false,...]}.
inline bool arrayBooleans(const char *json, size_t len, const char *key,
                          bool *out, size_t cap, size_t &count) {
  const char *v = nullptr;
  if (json == nullptr || !find(json, len, key, &v)) {
    return false;
  }
  const char *e = json + len;
  v = skipWs(v, e);
  if (v >= e || *v != '[') {
    return false;
  }
  ++v;
  count = 0;
  for (;;) {
    v = skipWs(v, e);
    if (v >= e) {
      return false;
    }
    if (*v == ']') {
      return true;
    }
    bool value = false;
    if (strncmp(v, "true", 4) == 0) {
      value = true;
      v += 4;
    } else if (strncmp(v, "false", 5) == 0) {
      v += 5;
    } else {
      return false;
    }
    if (count < cap) {
      out[count] = value;
    }
    ++count;
    v = skipWs(v, e);
    if (v < e && *v == ',') {
      ++v;
      continue;
    }
    if (v < e && *v == ']') {
      return true;
    }
    return false;
  }
}

}  // namespace warpjson
