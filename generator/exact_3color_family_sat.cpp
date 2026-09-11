#include <cadical.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

constexpr int COLORS = 3;
constexpr int ACTIONS = COLORS * COLORS + 1;
constexpr int STOP = COLORS * COLORS;

struct Graph {
  std::string graph6;
  int n = 0;
  int colorings = 0;
  int physical = 0;
  std::vector<std::vector<int>> adjacency;
  // Empty means full exploration.  Otherwise entry s is the one vertex that
  // must be visited before returning to start s.
  std::vector<int> weak_targets;
};

struct Physical {
  int position = 0;
  std::vector<int> colors;
};

struct GraphVariables {
  int rank_bits = 0;
  std::vector<int> reach;
  std::vector<std::vector<int>> rank;
  std::vector<std::vector<int>> return_reach;
  std::vector<std::vector<int>> avoid_reach;
  // Variables for the weaker task that succeeds automatically after visiting
  // the designated target and returning to the start.
  std::vector<int> target_slot;
  std::vector<std::vector<int>> before_union_reach;
  std::vector<std::vector<std::vector<int>>> before_union_rank;
  std::vector<std::vector<int>> before_start_reach;
  std::vector<std::vector<int>> after_reach;
  std::vector<std::vector<std::vector<int>>> after_rank;
};

struct Formula {
  CaDiCaL::Solver solver;
  int next_variable = 1;
  std::int64_t clauses = 0;
  std::ofstream cnf;

  int allocate() { return next_variable++; }

  void add(const std::vector<int>& clause) {
    for (int literal : clause) solver.add(literal);
    solver.add(0);
    if (cnf) {
      for (int literal : clause) cnf << literal << ' ';
      cnf << "0\n";
    }
    ++clauses;
  }

  void add(std::initializer_list<int> clause) {
    add(std::vector<int>(clause));
  }

  void open_cnf(const std::string& path) {
    cnf.open(path, std::ios::binary | std::ios::trunc);
    if (!cnf) throw std::runtime_error("cannot write CNF: " + path);
    cnf << "p cnf " << std::string(10, ' ') << ' '
        << std::string(10, ' ') << '\n';
  }

  void finish_cnf() {
    if (!cnf) return;
    cnf.seekp(0);
    cnf << "p cnf " << std::setfill(' ') << std::setw(10)
        << next_variable - 1 << ' ' << std::setw(10) << clauses << '\n';
    cnf.close();
  }
};

int integer_power(int base, int exponent) {
  int result = 1;
  for (int i = 0; i < exponent; ++i) result *= base;
  return result;
}

Graph parse_graph6(const std::string& encoded) {
  if (encoded.empty()) throw std::runtime_error("graph6 string is empty");
  const int first = static_cast<unsigned char>(encoded.front());
  if (first == 126) {
    throw std::runtime_error("extended graph6 is not supported");
  }
  Graph graph;
  graph.graph6 = encoded;
  graph.n = first - 63;
  if (graph.n < 2 || graph.n > 8) {
    throw std::runtime_error("graph6 order must be in 2..8");
  }
  const int required_bits = graph.n * (graph.n - 1) / 2;
  std::vector<int> bits;
  bits.reserve(6 * (encoded.size() - 1));
  for (std::size_t index = 1; index < encoded.size(); ++index) {
    const int value = static_cast<unsigned char>(encoded[index]) - 63;
    if (value < 0 || value > 63) {
      throw std::runtime_error("invalid graph6 character");
    }
    for (int bit = 5; bit >= 0; --bit) {
      bits.push_back((value >> bit) & 1);
    }
  }
  if (static_cast<int>(bits.size()) < required_bits) {
    throw std::runtime_error("graph6 payload is too short");
  }
  if (encoded.size() != 1 + static_cast<std::size_t>((required_bits + 5) / 6)) {
    throw std::runtime_error("graph6 payload is not canonical");
  }
  for (std::size_t index = required_bits; index < bits.size(); ++index) {
    if (bits[index] != 0) {
      throw std::runtime_error("graph6 padding must be zero");
    }
  }
  graph.adjacency.assign(graph.n, {});
  int cursor = 0;
  for (int right = 1; right < graph.n; ++right) {
    for (int left = 0; left < right; ++left) {
      if (bits[cursor++] == 0) continue;
      graph.adjacency[left].push_back(right);
      graph.adjacency[right].push_back(left);
    }
  }
  std::vector<std::uint8_t> seen(graph.n, 0);
  std::vector<int> queue = {0};
  seen[0] = 1;
  for (std::size_t head = 0; head < queue.size(); ++head) {
    for (int next : graph.adjacency[queue[head]]) {
      if (seen[next] != 0) continue;
      seen[next] = 1;
      queue.push_back(next);
    }
  }
  if (static_cast<int>(queue.size()) != graph.n) {
    throw std::runtime_error("graph6 graph must be connected");
  }
  graph.colorings = integer_power(COLORS, graph.n);
  graph.physical = graph.n * graph.colorings;
  return graph;
}

Physical decode_physical(const Graph& graph, int state) {
  Physical result;
  result.position = state / graph.colorings;
  int code = state % graph.colorings;
  result.colors.resize(graph.n);
  for (int vertex = 0; vertex < graph.n; ++vertex) {
    result.colors[vertex] = code % COLORS;
    code /= COLORS;
  }
  return result;
}

int encode_physical(const Graph& graph, int position,
                    const std::vector<int>& colors) {
  int code = 0;
  int multiplier = 1;
  for (int color : colors) {
    code += color * multiplier;
    multiplier *= COLORS;
  }
  return position * graph.colorings + code;
}

std::string observation_key(int current,
                            const std::array<int, COLORS>& counts) {
  return std::to_string(current) + "|" + std::to_string(counts[0]) + "," +
         std::to_string(counts[1]) + "," + std::to_string(counts[2]);
}

class ExactFamilyEncoding {
 public:
  ExactFamilyEncoding(std::vector<Graph> graphs, bool canonical_stops,
                      bool color_symmetry, bool success_on_target_return,
                      bool success_on_full_stop_anywhere,
                      bool exact_reach_support)
      : graphs_(std::move(graphs)),
        canonical_stops_(canonical_stops),
        color_symmetry_(color_symmetry),
        success_on_target_return_(success_on_target_return),
        success_on_full_stop_anywhere_(success_on_full_stop_anywhere),
        exact_reach_support_(exact_reach_support) {
    for (const Graph& graph : graphs_) {
      for (const auto& neighbors : graph.adjacency) {
        max_degree_ = std::max(max_degree_, static_cast<int>(neighbors.size()));
      }
    }
    if (max_degree_ > 4) {
      throw std::runtime_error("maximum degree greater than four is unsupported");
    }
    initialize_observations();
    formula_.next_variable = observations() * ACTIONS + 1;
    formula_.solver.set("quiet", 1);
  }

  int observations() const {
    return static_cast<int>(observation_keys_.size());
  }

  int policy_variable(int observation, int action) const {
    return 1 + observation * ACTIONS + action;
  }

  void enable_cnf_output(const std::string& path) {
    if (!path.empty()) formula_.open_cnf(path);
  }

  void enable_proof_output(const std::string& path) {
    if (!path.empty() && !formula_.solver.trace_proof(path.c_str())) {
      throw std::runtime_error("cannot open proof output: " + path);
    }
  }

  void build(const std::vector<std::string>& block_paths,
             const std::string& initial_repaints,
             const std::string& fixed_policy,
             const std::string& required_rules,
             const std::string& preferred_policy,
             const std::string& preferred_model,
             int preferred_model_limit,
             const std::vector<std::string>& minimum_distance_policies,
             int minimum_distance,
             const std::vector<std::string>& maximum_distance_policies,
             int maximum_distance) {
    add_policy_constraints(initial_repaints);
    add_fixed_policy(fixed_policy);
    add_required_rules(required_rules);
    for (const std::string& policy : minimum_distance_policies) {
      add_minimum_policy_distance(policy, minimum_distance);
    }
    for (const std::string& policy : maximum_distance_policies) {
      add_maximum_policy_distance(policy, maximum_distance);
    }
    for (const std::string& path : block_paths) load_blocks(path);
    graph_variables_.reserve(graphs_.size());
    for (const Graph& graph : graphs_) {
      graph_variables_.push_back(allocate_graph_variables(graph));
    }
    for (std::size_t index = 0; index < graphs_.size(); ++index) {
      add_graph_constraints(graphs_[index], graph_variables_[index]);
    }
    // Auxiliary reachability and comparator variables have least-model-like
    // intended values.  False phases avoid exploring gratuitous supersets of
    // the actual reachable states; policy decisions retain solver defaults.
    for (int variable = observations() * ACTIONS + 1;
         variable < formula_.next_variable; ++variable) {
      formula_.solver.phase(-variable);
    }
    add_preferred_policy(preferred_policy);
    add_preferred_model(preferred_model, preferred_model_limit);
    for (int variable = 1; variable <= observations() * ACTIONS; ++variable) {
      formula_.solver.freeze(variable);
    }
    formula_.finish_cnf();
  }

  int solve() { return formula_.solver.solve(); }

  std::vector<int> policy() {
    std::vector<int> result(observations(), -1);
    for (int observation = 0; observation < observations(); ++observation) {
      for (int action = 0; action < ACTIONS; ++action) {
        if (formula_.solver.val(policy_variable(observation, action)) > 0) {
          result[observation] = action;
          break;
        }
      }
    }
    return result;
  }

  void write_model(const std::string& path) {
    if (path.empty()) return;
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("cannot write model: " + path);
    output << "s SATISFIABLE\n";
    int on_line = 0;
    for (int variable = 1; variable < formula_.next_variable; ++variable) {
      if (on_line == 0) output << 'v';
      const int value = formula_.solver.val(variable);
      if (value == 0) {
        throw std::runtime_error("solver returned an incomplete model");
      }
      output << ' ' << value;
      if (++on_line == 20) {
        output << '\n';
        on_line = 0;
      }
    }
    if (on_line != 0) output << '\n';
    output << "v 0\n";
  }

  bool verify_policy(const std::vector<int>& policy, std::string* failure) const {
    for (const Graph& graph : graphs_) {
      const int masks = 1 << graph.n;
      for (int start = 0; start < graph.n; ++start) {
        std::vector<std::uint8_t> status(graph.physical * masks, 0);
        std::function<bool(int, int)> visit = [&](int state, int mask) {
          const int position = state / graph.colorings;
          if (success_on_target_return_ && position == start &&
              (mask & (1 << graph.weak_targets[start])) != 0) {
            return true;
          }
          const int full = mask * graph.physical + state;
          if (status[full] == 1) {
            *failure = "reachable cycle in " + graph.graph6 +
                       " from start " + std::to_string(start + 1);
            return false;
          }
          if (status[full] == 2) return true;
          status[full] = 1;
          const Physical physical = decode_physical(graph, state);
          const int observation = observation_of(graph, physical);
          const int action = policy[observation];
          const std::vector<int> next_states =
              successors(graph, physical, action);
          if (next_states.empty()) {
            const bool visited_enough = graph.weak_targets.empty()
                ? mask == masks - 1
                : (mask & (1 << graph.weak_targets[start])) != 0;
            const bool successful_terminal =
                success_on_full_stop_anywhere_
                ? visited_enough
                : physical.position == start && visited_enough;
            if (action != STOP || !successful_terminal) {
              *failure = "premature terminal in " + graph.graph6 +
                         " from start " + std::to_string(start + 1);
              return false;
            }
          } else {
            for (int next : next_states) {
              const int next_position = next / graph.colorings;
              if (!visit(next, mask | (1 << next_position))) return false;
            }
          }
          status[full] = 2;
          return true;
        };
        const int initial = start * graph.colorings;
        if (!visit(initial, 1 << start)) return false;
      }
    }
    return true;
  }

  void print_summary(int imported_blocks) const {
    std::int64_t physical = 0;
    for (const Graph& graph : graphs_) physical += graph.physical;
    std::cout << "graphs=" << graphs_.size()
              << " observations=" << observations()
              << " physical=" << physical
              << " variables=" << formula_.next_variable - 1
              << " clauses=" << formula_.clauses
              << " imported-blocks=" << imported_blocks
              << " weak-graphs="
              << std::count_if(graphs_.begin(), graphs_.end(),
                               [](const Graph& graph) {
                                 return !graph.weak_targets.empty();
                               })
              << " success-on-target-return="
              << (success_on_target_return_ ? 1 : 0)
              << " success-on-full-stop-anywhere="
              << (success_on_full_stop_anywhere_ ? 1 : 0)
              << " exact-reach-support="
              << (exact_reach_support_ ? 1 : 0)
              << " canonical-stop-actions=" << (canonical_stops_ ? 1 : 0)
              << " color-symmetry=" << (color_symmetry_ ? 1 : 0)
              << std::endl;
  }

  int imported_blocks() const { return imported_blocks_; }

  void print_policy(const std::vector<int>& policy) const {
    for (int observation = 0; observation < observations(); ++observation) {
      std::cout << "rule observation=" << observation
                << " key=" << observation_keys_[observation]
                << " action=" << policy[observation] << '\n';
    }
  }

  int scan_policy_neighborhood(const std::vector<int>& center,
                               int distance,
                               const std::string& required_rules,
                               const std::string& hit_rules) const {
    if (static_cast<int>(center.size()) != observations()) {
      throw std::runtime_error(
          "scan-policy must have one action per observation");
    }
    if (distance < 0 || distance > 3) {
      throw std::runtime_error("scan-distance must be in 0..3");
    }
    for (int action : center) {
      if (action < 0 || action >= ACTIONS) {
        throw std::runtime_error("scan-policy actions must be in 0..9");
      }
    }

    const auto parse_rules = [&](const std::string& text,
                                 const std::string& option) {
      std::vector<std::pair<int, int>> result;
      if (text.empty()) return result;
      std::istringstream stream(text);
      std::string token;
      while (std::getline(stream, token, ',')) {
        const std::size_t separator = token.find(':');
        if (separator == std::string::npos ||
            token.find(':', separator + 1) != std::string::npos) {
          throw std::runtime_error(option +
                                   " must contain observation:action pairs");
        }
        const int observation = std::stoi(token.substr(0, separator));
        const int action = std::stoi(token.substr(separator + 1));
        if (observation < 0 || observation >= observations() ||
            action < 0 || action >= ACTIONS) {
          throw std::runtime_error(option + " entry is out of range");
        }
        result.emplace_back(observation, action);
      }
      return result;
    };
    const std::vector<std::pair<int, int>> required =
        parse_rules(required_rules, "scan required-rules");
    const std::vector<std::pair<int, int>> hits =
        parse_rules(hit_rules, "scan-hit-rules");
    std::vector<std::uint8_t> fixed(observations(), 0);
    for (const auto& [observation, action] : required) {
      if (center[observation] != action) {
        throw std::runtime_error(
            "scan-policy does not satisfy a required rule");
      }
      fixed[observation] = 1;
    }
    const auto action_is_valid = [&](int observation, int action) {
      return action == STOP ||
             observation_counts_[observation][action % COLORS] != 0;
    };

    std::vector<int> policy = center;
    std::uint64_t tested = 0;
    int successful = 0;
    const auto test = [&]() {
      if (!hits.empty() &&
          std::none_of(hits.begin(), hits.end(), [&](const auto& rule) {
            return policy[rule.first] == rule.second;
          })) {
        return;
      }
      ++tested;
      std::string failure;
      if (!verify_policy(policy, &failure)) return;
      ++successful;
      std::cout << "neighborhood-success=" << successful
                << " distance=" << distance << " actions=";
      for (int observation = 0; observation < observations(); ++observation) {
        if (observation != 0) std::cout << ',';
        std::cout << policy[observation];
      }
      std::cout << '\n';
    };

    if (distance == 0) {
      test();
    } else if (distance == 1) {
      for (int observation = 0; observation < observations(); ++observation) {
        if (fixed[observation] != 0) continue;
        for (int action = 0; action < ACTIONS; ++action) {
          if (action == center[observation] ||
              !action_is_valid(observation, action)) continue;
          policy[observation] = action;
          test();
        }
        policy[observation] = center[observation];
      }
    } else if (distance == 2) {
      for (int first = 0; first < observations(); ++first) {
        if (fixed[first] != 0) continue;
        for (int first_action = 0; first_action < ACTIONS; ++first_action) {
          if (first_action == center[first] ||
              !action_is_valid(first, first_action)) continue;
          policy[first] = first_action;
          for (int second = first + 1; second < observations(); ++second) {
            if (fixed[second] != 0) continue;
            for (int second_action = 0; second_action < ACTIONS;
                 ++second_action) {
              if (second_action == center[second] ||
                  !action_is_valid(second, second_action)) continue;
              policy[second] = second_action;
              test();
            }
            policy[second] = center[second];
          }
        }
        policy[first] = center[first];
      }
    } else {
      for (int first = 0; first < observations(); ++first) {
        if (fixed[first] != 0) continue;
        for (int first_action = 0; first_action < ACTIONS; ++first_action) {
          if (first_action == center[first] ||
              !action_is_valid(first, first_action)) continue;
          policy[first] = first_action;
          for (int second = first + 1; second < observations(); ++second) {
            if (fixed[second] != 0) continue;
            for (int second_action = 0; second_action < ACTIONS;
                 ++second_action) {
              if (second_action == center[second] ||
                  !action_is_valid(second, second_action)) continue;
              policy[second] = second_action;
              for (int third = second + 1; third < observations(); ++third) {
                if (fixed[third] != 0) continue;
                for (int third_action = 0; third_action < ACTIONS;
                     ++third_action) {
                  if (third_action == center[third] ||
                      !action_is_valid(third, third_action)) continue;
                  policy[third] = third_action;
                  test();
                }
                policy[third] = center[third];
              }
            }
            policy[second] = center[second];
          }
        }
        policy[first] = center[first];
      }
    }
    std::cout << "neighborhood=COMPLETE distance=" << distance
              << " tested=" << tested
              << " successful=" << successful << std::endl;
    return successful;
  }

 private:
  void initialize_observations() {
    for (int current = 0; current < COLORS; ++current) {
      for (int degree = 1; degree <= max_degree_; ++degree) {
        for (int c0 = 0; c0 <= degree; ++c0) {
          for (int c1 = 0; c1 <= degree - c0; ++c1) {
            const std::array<int, COLORS> counts = {
                c0, c1, degree - c0 - c1};
            const std::string key = observation_key(current, counts);
            observation_index_.emplace(
                key, static_cast<int>(observation_keys_.size()));
            observation_keys_.push_back(key);
            observation_counts_.push_back(counts);
          }
        }
      }
    }
  }

  int observation_of(const Graph& graph, const Physical& physical) const {
    std::array<int, COLORS> counts{};
    for (int neighbor : graph.adjacency[physical.position]) {
      ++counts[physical.colors[neighbor]];
    }
    return observation_index_.at(
        observation_key(physical.colors[physical.position], counts));
  }

  std::vector<int> successors(const Graph& graph, const Physical& physical,
                              int action) const {
    if (action == STOP) return {};
    const int repaint = action / COLORS;
    const int target = action % COLORS;
    std::vector<int> colors = physical.colors;
    colors[physical.position] = repaint;
    std::vector<int> result;
    for (int neighbor : graph.adjacency[physical.position]) {
      if (physical.colors[neighbor] == target) {
        result.push_back(encode_physical(graph, neighbor, colors));
      }
    }
    return result;
  }

  int swapped_policy_variable(int observation, int action) const {
    if (action == STOP) {
      const std::string& key = observation_keys_[observation];
      const std::size_t bar = key.find('|');
      const int current = key[0] - '0';
      std::array<int, COLORS> counts{};
      char comma = '\0';
      std::istringstream stream(key.substr(bar + 1));
      stream >> counts[0] >> comma >> counts[1] >> comma >> counts[2];
      const auto swap_color = [](int color) {
        return color == 1 ? 2 : color == 2 ? 1 : 0;
      };
      const std::array<int, COLORS> swapped_counts = {
          counts[0], counts[2], counts[1]};
      const int swapped_observation = observation_index_.at(
          observation_key(swap_color(current), swapped_counts));
      return policy_variable(swapped_observation, STOP);
    }
    const std::string& key = observation_keys_[observation];
    const std::size_t bar = key.find('|');
    const int current = key[0] - '0';
    std::array<int, COLORS> counts{};
    char comma = '\0';
    std::istringstream stream(key.substr(bar + 1));
    stream >> counts[0] >> comma >> counts[1] >> comma >> counts[2];
    const auto swap_color = [](int color) {
      return color == 1 ? 2 : color == 2 ? 1 : 0;
    };
    const std::array<int, COLORS> swapped_counts = {
        counts[0], counts[2], counts[1]};
    const int swapped_observation = observation_index_.at(
        observation_key(swap_color(current), swapped_counts));
    const int repaint = action / COLORS;
    const int target = action % COLORS;
    const int swapped_action =
        swap_color(repaint) * COLORS + swap_color(target);
    return policy_variable(swapped_observation, swapped_action);
  }

  void add_policy_constraints(const std::string& initial_repaints) {
    for (int observation = 0; observation < observations(); ++observation) {
      std::vector<int> choices;
      for (int action = 0; action < ACTIONS; ++action) {
        choices.push_back(policy_variable(observation, action));
      }
      formula_.add(choices);
      for (int left = 0; left < ACTIONS; ++left) {
        for (int right = left + 1; right < ACTIONS; ++right) {
          formula_.add({-choices[left], -choices[right]});
        }
      }
      for (int action = 0; action < STOP; ++action) {
        const int target = action % COLORS;
        if (observation_counts_[observation][target] == 0) {
          formula_.add({-policy_variable(observation, action)});
        }
      }
    }

    std::vector<std::uint8_t> degree_present(max_degree_ + 1, 0);
    for (const Graph& graph : graphs_) {
      for (const auto& neighbors : graph.adjacency) {
        degree_present[neighbors.size()] = 1;
      }
    }
    for (int degree = 1; degree <= max_degree_; ++degree) {
      if (degree_present[degree] == 0) continue;
      const int observation = observation_index_.at(
          observation_key(0, {degree, 0, 0}));
      for (int repaint = 0; repaint < COLORS; ++repaint) {
        formula_.add({-policy_variable(observation, repaint * COLORS + 1)});
        formula_.add({-policy_variable(observation, repaint * COLORS + 2)});
      }
    }
    if (!initial_repaints.empty()) {
      if (static_cast<int>(initial_repaints.size()) != max_degree_) {
        throw std::runtime_error(
            "initial-repaints must have one digit per degree 1..max-degree");
      }
      for (int degree = 1; degree <= max_degree_; ++degree) {
        const char digit = initial_repaints[degree - 1];
        if (digit < '0' || digit >= '0' + COLORS) {
          throw std::runtime_error("initial-repaints digits must be in 0..2");
        }
        if (degree_present[degree] == 0) continue;
        const int observation = observation_index_.at(
            observation_key(0, {degree, 0, 0}));
        formula_.add({policy_variable(
            observation, (digit - '0') * COLORS)});
      }
    }
    if (color_symmetry_) add_color_symmetry();
  }

  void add_fixed_policy(const std::string& text) {
    if (text.empty()) return;
    std::istringstream stream(text);
    std::string token;
    int observation = 0;
    while (std::getline(stream, token, ',')) {
      if (observation >= observations()) {
        throw std::runtime_error("fixed-policy has too many actions");
      }
      const int action = std::stoi(token);
      if (action < 0 || action >= ACTIONS) {
        throw std::runtime_error("fixed-policy actions must be in 0..9");
      }
      formula_.add({policy_variable(observation, action)});
      ++observation;
    }
    if (observation != observations()) {
      throw std::runtime_error(
          "fixed-policy must have one action per observation");
    }
  }

  void add_required_rules(const std::string& text) {
    if (text.empty()) return;
    std::istringstream stream(text);
    std::string token;
    while (std::getline(stream, token, ',')) {
      const std::size_t separator = token.find(':');
      if (separator == std::string::npos ||
          token.find(':', separator + 1) != std::string::npos) {
        throw std::runtime_error(
            "require-rules must be observation:action pairs");
      }
      const int observation = std::stoi(token.substr(0, separator));
      const int action = std::stoi(token.substr(separator + 1));
      if (observation < 0 || observation >= observations() ||
          action < 0 || action >= ACTIONS) {
        throw std::runtime_error("require-rules entry is out of range");
      }
      formula_.add({policy_variable(observation, action)});
    }
  }

  void add_preferred_policy(const std::string& text) {
    if (text.empty()) return;
    std::istringstream stream(text);
    std::string token;
    int observation = 0;
    while (std::getline(stream, token, ',')) {
      if (observation >= observations()) {
        throw std::runtime_error("prefer-policy has too many actions");
      }
      const int action = std::stoi(token);
      if (action < 0 || action >= ACTIONS) {
        throw std::runtime_error("prefer-policy actions must be in 0..9");
      }
      formula_.solver.phase(policy_variable(observation, action));
      ++observation;
    }
    if (observation != observations()) {
      throw std::runtime_error(
          "prefer-policy must have one action per observation");
    }
  }

  void add_minimum_policy_distance(const std::string& text,
                                   int minimum_distance) {
    add_policy_distance_threshold(text, minimum_distance, true);
  }

  void add_maximum_policy_distance(const std::string& text,
                                   int maximum_distance) {
    if (maximum_distance < 0 || maximum_distance > observations()) {
      throw std::runtime_error(
          "maximum-distance must be in 0..observations");
    }
    if (maximum_distance == observations()) return;
    add_policy_distance_threshold(text, maximum_distance + 1, false);
  }

  void add_policy_distance_threshold(const std::string& text,
                                     int threshold_distance,
                                     bool require_threshold) {
    std::vector<int> center;
    std::istringstream stream(text);
    std::string token;
    while (std::getline(stream, token, ',')) {
      center.push_back(std::stoi(token));
    }
    if (static_cast<int>(center.size()) != observations()) {
      throw std::runtime_error(
          "distance-policy must have one action per observation");
    }
    for (int action : center) {
      if (action < 0 || action >= ACTIONS) {
        throw std::runtime_error(
            "distance-policy actions must be in 0..9");
      }
    }
    if (threshold_distance < 1 || threshold_distance > observations()) {
      throw std::runtime_error(
          "distance threshold must be in 1..observations");
    }

    // threshold[i][j] means that the first i observations differ from the
    // center in at least j actions.  Each row is exactly the recurrence
    //   T(i,j) = T(i-1,j) OR (T(i-1,j-1) AND deviation(i)).
    // A deviation literal is simply the negation of the center's selected
    // action because the policy constraints are exactly-one.
    std::vector<std::vector<int>> threshold(
        observations() + 1,
        std::vector<int>(threshold_distance + 1, 0));
    for (int count = 1; count <= threshold_distance; ++count) {
      for (int prefix = 1; prefix <= observations(); ++prefix) {
        if (count > prefix) continue;
        const int current = formula_.allocate();
        threshold[prefix][count] = current;
        const int previous_same = threshold[prefix - 1][count];
        const int deviation =
            -policy_variable(prefix - 1, center[prefix - 1]);
        if (count == 1) {
          if (previous_same != 0) {
            formula_.add({-previous_same, current});
            formula_.add({-current, previous_same, deviation});
          } else {
            formula_.add({-current, deviation});
          }
          formula_.add({-deviation, current});
          continue;
        }
        const int previous_less = threshold[prefix - 1][count - 1];
        if (previous_less == 0) {
          throw std::runtime_error(
              "internal minimum-distance recurrence failure");
        }
        if (previous_same != 0) {
          formula_.add({-previous_same, current});
          formula_.add({-current, previous_same, previous_less});
          formula_.add({-current, previous_same, deviation});
        } else {
          formula_.add({-current, previous_less});
          formula_.add({-current, deviation});
        }
        formula_.add({-previous_less, -deviation, current});
      }
    }
    const int final_threshold =
        threshold[observations()][threshold_distance];
    formula_.add({require_threshold ? final_threshold : -final_threshold});
  }

  void add_preferred_model(const std::string& path, int variable_limit) {
    if (path.empty()) return;
    std::ifstream input(path);
    if (!input) {
      throw std::runtime_error("cannot open preferred model: " + path);
    }
    std::vector<std::int8_t> seen(formula_.next_variable, 0);
    std::string line;
    int phases = 0;
    while (std::getline(input, line)) {
      if (line.empty() || line[0] != 'v') continue;
      std::istringstream stream(line.substr(1));
      int literal = 0;
      while (stream >> literal) {
        if (literal == 0) continue;
        const int variable = std::abs(literal);
        if (variable_limit > 0 && variable > variable_limit) continue;
        if (variable >= formula_.next_variable) {
          throw std::runtime_error(
              "preferred model variable exceeds formula size");
        }
        const std::int8_t value = literal > 0 ? 1 : -1;
        if (seen[variable] != 0 && seen[variable] != value) {
          throw std::runtime_error(
              "preferred model contains contradictory literals");
        }
        if (seen[variable] == 0) {
          formula_.solver.phase(literal);
          seen[variable] = value;
          ++phases;
        }
      }
    }
    if (phases == 0) {
      throw std::runtime_error("preferred model contains no v literals");
    }
    std::cout << "preferred-model-phases=" << phases << std::endl;
  }

  void add_color_symmetry() {
    int prefix_equal = 0;
    const int policy_bits = observations() * ACTIONS;
    for (int bit = 0; bit < policy_bits; ++bit) {
      const int observation = bit / ACTIONS;
      const int action = bit % ACTIONS;
      const int left = policy_variable(observation, action);
      const int right = swapped_policy_variable(observation, action);
      if (prefix_equal == 0) {
        formula_.add({-left, right});
      } else {
        formula_.add({-prefix_equal, -left, right});
      }
      if (bit == policy_bits - 1) break;
      const int equal = formula_.allocate();
      if (prefix_equal == 0) {
        formula_.add({-equal, -left, right});
        formula_.add({-equal, left, -right});
        formula_.add({left, right, equal});
        formula_.add({-left, -right, equal});
      } else {
        formula_.add({-equal, prefix_equal});
        formula_.add({-equal, -left, right});
        formula_.add({-equal, left, -right});
        formula_.add({-prefix_equal, left, right, equal});
        formula_.add({-prefix_equal, -left, -right, equal});
      }
      prefix_equal = equal;
    }
  }

  void load_blocks(const std::string& path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("cannot open blocks file: " + path);
    std::string line;
    int line_number = 0;
    bool verified_format = false;
    bool saw_clause = false;
    while (std::getline(input, line)) {
      ++line_number;
      if (line.empty()) continue;
      if (line == "c strict-stop-verified-v1") {
        verified_format = true;
        continue;
      }
      if (line[0] == 'c' || line[0] == 'p') continue;
      if (!verified_format) {
        throw std::runtime_error(
            "unverified policy blocks are disabled; " + path +
            " must start with 'c strict-stop-verified-v1'");
      }
      std::istringstream stream(line);
      std::vector<int> clause;
      int literal = 0;
      while (stream >> literal) {
        if (literal == 0) continue;
        if (std::abs(literal) > observations() * ACTIONS) {
          throw std::runtime_error("non-policy literal in " + path + ":" +
                                   std::to_string(line_number));
        }
        clause.push_back(literal);
      }
      if (clause.empty()) {
        throw std::runtime_error("empty block in " + path + ":" +
                                 std::to_string(line_number));
      }
      formula_.add(clause);
      ++imported_blocks_;
      saw_clause = true;
    }
    if (saw_clause && !verified_format) {
      throw std::runtime_error("unverified policy blocks in " + path);
    }
  }

  GraphVariables allocate_graph_variables(const Graph& graph) {
    GraphVariables variables;
    variables.rank_bits = 1;
    while ((1 << variables.rank_bits) < graph.physical) {
      ++variables.rank_bits;
    }
    if (success_on_target_return_) {
      variables.target_slot.assign(graph.n, -1);
      int slots = 0;
      for (int target : graph.weak_targets) {
        if (variables.target_slot[target] < 0) {
          variables.target_slot[target] = slots++;
        }
      }
      variables.before_union_reach.assign(
          slots, std::vector<int>(graph.physical));
      variables.before_union_rank.assign(
          slots, std::vector<std::vector<int>>(
                     graph.physical,
                     std::vector<int>(variables.rank_bits)));
      variables.before_start_reach.assign(
          graph.n, std::vector<int>(graph.physical));
      variables.after_reach.assign(
          graph.n, std::vector<int>(graph.physical));
      variables.after_rank.assign(
          graph.n, std::vector<std::vector<int>>(
                       graph.physical,
                       std::vector<int>(variables.rank_bits)));
      for (int state = 0; state < graph.physical; ++state) {
        for (int slot = 0; slot < slots; ++slot) {
          variables.before_union_reach[slot][state] = formula_.allocate();
          for (int bit = 0; bit < variables.rank_bits; ++bit) {
            variables.before_union_rank[slot][state][bit] =
                formula_.allocate();
          }
        }
        for (int start = 0; start < graph.n; ++start) {
          variables.before_start_reach[start][state] = formula_.allocate();
          variables.after_reach[start][state] = formula_.allocate();
          for (int bit = 0; bit < variables.rank_bits; ++bit) {
            variables.after_rank[start][state][bit] = formula_.allocate();
          }
        }
      }
      return variables;
    }
    variables.reach.resize(graph.physical);
    variables.rank.assign(graph.physical,
                          std::vector<int>(variables.rank_bits));
    if (!success_on_full_stop_anywhere_) {
      variables.return_reach.assign(graph.n,
                                    std::vector<int>(graph.physical));
    }
    variables.avoid_reach.assign(graph.n,
                                 std::vector<int>(graph.physical));
    for (int state = 0; state < graph.physical; ++state) {
      variables.reach[state] = formula_.allocate();
      for (int bit = 0; bit < variables.rank_bits; ++bit) {
        variables.rank[state][bit] = formula_.allocate();
      }
    }
    for (int start = 0; start < graph.n; ++start) {
      for (int state = 0; state < graph.physical; ++state) {
        if (!success_on_full_stop_anywhere_) {
          variables.return_reach[start][state] = formula_.allocate();
        }
        variables.avoid_reach[start][state] = formula_.allocate();
      }
    }
    return variables;
  }

  void add_strict_decrease(int reach, int choice,
                           const std::vector<int>& smaller,
                           const std::vector<int>& larger) {
    std::vector<int> terms;
    int prefix_equal = 0;
    for (int bit = static_cast<int>(smaller.size()) - 1; bit >= 0; --bit) {
      const int term = formula_.allocate();
      terms.push_back(term);
      if (prefix_equal == 0) {
        formula_.add({-term, -smaller[bit]});
        formula_.add({-term, larger[bit]});
        formula_.add({smaller[bit], -larger[bit], term});
      } else {
        formula_.add({-term, prefix_equal});
        formula_.add({-term, -smaller[bit]});
        formula_.add({-term, larger[bit]});
        formula_.add(
            {-prefix_equal, smaller[bit], -larger[bit], term});
      }
      if (bit == 0) continue;
      const int next_equal = formula_.allocate();
      if (prefix_equal == 0) {
        formula_.add({-next_equal, -smaller[bit], larger[bit]});
        formula_.add({-next_equal, smaller[bit], -larger[bit]});
        formula_.add({smaller[bit], larger[bit], next_equal});
        formula_.add({-smaller[bit], -larger[bit], next_equal});
      } else {
        formula_.add({-next_equal, prefix_equal});
        formula_.add({-next_equal, -smaller[bit], larger[bit]});
        formula_.add({-next_equal, smaller[bit], -larger[bit]});
        formula_.add(
            {-prefix_equal, smaller[bit], larger[bit], next_equal});
        formula_.add(
            {-prefix_equal, -smaller[bit], -larger[bit], next_equal});
      }
      prefix_equal = next_equal;
    }
    std::vector<int> comparison = {-reach, -choice};
    comparison.insert(comparison.end(), terms.begin(), terms.end());
    formula_.add(comparison);
  }

  void add_target_return_constraints(const Graph& graph,
                                     const GraphVariables& variables) {
    const std::vector<int> zero_colors(graph.n, 0);
    for (int start = 0; start < graph.n; ++start) {
      const int initial = encode_physical(graph, start, zero_colors);
      const int slot = variables.target_slot[graph.weak_targets[start]];
      formula_.add({variables.before_start_reach[start][initial]});
      formula_.add({variables.before_union_reach[slot][initial]});
    }
    for (int state = 0; state < graph.physical; ++state) {
      for (int start = 0; start < graph.n; ++start) {
        const int slot = variables.target_slot[graph.weak_targets[start]];
        formula_.add({-variables.before_start_reach[start][state],
                      variables.before_union_reach[slot][state]});
      }
    }

    for (int state = 0; state < graph.physical; ++state) {
      const Physical physical = decode_physical(graph, state);
      const int observation = observation_of(graph, physical);
      for (int action = 0; action < ACTIONS; ++action) {
        const int choice = policy_variable(observation, action);
        const std::vector<int> next_states =
            successors(graph, physical, action);
        if (next_states.empty()) {
          for (int start = 0; start < graph.n; ++start) {
            formula_.add(
                {-variables.before_start_reach[start][state], -choice});
            formula_.add({-variables.after_reach[start][state], -choice});
          }
          continue;
        }

        for (int target = 0; target < graph.n; ++target) {
          const int slot = variables.target_slot[target];
          if (slot < 0) continue;
          for (int next : next_states) {
            const int next_position = next / graph.colorings;
            if (next_position == target) continue;
            formula_.add(
                {-variables.before_union_reach[slot][state], -choice,
                 variables.before_union_reach[slot][next]});
            add_strict_decrease(
                variables.before_union_reach[slot][state], choice,
                variables.before_union_rank[slot][next],
                variables.before_union_rank[slot][state]);
          }
        }

        for (int start = 0; start < graph.n; ++start) {
          for (int next : next_states) {
            const int next_position = next / graph.colorings;
            if (next_position == graph.weak_targets[start]) {
              formula_.add(
                  {-variables.before_start_reach[start][state], -choice,
                   variables.after_reach[start][next]});
            } else {
              formula_.add(
                  {-variables.before_start_reach[start][state], -choice,
                   variables.before_start_reach[start][next]});
            }
            if (next_position == start) continue;
            formula_.add({-variables.after_reach[start][state], -choice,
                          variables.after_reach[start][next]});
            add_strict_decrease(
                variables.after_reach[start][state], choice,
                variables.after_rank[start][next],
                variables.after_rank[start][state]);
          }
        }
      }
    }
  }

  void add_graph_constraints(const Graph& graph,
                             const GraphVariables& variables) {
    if (success_on_target_return_) {
      add_target_return_constraints(graph, variables);
      return;
    }
    const std::vector<int> zero_colors(graph.n, 0);
    std::vector<std::vector<int>> reach_incoming;
    std::vector<std::uint8_t> reach_initial;
    if (exact_reach_support_) {
      reach_incoming.resize(graph.physical);
      reach_initial.assign(graph.physical, 0);
    }
    for (int start = 0; start < graph.n; ++start) {
      const int initial = encode_physical(graph, start, zero_colors);
      formula_.add({variables.reach[initial]});
      if (exact_reach_support_) reach_initial[initial] = 1;
      if (!success_on_full_stop_anywhere_) {
        formula_.add({variables.return_reach[start][initial]});
      }
      if (graph.weak_targets.empty()) {
        for (int missing = 0; missing < graph.n; ++missing) {
          if (missing != start) {
            formula_.add({variables.avoid_reach[missing][initial]});
          }
        }
      } else {
        formula_.add({variables.avoid_reach[start][initial]});
      }
    }

    // If executions from two different starts reach the same position and
    // coloring, the adversary can synchronize their futures.  They then
    // either run forever or stop at the same vertex, which cannot be the
    // required return vertex for both starts.  This is a redundant but strong
    // consequence of the full terminal constraints.
    if (!success_on_full_stop_anywhere_) {
      for (int state = 0; state < graph.physical; ++state) {
        for (int first = 0; first < graph.n; ++first) {
          for (int second = first + 1; second < graph.n; ++second) {
            formula_.add({-variables.return_reach[first][state],
                          -variables.return_reach[second][state]});
          }
        }
      }
    }

    for (int state = 0; state < graph.physical; ++state) {
      const Physical physical = decode_physical(graph, state);
      const int observation = observation_of(graph, physical);
      for (int action = 0; action < ACTIONS; ++action) {
        const int choice = policy_variable(observation, action);
        const std::vector<int> next_states =
            successors(graph, physical, action);
        if (next_states.empty()) {
          if (!success_on_full_stop_anywhere_) {
            for (int start = 0; start < graph.n; ++start) {
              if (physical.position != start) {
                formula_.add(
                    {-variables.return_reach[start][state], -choice});
              }
            }
          }
          if (graph.weak_targets.empty()) {
            for (int missing = 0; missing < graph.n; ++missing) {
              formula_.add(
                  {-variables.avoid_reach[missing][state], -choice});
            }
          } else {
            for (int start = 0; start < graph.n; ++start) {
              formula_.add(
                  {-variables.avoid_reach[start][state], -choice});
            }
          }
          continue;
        }
        for (int next : next_states) {
          formula_.add({-variables.reach[state], -choice,
                        variables.reach[next]});
          if (exact_reach_support_) {
            const int support = formula_.allocate();
            formula_.add({-support, variables.reach[state]});
            formula_.add({-support, choice});
            formula_.add(
                {-variables.reach[state], -choice, support});
            reach_incoming[next].push_back(support);
          }
          add_strict_decrease(variables.reach[state], choice,
                              variables.rank[next], variables.rank[state]);
          if (!success_on_full_stop_anywhere_) {
            for (int start = 0; start < graph.n; ++start) {
              formula_.add({-variables.return_reach[start][state], -choice,
                            variables.return_reach[start][next]});
            }
          }
          const int next_position = next / graph.colorings;
          if (graph.weak_targets.empty()) {
            for (int missing = 0; missing < graph.n; ++missing) {
              if (next_position == missing) continue;
              formula_.add({-variables.avoid_reach[missing][state], -choice,
                            variables.avoid_reach[missing][next]});
            }
          } else {
            for (int start = 0; start < graph.n; ++start) {
              if (next_position == graph.weak_targets[start]) continue;
              formula_.add({-variables.avoid_reach[start][state], -choice,
                            variables.avoid_reach[start][next]});
            }
          }
        }
      }
    }
    if (exact_reach_support_) {
      for (int state = 0; state < graph.physical; ++state) {
        if (reach_initial[state] != 0) continue;
        std::vector<int> supported = {-variables.reach[state]};
        supported.insert(supported.end(), reach_incoming[state].begin(),
                         reach_incoming[state].end());
        formula_.add(supported);
      }
    }
  }

  std::vector<Graph> graphs_;
  bool canonical_stops_ = false;
  bool color_symmetry_ = true;
  bool success_on_target_return_ = false;
  bool success_on_full_stop_anywhere_ = false;
  bool exact_reach_support_ = false;
  int max_degree_ = 0;
  Formula formula_;
  std::vector<std::string> observation_keys_;
  std::unordered_map<std::string, int> observation_index_;
  std::vector<std::array<int, COLORS>> observation_counts_;
  std::vector<GraphVariables> graph_variables_;
  int imported_blocks_ = 0;
};

}  // namespace

int main(int argc, char** argv) {
  try {
    std::vector<std::string> graph6s;
    std::vector<std::string> weak_targets_text;
    std::vector<std::string> blocks;
    std::vector<std::string> weak_blocks;
    std::vector<std::string> target_return_blocks;
    std::string initial_repaints;
    std::string fixed_policy;
    std::string required_rules;
    std::string preferred_policy;
    std::string preferred_model;
    std::string scan_policy;
    std::string scan_hit_rules;
    std::string cnf_path;
    std::string proof_path;
    std::string model_out;
    int scan_distance = -1;
    int preferred_model_limit = 0;
    int minimum_distance = 0;
    std::vector<std::string> minimum_distance_policies;
    int maximum_distance = -1;
    std::vector<std::string> maximum_distance_policies;
    bool canonical_stops = false;
    bool color_symmetry = true;
    bool generate_only = false;
    bool success_on_target_return = false;
    bool success_on_full_stop_anywhere = false;
    bool exact_reach_support = false;
    for (int index = 1; index < argc; ++index) {
      const std::string argument = argv[index];
      if (argument == "--graph6" && index + 1 < argc) {
        graph6s.push_back(argv[++index]);
      } else if (argument == "--graphs-file" && index + 1 < argc) {
        const std::string path = argv[++index];
        std::ifstream input(path);
        if (!input) {
          throw std::runtime_error("cannot read graph6 file: " + path);
        }
        std::string line;
        while (std::getline(input, line)) {
          if (!line.empty() && line.back() == '\r') line.pop_back();
          if (line.empty()) continue;
          graph6s.push_back(line);
        }
      } else if (argument == "--weak-targets" && index + 1 < argc) {
        weak_targets_text.push_back(argv[++index]);
      } else if (argument == "--blocks" && index + 1 < argc) {
        blocks.push_back(argv[++index]);
      } else if (argument == "--weak-blocks" && index + 1 < argc) {
        weak_blocks.push_back(argv[++index]);
      } else if (argument == "--target-return-blocks" && index + 1 < argc) {
        target_return_blocks.push_back(argv[++index]);
      } else if (argument == "--initial-repaints" && index + 1 < argc) {
        initial_repaints = argv[++index];
      } else if (argument == "--fixed-policy" && index + 1 < argc) {
        fixed_policy = argv[++index];
      } else if (argument == "--require-rules" && index + 1 < argc) {
        required_rules = argv[++index];
      } else if (argument == "--prefer-policy" && index + 1 < argc) {
        preferred_policy = argv[++index];
      } else if (argument == "--prefer-model" && index + 1 < argc) {
        preferred_model = argv[++index];
      } else if (argument == "--prefer-model-limit" && index + 1 < argc) {
        preferred_model_limit = std::stoi(argv[++index]);
      } else if (argument == "--minimum-distance-policy" &&
                 index + 1 < argc) {
        minimum_distance_policies.push_back(argv[++index]);
      } else if (argument == "--minimum-distance" &&
                 index + 1 < argc) {
        minimum_distance = std::stoi(argv[++index]);
      } else if (argument == "--maximum-distance-policy" &&
                 index + 1 < argc) {
        maximum_distance_policies.push_back(argv[++index]);
      } else if (argument == "--maximum-distance" &&
                 index + 1 < argc) {
        maximum_distance = std::stoi(argv[++index]);
      } else if (argument == "--scan-policy" && index + 1 < argc) {
        scan_policy = argv[++index];
      } else if (argument == "--scan-distance" && index + 1 < argc) {
        scan_distance = std::stoi(argv[++index]);
      } else if (argument == "--scan-hit-rules" && index + 1 < argc) {
        scan_hit_rules = argv[++index];
      } else if (argument == "--write-cnf" && index + 1 < argc) {
        cnf_path = argv[++index];
      } else if (argument == "--write-proof" && index + 1 < argc) {
        proof_path = argv[++index];
      } else if (argument == "--write-model" && index + 1 < argc) {
        model_out = argv[++index];
      } else if (argument == "--generate-only") {
        generate_only = true;
      } else if (argument == "--canonicalize-stop-actions") {
        canonical_stops = true;
      } else if (argument == "--no-color-symmetry") {
        color_symmetry = false;
      } else if (argument == "--success-on-target-return") {
        success_on_target_return = true;
      } else if (argument == "--success-on-full-stop-anywhere") {
        success_on_full_stop_anywhere = true;
      } else if (argument == "--exact-reach-support") {
        exact_reach_support = true;
      } else {
        throw std::runtime_error("unknown or incomplete argument: " + argument);
      }
    }
    if (graph6s.empty()) {
      throw std::runtime_error("at least one --graph6 is required");
    }
    if (preferred_model_limit < 0 ||
        (preferred_model_limit > 0 && preferred_model.empty())) {
      throw std::runtime_error(
          "prefer-model-limit must be nonnegative and requires a model");
    }
    if ((minimum_distance == 0) != minimum_distance_policies.empty()) {
      throw std::runtime_error(
          "minimum-distance and minimum-distance-policy must be used "
          "together");
    }
    if ((maximum_distance < 0) != maximum_distance_policies.empty()) {
      throw std::runtime_error(
          "maximum-distance and maximum-distance-policy must be used "
          "together");
    }
    if (success_on_target_return && success_on_full_stop_anywhere) {
      throw std::runtime_error("choose only one weakened success mode");
    }
    if (!initial_repaints.empty() && color_symmetry) {
      // The 1<->2 color swap sends a fixed initial repaint tuple to its
      // swapped tuple.  A global lex leader is therefore not a symmetry of a
      // non-invariant tuple branch and could delete its only solutions.
      color_symmetry = false;
      std::cout << "note=color-symmetry-disabled-for-fixed-tuple\n";
    }
    std::vector<Graph> graphs;
    graphs.reserve(graph6s.size());
    for (const std::string& graph6 : graph6s) {
      graphs.push_back(parse_graph6(graph6));
    }
    if (!weak_targets_text.empty()) {
      if (success_on_full_stop_anywhere) {
        throw std::runtime_error(
            "full-stop-anywhere does not accept weak targets");
      }
      if (!blocks.empty()) {
        throw std::runtime_error(
            "full-exploration blocks cannot be used with weak targets");
      }
      if (success_on_target_return) {
        if (!weak_blocks.empty()) {
          throw std::runtime_error(
              "ordinary weak blocks cannot be used for automatic return");
        }
        blocks.insert(blocks.end(), target_return_blocks.begin(),
                      target_return_blocks.end());
      } else {
        if (!target_return_blocks.empty()) {
          throw std::runtime_error(
              "target-return blocks require --success-on-target-return");
        }
        blocks.insert(blocks.end(), weak_blocks.begin(), weak_blocks.end());
      }
      if (weak_targets_text.size() != graphs.size()) {
        throw std::runtime_error(
            "--weak-targets must be repeated once per graph");
      }
      for (std::size_t index = 0; index < graphs.size(); ++index) {
        std::istringstream stream(weak_targets_text[index]);
        std::string token;
        while (std::getline(stream, token, ',')) {
          const int target = std::stoi(token) - 1;
          if (target < 0 || target >= graphs[index].n) {
            throw std::runtime_error("weak target is out of range");
          }
          graphs[index].weak_targets.push_back(target);
        }
        if (static_cast<int>(graphs[index].weak_targets.size()) !=
            graphs[index].n) {
          throw std::runtime_error(
              "weak-targets must have one entry per start vertex");
        }
        for (int start = 0; start < graphs[index].n; ++start) {
          if (graphs[index].weak_targets[start] == start) {
            throw std::runtime_error(
                "a weak target must differ from its start vertex");
          }
        }
      }
    } else if (!weak_blocks.empty() || !target_return_blocks.empty() ||
               success_on_target_return) {
      throw std::runtime_error(
          "weak modes and blocks require targets for every graph");
    }
    ExactFamilyEncoding encoding(std::move(graphs), canonical_stops,
                                 color_symmetry, success_on_target_return,
                                 success_on_full_stop_anywhere,
                                 exact_reach_support);
    if (!scan_policy.empty() || scan_distance >= 0) {
      if (scan_policy.empty() || scan_distance < 0) {
        throw std::runtime_error(
            "--scan-policy and --scan-distance must be used together");
      }
      std::vector<int> policy;
      std::istringstream stream(scan_policy);
      std::string token;
      while (std::getline(stream, token, ',')) {
        policy.push_back(std::stoi(token));
      }
      encoding.scan_policy_neighborhood(
          policy, scan_distance, required_rules, scan_hit_rules);
      return 0;
    }
    encoding.enable_proof_output(proof_path);
    encoding.enable_cnf_output(cnf_path);
    encoding.build(blocks, initial_repaints, fixed_policy, required_rules,
                   preferred_policy, preferred_model,
                   preferred_model_limit, minimum_distance_policies,
                   minimum_distance, maximum_distance_policies,
                   maximum_distance);
    encoding.print_summary(encoding.imported_blocks());
    if (generate_only) {
      if (cnf_path.empty()) {
        throw std::runtime_error("--generate-only requires --write-cnf");
      }
      std::cout << "exact=CNF-WRITTEN\n";
      return 0;
    }
    const int result = encoding.solve();
    if (result == 20) {
      std::cout << "exact=UNSAT\n";
      return 20;
    }
    if (result != 10) {
      std::cout << "exact=UNKNOWN\n";
      return 1;
    }
    const std::vector<int> policy = encoding.policy();
    std::string failure;
    if (!encoding.verify_policy(policy, &failure)) {
      throw std::runtime_error("SAT model failed independent check: " + failure);
    }
    encoding.write_model(model_out);
    std::cout << "exact=SAT verified=1\n";
    encoding.print_policy(policy);
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << '\n';
    return 2;
  }
}
