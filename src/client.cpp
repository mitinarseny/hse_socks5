#include "uv.h"
#include "uvw/dns.h"
#include "uvw/emitter.h"
#include "uvw/handle.hpp"
#include "uvw/stream.h"
#include "uvw/tcp.h"

#include "client.hpp"
#include "log.hpp"
#include "uvw/util.h"
#include <algorithm>
#include <cstddef>
#include <memory>
#include <netdb.h>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <utility>
#include <vector>

void Client::read(char buff[], int n) {
  this->read_buff.insert(read_buff.begin(), buff, buff + n);
}

bool Client::nread(int n) { return this->read_buff.size() >= n; }

void Client::handle_error_event(const uvw::ErrorEvent &ee) {
  log_error() << "client_conn error: " << ee.name() << ": " << ee.what()
              << std::endl;
  this->close();
}

void Client::handle_data_event(uvw::DataEvent &de) {
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
  if (!this->nread(10)) {
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

  std::string node;
  sockaddr dst_addrr;
  uint16_t dst_port;
  switch (cmd) {
  case Command::CONNECT: {
    log_debug() << "addr_type is: " << static_cast<unsigned int>(at)
                << std::endl;
    switch (at) {
    case AddrType::IPv4: {
      auto noder =
          ntohl(*reinterpret_cast<in_addr_t *>(this->read_buff.data() + 4));
      node = std::to_string(noder >> 24) + "." +
             std::to_string(noder >> 16 & 0xff) + "." +
             std::to_string(noder >> 8 & 0xff) + "." +
             std::to_string(noder & 0xff);
      dst_port =
          ntohs(*reinterpret_cast<uint16_t *>(this->read_buff.data() + 8));
      break;
    }
    case AddrType::DOMAIN_NAME: {
      std::size_t domain_name_size = this->read_buff[4];
      log_debug() << "domain name size: " << domain_name_size << std::endl;
      if (!this->nread(7 + domain_name_size)) {
        return;
      }
      node = std::string(this->read_buff.data() + 5, domain_name_size);
      dst_port = ntohs(*reinterpret_cast<uint16_t *>(this->read_buff.data() +
                                                     5 + domain_name_size));
      break;
    }
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

  log_debug() << "node: " << node << ", port: " << dst_port << std::endl;
  addrinfo hint{AI_NUMERICSERV, AF_INET, SOCK_STREAM};
  auto res =
      this->client_conn->loop().resource<uvw::GetAddrInfoReq>()->addrInfoSync(
          node, std::to_string(dst_port), &hint);
  if (!res.first) {
    log_error() << "addrinfo failed for " << node << " " << dst_port
                << std::endl;
    this->send_error_reply(Reply::HOST_UNREACHABLE);
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
        this->send_reply(Reply::SUCCEED, at, *res.second);
      });
  this->dst_conn->on<uvw::DataEvent>([&](uvw::DataEvent &de,
                                         uvw::TCPHandle &h) {
    log_debug() << "got data from dst_conn, length: " << de.length << std::endl;
    this->client_conn->write(std::move(de.data), de.length);
  });

  this->dst_conn->connect(*res.second->ai_addr);
}

void Client::handle_state_data(uvw::DataEvent &de) {
  this->dst_conn->write(std::move(de.data), de.length);
}

void Client::send_reply(Reply r, AddrType at, const addrinfo &bind_addr) {
  std::size_t addr_size = (bind_addr.ai_family == AF_INET6 ? 16 : 4);
  std::size_t reply_size = 6 + addr_size;
  std::vector<char> reply(reply_size, 0);
  reply[0] = SOCKS5_PROTO_VERSION;
  reply[1] = static_cast<char>(r);
  reply[2] = 0x00; // reserved
  reply[3] = static_cast<char>(at);
  switch (bind_addr.ai_family) {
  case AF_INET: {
    reply = std::vector<char>(6 + 4, 0);
    auto a = *reinterpret_cast<sockaddr_in *>(bind_addr.ai_addr);
    *reinterpret_cast<in_addr_t *>(reply.data() + 4) = a.sin_addr.s_addr;
    *reinterpret_cast<uint16_t *>(reply.data() + 4 + addr_size) =
        htons(a.sin_port);
    break;
  }
  case AF_INET6: {
    reply = std::vector<char>(6 + 16, 0);
    auto a = *reinterpret_cast<sockaddr_in6 *>(bind_addr.ai_addr);
    *reinterpret_cast<in6_addr *>(reply.data() + 4) = a.sin6_addr;
    *reinterpret_cast<uint16_t *>(reply.data() + 4 + addr_size) =
        htons(a.sin6_port);
    break;
  }
  }
  this->client_conn->tryWrite(reply.data(), reply_size);
}

void Client::send_error_reply(Reply r) {
  log_error() << "send_error_reply: " << static_cast<unsigned int>(r)
              << std::endl;
  this->send_reply(r, AddrType::IPv4, addrinfo{});
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

void Client::handle_end_event(const uvw::EndEvent &ee) { this->close(); }
