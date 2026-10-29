// HTTP GET for the desktop emulator (POSIX sockets).
//
// The firmware uses WiFiClient; this file is the same request against the same
// endpoint so the emulator exercises the real parsing code.
//
// SPDX-License-Identifier: MIT
#pragma once

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace warphttp {

struct Response {
  int status = 0;
  std::string body;
  std::string error;
  bool ok() const { return status >= 200 && status < 300 && !body.empty(); }
};

inline bool splitUrl(const std::string &url, std::string &host, int &port,
                     std::string &path) {
  std::string rest = url;
  const std::string scheme = "http://";
  if (rest.compare(0, scheme.size(), scheme) == 0) {
    rest = rest.substr(scheme.size());
  } else if (rest.find("://") != std::string::npos) {
    return false;  // https is not handled here
  }
  const size_t slash = rest.find('/');
  std::string authority = (slash == std::string::npos) ? rest : rest.substr(0, slash);
  path = (slash == std::string::npos) ? "/" : rest.substr(slash);
  if (path.empty()) {
    path = "/";
  }
  port = 80;
  const size_t colon = authority.rfind(':');
  if (colon != std::string::npos) {
    port = atoi(authority.c_str() + colon + 1);
    host = authority.substr(0, colon);
  } else {
    host = authority;
  }
  return !host.empty() && port > 0 && port < 65536;
}

inline int connectWithTimeout(const std::string &host, int port, int timeoutMs,
                              std::string &error) {
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo *res = nullptr;
  char portText[8];
  snprintf(portText, sizeof(portText), "%d", port);
  const int gai = getaddrinfo(host.c_str(), portText, &hints, &res);
  if (gai != 0) {
    error = std::string("getaddrinfo: ") + gai_strerror(gai);
    return -1;
  }

  int fd = -1;
  for (addrinfo *it = res; it != nullptr; it = it->ai_next) {
    fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
    if (fd < 0) {
      continue;
    }
    const int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    const int rc = connect(fd, it->ai_addr, it->ai_addrlen);
    if (rc == 0) {
      fcntl(fd, F_SETFL, flags);
      break;
    }
    if (errno == EINPROGRESS) {
      fd_set wfds;
      FD_ZERO(&wfds);
      FD_SET(fd, &wfds);
      timeval tv{};
      tv.tv_sec = timeoutMs / 1000;
      tv.tv_usec = (timeoutMs % 1000) * 1000;
      if (select(fd + 1, nullptr, &wfds, nullptr, &tv) > 0) {
        int soError = 0;
        socklen_t len = sizeof(soError);
        getsockopt(fd, SOL_SOCKET, SO_ERROR, &soError, &len);
        if (soError == 0) {
          fcntl(fd, F_SETFL, flags);
          break;
        }
        errno = soError;
      } else {
        errno = ETIMEDOUT;
      }
    }
    close(fd);
    fd = -1;
  }
  freeaddrinfo(res);
  if (fd < 0) {
    error = std::string("connect: ") + strerror(errno);
  }
  return fd;
}

inline Response get(const std::string &url, int timeoutMs = 5000) {
  Response r;
  std::string host;
  std::string path;
  int port = 80;
  if (!splitUrl(url, host, port, path)) {
    r.error = "invalid URL: " + url;
    return r;
  }

  const int fd = connectWithTimeout(host, port, timeoutMs, r.error);
  if (fd < 0) {
    return r;
  }

  timeval tv{};
  tv.tv_sec = timeoutMs / 1000;
  tv.tv_usec = (timeoutMs % 1000) * 1000;
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

  std::string request = "GET " + path + " HTTP/1.0\r\n";
  request += "Host: " + host + "\r\n";
  request += "Connection: close\r\n";
  request += "Accept: application/json\r\n\r\n";

  size_t sent = 0;
  while (sent < request.size()) {
    const ssize_t n = send(fd, request.data() + sent, request.size() - sent, 0);
    if (n <= 0) {
      r.error = std::string("send: ") + strerror(errno);
      close(fd);
      return r;
    }
    sent += static_cast<size_t>(n);
  }

  std::string raw;
  char buf[2048];
  const auto deadline = std::chrono::steady_clock::now() +
      std::chrono::milliseconds(timeoutMs);
  for (;;) {
    if (std::chrono::steady_clock::now() >= deadline) {
      break;
    }
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(fd, &rfds);
    timeval tv{};
    tv.tv_sec = 0;
    tv.tv_usec = 100000;  // 100ms
    const int sel = select(fd + 1, &rfds, nullptr, nullptr, &tv);
    if (sel == 0) {
      continue;  // timeout, check deadline
    }
    if (sel < 0) {
      r.error = std::string("select: ") + strerror(errno);
      break;
    }
    const ssize_t n = recv(fd, buf, sizeof(buf), 0);
    if (n == 0) {
      break;
    }
    if (n < 0) {
      r.error = std::string("recv: ") + strerror(errno);
      break;
    }
    raw.append(buf, static_cast<size_t>(n));
    // Check if we've received the full response based on Content-Length
    const size_t he = raw.find("\r\n\r\n");
    if (he != std::string::npos) {
      const std::string hdr = raw.substr(0, he);
      std::string lh = hdr;
      for (char &c : lh) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
      const size_t clp = lh.find("content-length:");
      if (clp != std::string::npos) {
        const size_t cl = strtoul(lh.c_str() + clp + 15, nullptr, 10);
        if (raw.size() >= he + 4 + cl) {
          break;
        }
      }
    }
    if (raw.size() > 1024 * 1024) {
      break;
    }
  }
  close(fd);

  const size_t headerEnd = raw.find("\r\n\r\n");
  if (headerEnd == std::string::npos) {
    r.error = "no HTTP header in response";
    return r;
  }
  const std::string header = raw.substr(0, headerEnd);
  r.body = raw.substr(headerEnd + 4);

  // A few embedded servers answer HTTP/1.1 with chunked encoding despite the
  // HTTP/1.0 request. Decode it so the parser sees the JSON, not the framing.
  std::string lowerHeader = header;
  for (char &c : lowerHeader) {
    c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
  }
  if (lowerHeader.find("transfer-encoding: chunked") != std::string::npos) {
    std::string decoded;
    size_t pos = 0;
    bool good = true;
    while (pos < r.body.size()) {
      const size_t lineEnd = r.body.find("\r\n", pos);
      if (lineEnd == std::string::npos) {
        good = false;
        break;
      }
      const std::string sizeText = r.body.substr(pos, lineEnd - pos);
      const unsigned long chunk =
          strtoul(sizeText.c_str(), nullptr, 16);
      pos = lineEnd + 2;
      if (chunk == 0) {
        break;
      }
      if (pos + chunk > r.body.size()) {
        good = false;
        break;
      }
      decoded.append(r.body, pos, chunk);
      pos += chunk;
      if (r.body.compare(pos, 2, "\r\n") == 0) {
        pos += 2;
      }
    }
    if (good && !decoded.empty()) {
      r.body.swap(decoded);
    }
  }

  const size_t sp = header.find(' ');
  if (sp != std::string::npos) {
    r.status = atoi(header.c_str() + sp + 1);
  }
  if (r.status != 200) {
    r.error = "HTTP status " + std::to_string(r.status);
  }
  return r;
}  // namespace warphttp
}
