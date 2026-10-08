/**
 * @file src/usbip_helper_protocol.h
 * @brief The one definition of how the ArtLight host and its privileged USB helper talk.
 *
 * Both ends include this, and that is the entire point of it existing. The helper is reached over a
 * socket systemd serves (artlight-input-service.socket), and the exchange is two short framed
 * packets:
 *
 *     request : <verb> [arg [arg]]
 *     reply   : <code> <outlen> <errlen>\n<out bytes><err bytes>
 *
 * A wire format written twice is a wire format that drifts, and this particular drift would land on
 * the half that runs as root. So every constant, every limit and BOTH directions of the encoding
 * live here: there is exactly one place to change it and exactly one place to be wrong.
 *
 * Why the reply carries the code and the two streams separately rather than just text: the caller
 * classifies on the code and quotes the reason, and the reason a tool refused arrives on stderr
 * while the exit code says only "no". A format that merged them would turn a diagnosable refusal
 * into an anonymous one, which has already cost this project a night.
 *
 * The arguments need no escaping, and that is deliberate rather than lucky: the policy admits only
 * letters, digits, dot, colon, hyphen and underscore in an exporter address, and a busid cannot
 * carry whitespace either. A packet is therefore split on whitespace and nothing else, and the verb
 * validates both halves again on its own side.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace input::usbip::helper_protocol {
  /// One accepted connection is one verb, so a request never needs more than a verb and two short
  /// arguments. Sized generously anyway: refusing a well-formed request because a buffer was tight
  /// would be a bug with no upside.
  constexpr std::size_t kMaxRequest = 1024;

  /// The reply travels as ONE packet, and a SOCK_SEQPACKET write is all-or-nothing, so it has to fit
  /// the socket's receive buffer. The unit sets that to 1M; this cap sits well inside it and the
  /// truncation says so in the text rather than quietly shortening a reason.
  constexpr std::size_t kMaxReplyField = 256 * 1024;

  /// The caller's receive buffer has to hold BOTH capped fields plus the header, because a
  /// SEQPACKET read that does not fit discards the remainder - and a discarded remainder is a
  /// reason the caller would never get to read.
  constexpr std::size_t kMaxReplyPacket = 2 * kMaxReplyField + 64;

  struct Request {
    std::string verb;
    std::vector<std::string> args;
  };

  /// Encode a request. Whitespace-joined, because no argument can contain whitespace and the verb
  /// validates what it receives again rather than trusting this side.
  inline std::string encode_request(const std::string_view verb,
                                    const std::vector<std::string> &args) {
    std::string packet {verb};
    for (const auto &arg : args) {
      packet.push_back(' ');
      packet += arg;
    }
    return packet;
  }

  /// Decode a request. At most a verb and two arguments are taken, and anything after the second
  /// argument is deliberately dropped rather than passed on: a verb that accepts a fixed arity
  /// should be handed a fixed arity, not a line it has to police itself.
  inline bool decode_request(const std::string_view packet, Request &request) {
    request.verb.clear();
    request.args.clear();

    std::istringstream fields {std::string {packet}};
    std::string field;
    if (!(fields >> request.verb)) {
      return false;
    }
    while (request.args.size() < 2 && (fields >> field)) {
      request.args.push_back(field);
    }
    return true;
  }

  /// Say in the text that a field was shortened, rather than shortening it silently. A truncated
  /// reason that does not admit it is worse than no reason at all.
  inline void cap_field(std::string &field) {
    if (field.size() <= kMaxReplyField) {
      return;
    }
    field.resize(kMaxReplyField);
    field += "\n[artlight-input-service: this output was truncated to fit the reply packet]\n";
  }

  /// Encode a reply: the header line, then stdout, then stderr, all inside one packet.
  inline std::string encode_reply(const int code, std::string out, std::string err) {
    cap_field(out);
    cap_field(err);

    std::ostringstream header;
    header << code << ' ' << out.size() << ' ' << err.size() << '\n';

    std::string packet = header.str();
    packet += out;
    packet += err;
    return packet;
  }

  /**
   * Decode a reply.
   *
   * Returns false for anything that is not exactly the format above: no header line, a field that is
   * not a number, a count running past the packet, or trailing junk. A short reply is reported as a
   * BROKEN reply rather than quietly parsed as a shorter one, because a classification made on half
   * a sentence is how a diagnosable refusal becomes an anonymous one.
   */
  inline bool decode_reply(const char *packet, const std::size_t size,
                           int &code, std::string &out, std::string &err) {
    if (packet == nullptr || size == 0) {
      return false;
    }

    const std::string_view whole {packet, size};
    const auto newline = whole.find('\n');
    if (newline == std::string_view::npos) {
      return false;
    }

    long decoded_code = 0;
    unsigned long out_length = 0;
    unsigned long err_length = 0;
    std::istringstream fields {std::string {whole.substr(0, newline)}};
    if (!(fields >> decoded_code >> out_length >> err_length)) {
      return false;
    }
    std::string trailing;
    if (fields >> trailing) {
      return false;
    }

    // EXACT, not "at least". This is a packet protocol, so a packet is precisely what the sender
    // wrote; bytes past the two counted fields mean the two ends disagree about the format, and the
    // honest answer to that is to refuse rather than to read the part that happens to line up. The
    // first version of this function ignored them, and the round-trip test caught it.
    const std::size_t body = newline + 1;
    if (body > size || out_length > size - body || err_length > size - body - out_length) {
      return false;
    }
    if (body + out_length + err_length != size) {
      return false;
    }

    code = static_cast<int>(decoded_code);
    out.assign(packet + body, out_length);
    err.assign(packet + body + out_length, err_length);
    return true;
  }
}  // namespace input::usbip::helper_protocol
