#include <ctime>
#include <iomanip>
#include <iostream>
#include <ostream>

std::ostream& log(const std::string &prefix) {
  std::time_t now = std::time(nullptr);
  std::tm tm = *std::localtime(&now);
  return std::cout << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S") << " " << prefix;
}

std::ostream& log_debug() {
  return log("[DEBUG] ");
}

std::ostream& log_info() {
  return log("[INFO] ");
}

std::ostream& log_error() {
  return log("[ERROR] ");
}
