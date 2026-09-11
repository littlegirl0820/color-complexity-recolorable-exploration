#pragma once

#ifndef CACTUS_GENERATE_ONLY_STUB
#error "This stub is only for deterministic CNF generation"
#endif

namespace CaDiCaL {

class Solver {
 public:
  void add(int) {}
  void freeze(int) {}
  void phase(int) {}
  void set(const char*, int) {}
  bool trace_proof(const char*) { return false; }
  int solve() { return 0; }
  int val(int) { return 0; }
};

}  // namespace CaDiCaL
