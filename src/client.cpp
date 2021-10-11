#include "uvw/stream.h"
#include "uvw/tcp.h"

#include "client.hpp"
#include "log.hpp"
#include <algorithm>
#include <string>
#include <vector>

void Client::handle_data_event(const uvw::DataEvent &de ,uvw::TCPHandle &h) {
  switch (this->state) {
  case State::GREETING:
    this->handle_state_greeting(de, h);
    break;
  case State::REQUEST:
    this->handle_state_request(de, h);
    break;
  case State::ADDRESS:
    this->handle_state_address(de, h);
    break;
  case State::DATA:
    this->handle_state_data(de, h);
    break;
  }
}

void Client::handle_state_greeting(const uvw::DataEvent &de, uvw::TCPHandle &h) {
  if (de.length < 3 || de.data[0] != SOCKS5_PROTO_VERSION) {
      log_error() << "wrong SOCKS version: " << static_cast<unsigned int>(de.data[0]) << std::endl;
      h.close();
  }
  std::vector<char> rep = {SOCKS5_PROTO_VERSION, static_cast<char>(AuthMethod::NO_AUTH_REQUIRED)};
  h.write(rep.data(), rep.size());
  this->setState(State::REQUEST);
}

void Client::handle_state_request(const uvw::DataEvent &de, uvw::TCPHandle &h) {
  if (de.length < 6 || de.data[0] != SOCKS5_PROTO_VERSION) {
      log_error() << "wrong SOCKS version: " << static_cast<unsigned int>(de.data[0]) << std::endl;
      h.close();
    }

    Command cmd = static_cast<Command>(de.data[1]);
    if (de.data[2] != 0x00) {
      log_error() << "RSV is not zero, but: " << static_cast<unsigned int>(de.data[2]) << std::endl;
      this->send_error_reply(h, Reply::COMMAND_NOT_SUPPORTED);
      return;
    }
    AddrType at = static_cast<AddrType>(de.data[3]);
    unsigned int dst_addr_size = de.length - 6;
    std::string dst_addr;
    uint16_t dst_port = *reinterpret_cast<uint16_t*>(de.data.get() + de.length - 2);

    Reply rep;
    std::string bind_addr;
    uint16_t bind_port;

    switch (cmd) {
    case Command::CONNECT: {
      switch (at) {
      case AddrType::IPv4:
        if (dst_addr_size != 4) {
          log_error() << "wrong IPv4 addr size: " << dst_addr_size << std::endl;
          this->send_error_reply(h, Reply::COMMAND_NOT_SUPPORTED);
          return;
        }
        dst_addr = std::string(de.data.get() + 4, dst_addr_size);
      case AddrType::DOMAIN_NAME:
        dst_addr_size = de.data[4];
        if (dst_addr_size != de.length - 7) {
          log_error() << "wrong domain name size: " << dst_addr_size <<
              ", want: " << de.length - 7 << std::endl;
          this->send_error_reply(h, Reply::COMMAND_NOT_SUPPORTED);
          return;
        }
        dst_addr = std::string(de.data.get() + 5, dst_addr_size);
      case AddrType::IPv6:
        if (dst_addr_size != 16) {
          log_error() << "wrong IPv6 addr size: " << dst_addr_size << std::endl;
          this->send_error_reply(h, Reply::GENERAL_ERROR);
          return;
        }
        dst_addr = std::string(de.data.get() + 4, dst_addr_size);
      default: {
        this->send_error_reply(h, Reply::ADDR_TYPE_NOT_SUPPORTED);
        return;
      }
      }
    }
    default:
      this->send_error_reply(h, Reply::COMMAND_NOT_SUPPORTED);
      return;
  }

  this->send_reply(h, Reply::SUCCEED, at, dst_addr, dst_port);
  this->setState(State::DATA);
}

void Client::handle_state_address(const uvw::DataEvent &de, uvw::TCPHandle &h) {

}

void Client::handle_state_data(const uvw::DataEvent &de, uvw::TCPHandle &h) {
  (log_info() << "data: ").write(de.data.get(), de.length) << std::endl;
}

void Client::send_reply(uvw::TCPHandle &h, Reply r, AddrType at, const std::string &bind_addr, uint8_t bind_port) {
  std::size_t reply_size = 6 + bind_addr.size();
  std::vector<char> reply(reply_size, 0);
  reply[0] = SOCKS5_PROTO_VERSION;
  reply[1] = static_cast<char>(r);
  reply[2] = 0x00; // reserved
  reply[3] = static_cast<char>(at);
  std::copy_n(bind_addr.begin(), bind_addr.size(), reply.begin() + 4);
  reply[reply_size - 2] = bind_port >> 8;
  reply[reply_size - 1] = bind_port & 0xff;
  h.write(reply.data(), reply.size());
}

void Client::send_error_reply(uvw::TCPHandle &h, Reply r) {
  this->send_reply(h, r, AddrType::IPv4, std::string(4, 0), 0);
}

void Client::handle_end_event(const uvw::EndEvent &ee, uvw::TCPHandle &) {
  log_info() << "end" << std::endl;
}
