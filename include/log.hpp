#include <ostream>

std::ostream& log(const std::string &prefix);

std::ostream& log_debug();

std::ostream& log_info();

std::ostream& log_error();
