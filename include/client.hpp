#include "uvw/emitter.h"
#include "uvw/stream.h"
#include "uvw/tcp.h"
#include <memory>
#include <netdb.h>

#include "log.hpp"

constexpr uint8_t SOCKS5_PROTO_VERSION = 0x05;

class Client {
public:
  Client(std::shared_ptr<uvw::TCPHandle> &h): client_conn(h), state(State::GREETING) {}
  void handle_data_event(uvw::DataEvent &);
  void handle_error_event(const uvw::ErrorEvent &);
  void handle_end_event(const uvw::EndEvent &);
private:
  enum class State {
    GREETING,
    REQUEST,
    DATA,
  };
  State state = State::GREETING;

  void setState(State s) {
    log_debug() << "set state: " << static_cast<int>(s) << std::endl;
    this->state = s;
    this->read_buff.clear();
  }

  void handle_state_greeting();
  void handle_state_request();
  void handle_state_data(uvw::DataEvent &);

  enum class AuthMethod {
    NO_AUTH_REQUIRED       = 0x00,
    NO_ACCEPTABLE_METHODDS = 0xFF,
  };

  enum class Command {
    CONNECT       = 0x01,
    BIND          = 0x02,
    UDP_ASSOCIATE = 0x03,
  };

  enum class AddrType {
    IPv4        = 0x01,
    DOMAIN_NAME = 0x03,
    IPv6        = 0x04,
  };

  enum class Reply {
    SUCCEED                 = 0x00,
    GENERAL_ERROR           = 0x01,
    CONN_NOT_ALLOWED        = 0x02,
    NETWORK_UNREACHABLE     = 0x03,
    HOST_UNREACHABLE        = 0x04,
    CONN_REFUSED            = 0x05,
    TTL_EXPIRED             = 0x06,
    COMMAND_NOT_SUPPORTED   = 0x07,
    ADDR_TYPE_NOT_SUPPORTED = 0x08,
  };

  void send_reply(Reply r, AddrType at, const addrinfo &bind_addr);
  void send_error_reply(Reply);

  std::shared_ptr<uvw::TCPHandle> client_conn, dst_conn;

  void close();

  void read(char buff[], int n);
  bool nread(int n);
  std::vector<char> read_buff;
};
