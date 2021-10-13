#include "log.hpp"
#include "server.hpp"
#include <iostream>
#include <string>

void help(const std::string &);

int main(int argc, char *argv[]) {
  std::string ip("127.0.0.1");
  uint16_t port = 1080;

  switch (argc) {
  case 1:
    break;
  case 2:
    if (std::string(argv[1]) == "--help") {
      help(argv[0]);
      return 0;
    }
    port = std::stoi(argv[1]);
    break;
  case 3:
    ip = std::string(argv[1]);
    port = std::stoi(argv[2]);
    break;
  default:
    help(argv[0]);
    return 1;
  }

  start_server(ip, port);
}

void help(const std::string &name) {
  std::cerr << "Usage:\n\t" << name << " [[IP] PORT]" << std::endl;
}
