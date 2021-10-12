#include "log.hpp"
#include "server.hpp"

int main() {
  log_info() << "starting server" << std::endl;
  start_server("127.0.0.1", 1080);
}
