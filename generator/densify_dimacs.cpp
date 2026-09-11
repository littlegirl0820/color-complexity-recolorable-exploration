#include <charconv>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
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
    if (argc != 4) {
      std::cerr << "usage: " << argv[0]
                << " INPUT_CNF OUTPUT_CNF VARIABLE_MAP_TSV\n";
      return 2;
    }
    const std::string input_path = argv[1];
    const std::string output_path = argv[2];
    const std::string map_path = argv[3];
    for (const std::string& path : {output_path, map_path}) {
      std::ifstream existing(path);
      if (existing) {
        throw std::runtime_error("refusing to overwrite output: " + path);
      }
    }
    std::ifstream input(input_path);
    if (!input) throw std::runtime_error("cannot open input: " + input_path);

    std::vector<std::string> comments;
    std::vector<std::vector<int>> clauses;
    std::int64_t original_variables = -1;
    std::int64_t declared_clauses = -1;
    std::string line;
    while (std::getline(input, line)) {
      if (line.empty()) continue;
      if (line[0] == 'c') {
        comments.push_back(line);
        continue;
      }
      if (line[0] == 'p') {
        std::string marker;
        std::string format;
        std::istringstream header(line);
        header >> marker >> format >> original_variables >> declared_clauses;
        if (marker != "p" || format != "cnf" || original_variables <= 0 ||
            declared_clauses < 0) {
          throw std::runtime_error("invalid DIMACS header");
        }
        continue;
      }
      clauses.push_back(parse_clause(line));
    }
    if (original_variables <= 0 ||
        declared_clauses != static_cast<std::int64_t>(clauses.size())) {
      throw std::runtime_error("DIMACS clause count mismatch");
    }

    std::unordered_map<int, int> dense_by_original;
    std::vector<int> original_by_dense{0};
    for (std::vector<int>& clause : clauses) {
      for (int& literal : clause) {
        const int original = literal < 0 ? -literal : literal;
        if (original > original_variables) {
          throw std::runtime_error("literal exceeds declared variable count");
        }
        auto [position, inserted] = dense_by_original.emplace(
            original, static_cast<int>(original_by_dense.size()));
        if (inserted) original_by_dense.push_back(original);
        literal = literal < 0 ? -position->second : position->second;
      }
    }

    std::ofstream output(output_path, std::ios::trunc);
    if (!output) throw std::runtime_error("cannot open output: " + output_path);
    output << "c dense-dimacs-v1 original-variables=" << original_variables
           << '\n';
    for (const std::string& comment : comments) output << comment << '\n';
    output << "p cnf " << original_by_dense.size() - 1 << ' '
           << clauses.size() << '\n';
    for (const std::vector<int>& clause : clauses) {
      for (int literal : clause) output << literal << ' ';
      output << "0\n";
    }
    output.close();

    std::ofstream mapping(map_path, std::ios::trunc);
    if (!mapping) {
      throw std::runtime_error("cannot open variable map: " + map_path);
    }
    mapping << "dense_variable\toriginal_variable\n";
    for (std::size_t dense = 1; dense < original_by_dense.size(); ++dense) {
      mapping << dense << '\t' << original_by_dense[dense] << '\n';
    }
    mapping.close();
    std::cout << "dense-dimacs=WRITTEN original-variables="
              << original_variables << " dense-variables="
              << original_by_dense.size() - 1
              << " clauses=" << clauses.size() << std::endl;
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << '\n';
    return 2;
  }
}
