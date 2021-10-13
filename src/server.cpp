#include "uvw/emitter.h"
#include "uvw/loop.h"
#include "uvw/stream.h"
#include "uvw/tcp.h"
#include "uvw/util.h"
#include <cassert>
#include <iostream>
#include <memory>
#include <string>

#include "log.hpp"
#include "client.hpp"

void start_server(const std::string &ip, unsigned int port) {
  auto loop = uvw::Loop::getDefault();

  std::shared_ptr<uvw::TCPHandle> tcp = loop->resource<uvw::TCPHandle>();
  tcp->on<uvw::ErrorEvent>([](const uvw::ErrorEvent &, uvw::TCPHandle &) { assert(false); });

  tcp->on<uvw::ListenEvent>([](const uvw::ListenEvent &, uvw::TCPHandle &srv) {
     std::shared_ptr<uvw::TCPHandle> client = srv.loop().resource<uvw::TCPHandle>();

     client->on<uvw::ErrorEvent>([](const uvw::ErrorEvent &ee, uvw::TCPHandle &h) {
         log_error() << ee.name() << ": " << ee.what() << std::endl;
     });

     srv.accept(*client);

     uvw::Addr remote = client->peer();
     log_info() << "accept: " << remote.ip << " " << remote.port << std::endl;

     auto cl = std::make_shared<Client>(client);
     client->data(cl);

     client->on<uvw::ErrorEvent>([cl](const uvw::ErrorEvent &ee, uvw::TCPHandle &) {
         cl->handle_error_event(ee);
      });
     client->on<uvw::DataEvent>([cl](uvw::DataEvent &de, uvw::TCPHandle &) {
        cl->handle_data_event(de);
     });

     client->on<uvw::EndEvent>([cl](const uvw::EndEvent &ee, uvw::TCPHandle &) {
         cl->handle_end_event(ee);
     });

     client->read();
  });

  tcp->bind(ip, port);
  tcp->listen();

  loop->run();
}
