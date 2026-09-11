#include <charconv>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

std::vector<int> parse_clause(const std::string& line) {
  std::vector<int> result;
  const char* cursor = line.data();
  const char* end = cursor + line.size();
  bool terminated = false;
  while (cursor < end) {
    while (cursor < end && *cursor == ' ') ++cursor;
    if (cursor == end) break;
    int literal = 0;
    const auto parsed = std::from_chars(cursor, end, literal);
    if (parsed.ec != std::errc()) {
      throw std::runtime_error("invalid DIMACS clause");
    }
    cursor = parsed.ptr;
    if (literal == 0) {
      terminated = true;
      while (cursor < end && *cursor == ' ') ++cursor;
      if (cursor != end) {
        throw std::runtime_error("text follows DIMACS clause terminator");
      }
      break;
    }
    result.push_back(literal);
  }
  if (!terminated) throw std::runtime_error("unterminated DIMACS clause");
  return result;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc != 3) {
      std::cerr << "usage: " << argv[0] << " INPUT_111_CNF OUTPUT_CNF\n";
      return 2;
    }
    const std::string input_path = argv[1];
    const std::string output_path = argv[2];
    std::ifstream input(input_path);
    if (!input) throw std::runtime_error("cannot open input: " + input_path);
    {
      std::ifstream existing(output_path);
      if (existing) {
        throw std::runtime_error("refusing to overwrite output: " + output_path);
      }
    }
    std::ofstream output(output_path, std::ios::trunc);
    if (!output) throw std::runtime_error("cannot open output: " + output_path);

    std::string line;
    std::int64_t declared_clauses = -1;
    std::int64_t seen_clauses = 0;
    int degree1_units = 0;
    int degree2_units = 0;
    int degree3_units = 0;
    bool header_seen = false;
    output << "c residual-initial-domains-v1 degree1=1,2 degree2=1,2 "
              "degree3=1\n";
    while (std::getline(input, line)) {
      if (line.empty()) continue;
      if (line[0] == 'c') {
        const std::string needle = "tuple=111";
        const std::size_t position = line.find(needle);
        if (position != std::string::npos) {
          line.replace(position, needle.size(), "initial-domains=12,12,1");
        }
        output << line << '\n';
        continue;
      }
      if (line[0] == 'p') {
        std::string marker;
        std::string format;
        std::int64_t variables = 0;
        std::istringstream fields(line);
        fields >> marker >> format >> variables >> declared_clauses;
        if (header_seen || marker != "p" || format != "cnf" ||
            variables < 570 || declared_clauses < 0) {
          throw std::runtime_error("invalid DIMACS header");
        }
        header_seen = true;
        output << line << '\n';
        continue;
      }
      if (!header_seen) {
        throw std::runtime_error("clause appears before DIMACS header");
      }
      const std::vector<int> clause = parse_clause(line);
      ++seen_clauses;
      if (clause == std::vector<int>{24}) {
        output << "24 27 0\n";
        ++degree1_units;
      } else if (clause == std::vector<int>{84}) {
        output << "84 87 0\n";
        ++degree2_units;
      } else {
        if (clause == std::vector<int>{184}) ++degree3_units;
        if (clause == std::vector<int>{27} ||
            clause == std::vector<int>{87}) {
          throw std::runtime_error(
              "input contains a conflicting residual-tuple unit");
        }
        output << line << '\n';
      }
    }
    if (!header_seen || seen_clauses != declared_clauses) {
      throw std::runtime_error("DIMACS clause count mismatch");
    }
    if (degree1_units != 1 || degree2_units != 1 || degree3_units != 1) {
      throw std::runtime_error(
          "input does not contain exactly the three 111 tuple units");
    }
    output.close();
    std::cout << "residual-initial-domains=WRITTEN variables=unchanged clauses="
              << seen_clauses << " degree1=1,2 degree2=1,2 degree3=1"
              << std::endl;
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << '\n';
    return 2;
  }
}
