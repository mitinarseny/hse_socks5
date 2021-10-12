#include "uv.h"
#include "uvw/emitter.h"
#include "uvw/handle.hpp"
#include "uvw/stream.h"
#include "uvw/tcp.h"

#include "client.hpp"
#include "log.hpp"
#include "uvw/util.h"
#include <algorithm>
#include <memory>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <vector>

void Client::read(char buff[], int n) {
  this->read_buff.insert(read_buff.begin(), buff, buff + n);
}

bool Client::nread(int n) { return this->read_buff.size() >= n; }

void Client::handle_data_event(const uvw::DataEvent &de) {
  log_debug() << "read event, state " << static_cast<unsigned int>(this->state)
              << ", length: " << de.length << std::endl;
  switch (this->state) {
  case State::GREETING:
    this->read(de.data.get(), de.length);
    this->handle_state_greeting();
    break;
  case State::REQUEST:
    this->read(de.data.get(), de.length);
    this->handle_state_request();
    break;
  case State::DATA:
    this->handle_state_data(de);
    break;
  }
}

void Client::handle_state_greeting() {
  if (!this->nread(3)) {
    return;
  }
  if (this->read_buff[0] != SOCKS5_PROTO_VERSION) {
    log_error() << "wrong SOCKS version: "
                << static_cast<unsigned int>(this->read_buff[0]) << std::endl;
    this->close();
    return;
  }
  std::vector<char> rep = {SOCKS5_PROTO_VERSION,
                           static_cast<char>(AuthMethod::NO_AUTH_REQUIRED)};
  this->client_conn->write(rep.data(), rep.size());
  this->setState(State::REQUEST);
}

void Client::handle_state_request() {
  if (!this->nread(4)) {
    return;
  }
  if (this->read_buff[0] != SOCKS5_PROTO_VERSION) {
    log_error() << "wrong SOCKS version: "
                << static_cast<unsigned int>(this->read_buff[0]) << std::endl;
    this->close();
    return;
  }

  Command cmd = static_cast<Command>(this->read_buff[1]);
  if (this->read_buff[2] != 0x00) {
    log_error() << "RSV is not zero, but: "
                << static_cast<unsigned int>(this->read_buff[2]) << std::endl;
    this->send_error_reply(Reply::COMMAND_NOT_SUPPORTED);
    return;
  }
  AddrType at = static_cast<AddrType>(this->read_buff[3]);

  sockaddr dst_addrr;
  uint16_t dst_port;
  switch (cmd) {
  case Command::CONNECT: {
    switch (at) {
    case AddrType::IPv4: {
      if (!this->nread(6 + 4)) {
        return;
      }
      uvw::details::IpTraits<uvw::IPv4>::Type dst_addr;
      memset(&dst_addr, 0, sizeof(dst_addr));
      dst_addr.sin_family = AF_INET;
      dst_addr.sin_port = (*(this->read_buff.data() + 8) << 8) |
                          (*(this->read_buff.data() + 9) & 0xff);
      dst_addr.sin_addr.s_addr =
          *reinterpret_cast<in_addr_t *>(this->read_buff.data() + 4);
      dst_addrr = reinterpret_cast<const sockaddr &>(dst_addr);
      dst_port = dst_addr.sin_port;
      break;
    }
    // case AddrType::DOMAIN_NAME: {
    //   uint8_t dst_addr_size = this->read_buff[4];
    //   if (!this->nread(7 + dst_addr_size)) {
    //     return;
    //   }
    //   // TODO
    //   // dst_addr = std::string(this->read_buff.begin() + 5,
    //   // this->read_buff.begin() + dst_addr_size);
    //   break;
    // }
    // case AddrType::IPv6: {
    //   if (!this->nread(6 + 16)) {
    //     return;
    //   }
    //   // uvw::details::IpTraits<uvw::IPv6>::Type dst_addr;
    //   // memset(&dst_addr, 0, sizeof(dst_addr));
    //   // dst_addr.sin6_family = AF_INET6;
    //   // dst_addr.sin6_port = (*(this->read_buff.data()+8) << 8) |
    //   // (*(this->read_buff.data()+9) & 0xff); dst_addr.sin6_addr.s_addr =
    //   // *reinterpret_cast<in_addr_t*>(this->read_buff.data()+4); dst_addrr =
    //   // reinterpret_cast<const sockaddr &>(dst_addr); break;
    //   dst_addr.sa_family
    //   // = AF_INET; std::copy(this->read_buff.begin() + 4,
    //   // this->read_buff.begin() + 8, dst_addr.sa_data);
    //   break;
    // }
    default: {
      log_error() << "addr type not supported: "
                  << static_cast<unsigned int>(at) << std::endl;
      this->send_error_reply(Reply::ADDR_TYPE_NOT_SUPPORTED);
      return;
    }
    }
    break;
  }
  default:
    log_error() << "command not supported: " << static_cast<unsigned int>(cmd)
                << std::endl;
    this->send_error_reply(Reply::COMMAND_NOT_SUPPORTED);
    return;
  }

  this->dst_conn = this->client_conn->loop().resource<uvw::TCPHandle>();
  this->dst_conn->on<uvw::ErrorEvent>(
      [&](const uvw::ErrorEvent &ee, uvw::TCPHandle &h) {
        log_error() << "dst_conn error: " << ee.name() << ": " << ee.what()
                    << std::endl;
        switch (ee.code()) {
        case UV_EALREADY:
          return;
        case UV_ECANCELED:
          return;
        case UV_ECONNREFUSED:
          this->send_error_reply(Reply::CONN_REFUSED);
          return;
        case UV_EHOSTUNREACH:
          this->send_error_reply(Reply::HOST_UNREACHABLE);
          return;
        case UV_ENETUNREACH:
          this->send_error_reply(Reply::NETWORK_UNREACHABLE);
          return;
        default:
          this->send_error_reply(Reply::GENERAL_ERROR);
          return;
        }
      });
  this->dst_conn->on<uvw::ConnectEvent>(
      [&](uvw::ConnectEvent, uvw::TCPHandle &h) {
        log_debug() << "connected" << std::endl;
        this->setState(State::DATA);
        h.read();
        this->send_reply(Reply::SUCCEED, at, dst_addrr, dst_port);
      });
  this->dst_conn->on<uvw::DataEvent>(
      [&](const uvw::DataEvent &de, uvw::TCPHandle &h) {
        log_debug() << "got data from dst_conn, length: " << de.length << std::endl;
        this->client_conn->write(de.data.get(), de.length);
      });

  // TODO
  this->dst_conn->connect("34.117.59.81", 80);
}

void Client::handle_state_data(const uvw::DataEvent &de) {
  this->dst_conn->tryWrite(de.data.get(), de.length);
}

void Client::send_reply(Reply r, AddrType at, const sockaddr &bind_addr,
                        uint8_t bind_port) {
  std::size_t addr_size = (bind_addr.sa_family == AF_INET ? 4 : 16);
  std::size_t reply_size = 6 + addr_size;
  std::vector<char> reply(reply_size, 0);
  reply[0] = SOCKS5_PROTO_VERSION;
  reply[1] = static_cast<char>(r);
  reply[2] = 0x00; // reserved
  reply[3] = static_cast<char>(at);
  std::copy_n(bind_addr.sa_data, addr_size, reply.begin() + 4);
  reply[reply_size - 2] = bind_port >> 8;
  reply[reply_size - 1] = bind_port & 0xff;
  log_debug() << "send reply: " << static_cast<unsigned int>(r) << std::endl;
  this->client_conn->write(reply.data(), reply.size());
}

void Client::send_error_reply(Reply r) {
  log_error() << "send_error_reply" << static_cast<unsigned int>(r) << std::endl;
  this->send_reply(r, AddrType::IPv4, sockaddr{}, 0);
  this->close();
}

void Client::close() {
  log_info() << "close" << std::endl;
  if (this->client_conn != nullptr && this->client_conn.get() != nullptr) {
    this->client_conn->close();
  }
  this->client_conn = nullptr;
  if (this->dst_conn != nullptr && this->dst_conn.get() != nullptr) {
    this->dst_conn->close();
  }
  this->dst_conn = nullptr;
}

void Client::handle_end_event(const uvw::EndEvent &ee) { 
  // this->close();
}
