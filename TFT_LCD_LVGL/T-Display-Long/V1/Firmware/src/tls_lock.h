#pragma once

#include <stdint.h>

// One TLS connection at a time. A handshake needs a large block of internal
// RAM, so the firmware update check, the update download and the weather fetch
// take turns. The lock is recursive: installFirmware() holds it and then calls
// refreshLatest(), which takes it again.
bool tlsLock(uint32_t waitMs);
void tlsUnlock();

class TlsGuard {
 public:
  explicit TlsGuard(uint32_t waitMs) : ok_(tlsLock(waitMs)) {}
  ~TlsGuard() {
    if (ok_) tlsUnlock();
  }
  TlsGuard(const TlsGuard&) = delete;
  TlsGuard& operator=(const TlsGuard&) = delete;
  bool ok() const { return ok_; }

 private:
  bool ok_;
};
