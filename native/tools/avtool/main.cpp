// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/version.hpp"

#include <iostream>
#include <string_view>

namespace {

void print_usage(std::ostream& output) {
  output << "用法: avtool <命令> [选项]\n"
            "\n"
            "当前可用命令:\n"
            "  --help       显示此帮助\n"
            "  --version    显示 av_core 版本\n";
}

}  // namespace

int main(int argc, char* argv[]) {
  if (argc == 1) {
    print_usage(std::cout);
    return 0;
  }

  const std::string_view command{argv[1]};
  if (command == "--help" || command == "-h" || command == "help") {
    print_usage(std::cout);
    return 0;
  }

  if (command == "--version" || command == "version") {
    std::cout << "avtool " << otgy::core::version_string() << '\n';
    return 0;
  }

  std::cerr << "未知命令: " << command << '\n';
  print_usage(std::cerr);
  return 2;
}
