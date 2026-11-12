#pragma once
#include <exception>
#include <string>
namespace relay {
constexpr int CONNECTION_ATTEMPTS = 3;
constexpr unsigned WAKE_SETTLE_MS = 15000, CONNECTION_RETRY_MS = 15000;
struct ConnectionEnd {
  bool established = false;
  std::string message;
};
// Called only on the console worker. Wait hooks must remain cancellable.
template<class Running, class Responds, class Wake, class Status, class Wait>
bool wake_before_connect(bool paired, Running running, Responds responds,
                         Wake wake, Status status, Wait wait) {
  if (!running()) return false;
  if (!paired || responds()) return running();
  if (!running()) return false;
  status("Console not responding. Sending wake request...");
  try { wake(); }
  catch (const std::exception &) {
    // A failed wake packet does not prove that a connection will fail.
    status("Wake request unavailable. Will still try connecting...");
  }
  if (!running()) return false;
  status("Waiting 15 seconds for the console to wake...");
  return wait(WAKE_SETTLE_MS) && running();
}
template<class Running, class Attempt, class Cleanup, class Status, class Wait, class Failed>
void connect_with_retries(Running running, Attempt attempt, Cleanup cleanup,
                          Status status, Wait wait, Failed failed) {
  for (int index = 1; index <= CONNECTION_ATTEMPTS && running(); ++index) {
    status("Connecting to console. Attempt " + std::to_string(index) + " of 3...");
    ConnectionEnd end;
    try { end = attempt(); }
    catch (const std::exception &e) { end.message = e.what(); }
    cleanup();
    if (!running()) return;
    // Once streaming was established, preserve normal disconnect handling.
    if (end.established) {
      failed(end.message.empty() ? "Console disconnected" : end.message);
      return;
    }
    if (index == CONNECTION_ATTEMPTS) {
      failed("Could not connect after 3 attempts. " +
             (end.message.empty() ? std::string("Check the console and network.") : end.message));
      return;
    }
    status("Console not ready. Retrying in 15 seconds... (" + std::to_string(index) + "/3)");
    if (!wait(CONNECTION_RETRY_MS)) return;
  }
}
}
