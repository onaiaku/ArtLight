/**
 * @file src/usbip_input_session.h
 * @brief The lifetime of the devices ONE stream is holding, and the promise to give them back.
 *
 * This is the file that decides whether somebody loses their keyboard. Everything under it is
 * careful and testable, but careful and testable is not the same as SAFE: attaching a device takes
 * it away from the machine it is plugged into, and the only thing standing between a user and a
 * keyboard that never comes home is that this object gets destroyed.
 *
 * So the release is not a call. It is a destructor.
 *
 * A stream session can end by many paths - the user closes the client, the client's network drops,
 * a worker thread throws, the process is killed, a stop races a start. Writing `detach()` at the
 * end of each of those means the one path nobody thought of is the one that strands the device.
 * Owning the devices in an object means every one of those paths releases them, because every one
 * of them destroys the object. There is no path that can skip a destructor.
 *
 * Terminology: the machine a device is plugged into is the EXPORTER, the machine it arrives on is
 * the IMPORTER. This file is the importer's. Never write "host" or "client" bare.
 */
#pragma once

#include <memory>
#include <string>

#include "src/usbip_input.h"
#include "src/usbip_input_policy.h"

namespace input::usbip {
  /**
   * @brief What a session wants attached, already resolved by the caller.
   *
   * The address is resolved BEFORE it gets here on purpose. Working out "which machine is this
   * stream coming from" belongs to the code that knows about streams; this file should not be able
   * to get it subtly wrong, and more importantly it should not be the place a reader has to go to
   * find out which of two addresses was used.
   */
  struct session_request_t {
    /// Straight from config. False means this whole file does nothing, and that is the default.
    bool enabled = false;

    /// The exporter to attach from. The caller resolves this: config's override if it is set,
    /// otherwise the address the stream is coming from.
    std::string exporter;

    /// The optional comma-separated busid allowlist, raw from config. Empty means everything.
    std::string busids;
  };

  /// One device this session took, and the port that gives it back.
  struct held_device_t {
    std::string busid;  ///< As reported by the exporter. May be empty (see Attached::busid).
    int port = 0;       ///< The only thing detach accepts. 0 is a VALID port on Linux.
  };

  /**
   * @brief Owns the devices one stream is holding. Destroying it gives them back.
   *
   * Deliberately not copyable and not movable: two objects that believe they own the same device
   * would each try to give it back, and the second attempt would detach whatever had taken that
   * port number next. One owner, one release.
   */
  class session_holder_t {
   public:
    /**
     * @brief Attach what the exporter is offering, per the request.
     *
     * Returns a holder that may be holding nothing. That is a normal result, not a failure: the
     * feature is off by default, the client may not be set up on this PC, and the exporter may
     * simply be offering nothing. Each of those is reported in words by report() rather than by a
     * silent empty holder, because "nothing was attached and nobody said why" is the state that
     * costs an afternoon to diagnose.
     *
     * Never throws. This runs on the streaming path; a failure to move a USB device must not be
     * able to take a stream down.
     */
    static std::shared_ptr<session_holder_t> attach(const session_request_t &request);

    /**
     * @brief Give every device back.
     *
     * This is the promise. It runs on every end path there is, including the ones nobody planned.
     * It never throws: a destructor that throws is a program that dies instead of releasing a
     * device, which is the exact opposite of the point.
     */
    ~session_holder_t();

    session_holder_t(const session_holder_t &) = delete;
    session_holder_t &operator=(const session_holder_t &) = delete;

    /// Whether this session is holding anything at all.
    bool holding() const {
      return !m_Held.empty();
    }

    /// What we took and what it was for, in words, for the log. Never empty.
    std::string report() const;

    /// Anything the release could not give back. Empty in every normal case; non-empty means a
    /// device is still away from the machine it belongs to and a person needs to know.
    const std::string &release_failure() const {
      return m_ReleaseFailure;
    }

   private:
    session_holder_t() = default;

    /// Give back one device by port. Returns false if the detach did not take.
    bool release(const held_device_t &device);

    std::vector<held_device_t> m_Held;

    /// Devices the destructor tried and FAILED to give back, in words, for the caller to log.
    /// A non-empty value here is the one state that needs a human: a device is still away from
    /// the machine it is plugged into.
    std::string m_ReleaseFailure;

    /// Why we are holding nothing, when we are. When the state is "nothing was taken and nobody
    /// said why", this is the sentence that says why - and it is filled in even for the boring
    /// cases, because the boring cases are the ones that get reported as bugs.
    std::string m_Report;
  };

  /**
   * @brief Sweep this machine clean of anything it is holding that it did not just attach.
   *
   * The insurance policy, run once at startup. If a previous run of this process died between
   * attaching and detaching - a power cut, a kill -9, a panic - the devices are still held, and the
   * machine they were taken from is missing them. Nothing in this process remembers that; the
   * kernel does, and `usbip port` reads it.
   *
   * Safe to run when nothing is held, and safe to run on a machine that has never attached
   * anything. It only ever releases devices the kernel says are attached AND the client agrees
   * about, and it reports what it released.
   *
   * @return A sentence describing what it did. Never empty.
   */
  std::string sweep_leftovers();

}  // namespace input::usbip
