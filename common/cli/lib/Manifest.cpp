// common/cli/lib/Manifest.cpp
//
// Self-contained SHA256 (RFC 6234) + JSON writer. We deliberately do
// not pull in OpenSSL to keep the static binaries small and CGO-free.

#include "qs/common/cli/Manifest.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace qs::common::cli {

namespace {

// --- SHA-256 ---------------------------------------------------------

constexpr std::array<std::uint32_t, 64> kK = {
  0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
  0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
  0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
  0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
  0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
  0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
  0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
  0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
  0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
  0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
  0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
  0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
  0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
  0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
  0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
  0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

inline std::uint32_t rotr(std::uint32_t x, std::uint32_t n) {
  return (x >> n) | (x << (32 - n));
}

struct Sha256 {
  std::array<std::uint32_t, 8> h = {
    0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
    0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u,
  };
  std::array<std::uint8_t, 64> buf{};
  std::size_t                   buflen = 0;
  std::uint64_t                 total_bits = 0;

  void update(const std::uint8_t* p, std::size_t n) {
    total_bits += static_cast<std::uint64_t>(n) * 8u;
    while (n > 0) {
      auto take = std::min(n, std::size_t{64} - buflen);
      std::memcpy(buf.data() + buflen, p, take);
      buflen += take;
      p      += take;
      n      -= take;
      if (buflen == 64) {
        compress();
        buflen = 0;
      }
    }
  }

  void compress() {
    std::array<std::uint32_t, 64> w{};
    for (int i = 0; i < 16; ++i) {
      w[i] = (std::uint32_t(buf[i*4    ]) << 24) |
             (std::uint32_t(buf[i*4 + 1]) << 16) |
             (std::uint32_t(buf[i*4 + 2]) <<  8) |
             (std::uint32_t(buf[i*4 + 3]));
    }
    for (int i = 16; i < 64; ++i) {
      auto s0 = rotr(w[i-15], 7) ^ rotr(w[i-15], 18) ^ (w[i-15] >> 3);
      auto s1 = rotr(w[i-2], 17) ^ rotr(w[i-2], 19) ^ (w[i-2] >> 10);
      w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    auto a = h[0], b = h[1], c = h[2], d = h[3];
    auto e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 64; ++i) {
      auto S1   = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
      auto ch   = (e & f) ^ ((~e) & g);
      auto t1   = hh + S1 + ch + kK[i] + w[i];
      auto S0   = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
      auto mj   = (a & b) ^ (a & c) ^ (b & c);
      auto t2   = S0 + mj;
      hh = g;  g = f;  f = e;  e = d + t1;
      d  = c;  c = b;  b = a;  a = t1 + t2;
    }
    h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d;
    h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=hh;
  }

  std::array<std::uint8_t, 32> finalize() {
    std::uint64_t bits = total_bits;
    std::uint8_t pad[72]{};
    pad[0] = 0x80;
    auto pad_len = (buflen < 56) ? (56 - buflen) : (120 - buflen);
    update(pad, pad_len);
    std::uint8_t lenbe[8];
    for (int i = 0; i < 8; ++i) {
      lenbe[7 - i] = static_cast<std::uint8_t>((bits >> (i * 8)) & 0xff);
    }
    update(lenbe, 8);
    std::array<std::uint8_t, 32> out{};
    for (int i = 0; i < 8; ++i) {
      out[i*4    ] = static_cast<std::uint8_t>((h[i] >> 24) & 0xff);
      out[i*4 + 1] = static_cast<std::uint8_t>((h[i] >> 16) & 0xff);
      out[i*4 + 2] = static_cast<std::uint8_t>((h[i] >>  8) & 0xff);
      out[i*4 + 3] = static_cast<std::uint8_t>((h[i]      ) & 0xff);
    }
    return out;
  }
};

std::string hex(std::array<std::uint8_t, 32> d) {
  static const char* x = "0123456789abcdef";
  std::string s(64, '0');
  for (int i = 0; i < 32; ++i) {
    s[i*2    ] = x[(d[i] >> 4) & 0xf];
    s[i*2 + 1] = x[d[i]        & 0xf];
  }
  return s;
}

// --- JSON helpers ----------------------------------------------------

std::string jsonEscape(std::string_view s) {
  std::string out;
  out.reserve(s.size() + 2);
  for (char c : s) {
    switch (c) {
      case '"':  out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b";  break;
      case '\f': out += "\\f";  break;
      case '\n': out += "\\n";  break;
      case '\r': out += "\\r";  break;
      case '\t': out += "\\t";  break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char b[8];
          std::snprintf(b, sizeof(b), "\\u%04x", c);
          out += b;
        } else {
          out += c;
        }
    }
  }
  return out;
}

}  // namespace

std::string sha256OfFile(std::string_view path) {
  std::ifstream f((std::string(path)), std::ios::binary);
  if (!f) return "";
  Sha256 s;
  std::array<char, 4096> buf{};
  while (f) {
    f.read(buf.data(), buf.size());
    auto got = f.gcount();
    if (got > 0) {
      s.update(reinterpret_cast<const std::uint8_t*>(buf.data()),
               static_cast<std::size_t>(got));
    }
  }
  return hex(s.finalize());
}

std::string nowUtcIso8601() {
  auto t = std::time(nullptr);
  std::tm tm{};
#if defined(_WIN32)
  ::gmtime_s(&tm, &t);
#else
  ::gmtime_r(&t, &tm);
#endif
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
  return buf;
}

std::string toJson(const Manifest& m) {
  std::ostringstream os;
  os << "{\n";
  os << "  \"schema\":            \"heisenberg.manifest/v1\",\n";
  os << "  \"source_lang\":       \"" << jsonEscape(m.source_lang) << "\",\n";
  os << "  \"source_path\":       \"" << jsonEscape(m.source_path) << "\",\n";
  os << "  \"source_sha256\":     \"" << jsonEscape(m.source_sha256) << "\",\n";
  os << "  \"target\":            \"" << jsonEscape(m.target) << "\",\n";
  os << "  \"provider\":          \"" << jsonEscape(m.provider) << "\",\n";
  os << "  \"verbatim\":          " << (m.verbatim ? "true" : "false") << ",\n";
  os << "  \"estimate\":          {"
     << "\"num_qubits\": "      << m.estimate.num_qubits
     << ", \"two_qubit_count\": " << m.estimate.two_qubit_count
     << ", \"depth\": "         << m.estimate.depth << "},\n";
  os << "  \"shots\":             " << m.shots << ",\n";
  if (m.cost_usd_estimate.has_value()) {
    os << "  \"cost_usd_estimate\": " << *m.cost_usd_estimate << ",\n";
  } else {
    os << "  \"cost_usd_estimate\": null,\n";
  }
  os << "  \"produced_by\":       {"
     << "\"binary\": \""  << jsonEscape(m.produced_by_binary)  << "\""
     << ", \"version\": \"" << jsonEscape(m.produced_by_version) << "\""
     << ", \"git_sha\": \"" << jsonEscape(m.produced_by_git_sha) << "\"},\n";
  os << "  \"produced_at_utc\":   \"" << jsonEscape(m.produced_at_utc) << "\"\n";
  os << "}\n";
  return os.str();
}

}  // namespace qs::common::cli
