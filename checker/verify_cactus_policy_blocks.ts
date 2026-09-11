import {
  createReadStream,
  existsSync,
  readFileSync,
  writeFileSync
} from "node:fs";
import { createInterface } from "node:readline";

const COLORS = 3;
const ACTIONS = 10;
const STOP = 9;
const OBSERVATIONS = 57;
const MAX_VERTICES = 12;

type Observation = {
  current: number;
  counts: [number, number, number];
};

type Transition = {
  nextPhysical: readonly number[];
};

type Witness = {
  graph6: string;
  start: number;
  kind: "BAD_STOP" | "MISSING_TARGET" | "CYCLE";
  reachable: number;
};

type GraphDomainCertificate = {
  graph6: string;
  start: number;
  domain: Uint16Array;
};

const observations: Observation[] = [];
const observationByKey = new Map<string, number>();

function observationKey(
  current: number,
  counts: readonly number[]
): string {
  return `${current}|${counts[0]},${counts[1]},${counts[2]}`;
}

function initializeObservations(): void {
  for (let current = 0; current < COLORS; current += 1) {
    for (let degree = 1; degree <= 3; degree += 1) {
      for (let color0 = 0; color0 <= degree; color0 += 1) {
        for (
          let color1 = 0;
          color1 <= degree - color0;
          color1 += 1
        ) {
          const counts: [number, number, number] = [
            color0,
            color1,
            degree - color0 - color1
          ];
          const index = observations.length;
          observations.push({ current, counts });
          observationByKey.set(observationKey(current, counts), index);
        }
      }
    }
  }
  if (observations.length !== OBSERVATIONS) {
    throw new Error(`expected 57 observations, got ${observations.length}`);
  }
}

class Graph {
  readonly graph6: string;
  readonly n: number;
  readonly adjacency: number[][];
  readonly colorings: number;
  readonly masks: number;
  readonly observationCache: Int8Array;
  readonly transitionCache = new Map<number, readonly number[]>();

  constructor(graph6: string) {
    this.graph6 = graph6;
    if (graph6.length === 0 || graph6.charCodeAt(0) === 126) {
      throw new Error("only short standard graph6 strings are supported");
    }
    this.n = graph6.charCodeAt(0) - 63;
    if (this.n < 2 || this.n > MAX_VERTICES) {
      throw new Error(`graph order is outside 2..12: ${graph6}`);
    }
    const requiredBits = this.n * (this.n - 1) / 2;
    const bits: number[] = [];
    for (
      let index = 1;
      index < graph6.length && bits.length < requiredBits;
      index += 1
    ) {
      const value = graph6.charCodeAt(index) - 63;
      if (value < 0 || value > 63) {
        throw new Error(`invalid graph6 character in ${graph6}`);
      }
      for (let bit = 5; bit >= 0 && bits.length < requiredBits; bit -= 1) {
        bits.push((value >> bit) & 1);
      }
    }
    if (bits.length !== requiredBits) {
      throw new Error(`graph6 payload is too short: ${graph6}`);
    }

    this.adjacency = Array.from({ length: this.n }, () => []);
    let cursor = 0;
    for (let right = 1; right < this.n; right += 1) {
      for (let left = 0; left < right; left += 1) {
        if (bits[cursor] !== 0) {
          this.adjacency[left].push(right);
          this.adjacency[right].push(left);
        }
        cursor += 1;
      }
    }
    const reached = new Set<number>([0]);
    const queue = [0];
    for (let head = 0; head < queue.length; head += 1) {
      for (const next of this.adjacency[queue[head]]) {
        if (reached.has(next)) continue;
        reached.add(next);
        queue.push(next);
      }
    }
    if (reached.size !== this.n) {
      throw new Error(`candidate graph is disconnected: ${graph6}`);
    }
    for (const neighbors of this.adjacency) {
      if (neighbors.length < 1 || neighbors.length > 3) {
        throw new Error(`candidate graph is not subcubic: ${graph6}`);
      }
    }

    this.colorings = COLORS ** this.n;
    this.masks = 1 << this.n;
    this.observationCache = new Int8Array(this.n * this.colorings);
    this.observationCache.fill(-1);
  }

  private colorsOf(physical: number): number[] {
    let colorCode = physical % this.colorings;
    const colors = Array<number>(this.n).fill(0);
    for (let vertex = 0; vertex < this.n; vertex += 1) {
      colors[vertex] = colorCode % COLORS;
      colorCode = Math.floor(colorCode / COLORS);
    }
    return colors;
  }

  positionOf(physical: number): number {
    return Math.floor(physical / this.colorings);
  }

  observationOf(physical: number): number {
    const cached = this.observationCache[physical];
    if (cached >= 0) return cached;
    const position = this.positionOf(physical);
    const colors = this.colorsOf(physical);
    const counts = [0, 0, 0];
    for (const neighbor of this.adjacency[position]) {
      counts[colors[neighbor]] += 1;
    }
    const result = observationByKey.get(
      observationKey(colors[position], counts)
    );
    if (result === undefined) {
      throw new Error("physical state has an unknown observation");
    }
    this.observationCache[physical] = result;
    return result;
  }

  transitionOf(physical: number, action: number): Transition {
    if (action === STOP) return { nextPhysical: [] };
    const cacheKey = physical * ACTIONS + action;
    const cached = this.transitionCache.get(cacheKey);
    if (cached !== undefined) {
      return { nextPhysical: cached };
    }
    const position = this.positionOf(physical);
    const colors = this.colorsOf(physical);
    const repaint = Math.floor(action / COLORS);
    const target = action % COLORS;
    const repainted = [...colors];
    repainted[position] = repaint;
    let repaintedCode = 0;
    let multiplier = 1;
    for (let vertex = 0; vertex < this.n; vertex += 1) {
      repaintedCode += repainted[vertex] * multiplier;
      multiplier *= COLORS;
    }
    const nextPhysical: number[] = [];
    for (const neighbor of this.adjacency[position]) {
      if (colors[neighbor] !== target) continue;
      nextPhysical.push(neighbor * this.colorings + repaintedCode);
    }
    this.transitionCache.set(cacheKey, nextPhysical);
    return { nextPhysical };
  }

  fullState(physical: number, visited: number): number {
    return physical * this.masks + visited;
  }

  nextState(
    state: number,
    nextPhysical: number
  ): number {
    const visited = state % this.masks;
    const nextVisited = visited | (1 << this.positionOf(nextPhysical));
    return this.fullState(nextPhysical, nextVisited);
  }
}

function parsePartialPolicy(clause: readonly number[]): Int8Array | "TAUTOLOGY" {
  const partial = new Int8Array(OBSERVATIONS);
  partial.fill(-1);
  for (const literal of clause) {
    if (!Number.isInteger(literal) || literal >= 0 ||
        -literal > OBSERVATIONS * ACTIONS) {
      throw new Error("graph witness clauses must contain negative policy literals");
    }
    const zeroBased = -literal - 1;
    const observation = Math.floor(zeroBased / ACTIONS);
    const action = zeroBased % ACTIONS;
    if (partial[observation] >= 0 &&
        partial[observation] !== action) {
      return "TAUTOLOGY";
    }
    partial[observation] = action;
  }
  return partial;
}

function strictActionIsValid(observation: number, action: number): boolean {
  return action === STOP ||
    observations[observation].counts[action % COLORS] !== 0;
}

function strictActionMask(observation: number): number {
  let result = 1 << STOP;
  for (let action = 0; action < STOP; action += 1) {
    if (strictActionIsValid(observation, action)) {
      result |= 1 << action;
    }
  }
  return result;
}

function traceDomain(clause: readonly number[]): Uint16Array | "TAUTOLOGY" {
  const partial = parsePartialPolicy(clause);
  if (partial === "TAUTOLOGY") return partial;
  const domain = new Uint16Array(OBSERVATIONS);
  for (let observation = 0;
       observation < OBSERVATIONS; observation += 1) {
    domain[observation] = strictActionMask(observation);
    if (partial[observation] >= 0) {
      domain[observation] = 1 << partial[observation];
    }
  }
  return domain;
}

function domainForcesFailure(
  graph: Graph,
  domain: Uint16Array,
  start: number
): boolean {
  const fullStates = graph.n * graph.colorings * graph.masks;
  const winning = new Uint8Array(fullStates);
  const remaining = new Uint8Array(fullStates * STOP);
  const reverseCounts = new Uint32Array(fullStates);
  const queue: number[] = [];
  for (let state = 0; state < fullStates; state += 1) {
    const physical = Math.floor(state / graph.masks);
    const visited = state % graph.masks;
    const observation = graph.observationOf(physical);
    const actions = domain[observation];
    if ((actions & (1 << STOP)) !== 0 &&
        graph.positionOf(physical) === start &&
        visited === graph.masks - 1) {
      winning[state] = 1;
      queue.push(state);
    }
    for (let action = 0; action < STOP; action += 1) {
      if ((actions & (1 << action)) === 0) continue;
      const successors = graph.transitionOf(
        physical,
        action
      ).nextPhysical;
      if (successors.length === 0) continue;
      remaining[state * STOP + action] = successors.length;
      for (const nextPhysical of successors) {
        reverseCounts[graph.nextState(state, nextPhysical)] += 1;
      }
    }
  }

  const reverseOffsets = new Uint32Array(fullStates + 1);
  for (let state = 0; state < fullStates; state += 1) {
    reverseOffsets[state + 1] =
      reverseOffsets[state] + reverseCounts[state];
  }
  const reverseActions = new Uint32Array(
    reverseOffsets[fullStates]
  );
  const reverseCursors = new Uint32Array(reverseOffsets);
  for (let state = 0; state < fullStates; state += 1) {
    const physical = Math.floor(state / graph.masks);
    const observation = graph.observationOf(physical);
    const actions = domain[observation];
    for (let action = 0; action < STOP; action += 1) {
      if ((actions & (1 << action)) === 0) continue;
      const successors = graph.transitionOf(
        physical,
        action
      ).nextPhysical;
      if (successors.length === 0) continue;
      const actionKey = state * STOP + action;
      for (const nextPhysical of successors) {
        const next = graph.nextState(state, nextPhysical);
        reverseActions[reverseCursors[next]] = actionKey;
        reverseCursors[next] += 1;
      }
    }
  }
  for (let head = 0; head < queue.length; head += 1) {
    const won = queue[head];
    for (let offset = reverseOffsets[won];
         offset < reverseOffsets[won + 1]; offset += 1) {
      const actionKey = reverseActions[offset];
      if (remaining[actionKey] === 0) continue;
      remaining[actionKey] -= 1;
      if (remaining[actionKey] !== 0) continue;
      const predecessor = Math.floor(actionKey / STOP);
      if (winning[predecessor] !== 0) continue;
      winning[predecessor] = 1;
      queue.push(predecessor);
    }
  }
  const initial = graph.fullState(
    start * graph.colorings,
    1 << start
  );
  return winning[initial] === 0;
}

function domainForcesFailureNaive(
  graph: Graph,
  domain: Uint16Array,
  start: number
): boolean {
  const fullStates = graph.n * graph.colorings * graph.masks;
  const winning = new Uint8Array(fullStates);
  let changed = true;
  while (changed) {
    changed = false;
    for (let state = 0; state < fullStates; state += 1) {
      if (winning[state] !== 0) continue;
      const physical = Math.floor(state / graph.masks);
      const visited = state % graph.masks;
      const observation = graph.observationOf(physical);
      const actions = domain[observation];
      let stateWins =
        (actions & (1 << STOP)) !== 0 &&
        graph.positionOf(physical) === start &&
        visited === graph.masks - 1;
      for (let action = 0;
           action < STOP && !stateWins; action += 1) {
        if ((actions & (1 << action)) === 0) continue;
        const successors = graph.transitionOf(
          physical,
          action
        ).nextPhysical;
        if (successors.length === 0) continue;
        stateWins = successors.every((nextPhysical) =>
          winning[graph.nextState(state, nextPhysical)] !== 0
        );
      }
      if (!stateWins) continue;
      winning[state] = 1;
      changed = true;
    }
  }
  const initial = graph.fullState(
    start * graph.colorings,
    1 << start
  );
  return winning[initial] === 0;
}

function runDomainSelfTest(
  graphs: readonly Graph[],
  tests: number
): void {
  const small = graphs.filter((graph) => graph.n <= 4);
  if (small.length === 0) {
    throw new Error("domain self-test requires a graph of order at most 4");
  }
  let randomState = 0x47504631;
  const nextRandom = (): number => {
    randomState =
      (Math.imul(randomState, 1664525) + 1013904223) >>> 0;
    return randomState;
  };
  for (let test = 0; test < tests; test += 1) {
    const graph = small[test % small.length];
    const domain = new Uint16Array(OBSERVATIONS);
    for (let observation = 0;
         observation < OBSERVATIONS; observation += 1) {
      const ceiling = strictActionMask(observation);
      let mask = 0;
      for (let action = 0; action < ACTIONS; action += 1) {
        const bit = 1 << action;
        if ((ceiling & bit) !== 0 && (nextRandom() & 3) !== 0) {
          mask |= bit;
        }
      }
      if (mask === 0) {
        const actions: number[] = [];
        for (let action = 0; action < ACTIONS; action += 1) {
          if ((ceiling & (1 << action)) !== 0) actions.push(action);
        }
        mask = 1 << actions[nextRandom() % actions.length];
      }
      domain[observation] = mask;
    }
    const start = test % graph.n;
    const fast = domainForcesFailure(graph, domain, start);
    const reference = domainForcesFailureNaive(graph, domain, start);
    if (fast !== reference) {
      throw new Error(
        `graph domain self-test mismatch at test ${test}`
      );
    }
  }
  console.log(
    `graph-domain-self-test=PASS tests=${tests} seed=1196443185`
  );
}

type DomainGeneralization = {
  initialComplement: number;
  finalComplement: number;
  addedActions: number;
  oracleCalls: number;
};

function generalizeTraceDomain(
  graph: Graph,
  clause: readonly number[],
  start: number
): DomainGeneralization | undefined {
  const domain = traceDomain(clause);
  if (domain === "TAUTOLOGY") return undefined;
  if (!domainForcesFailure(graph, domain, start)) {
    throw new Error(
      "trace domain is not losing under the graph domain oracle"
    );
  }
  let oracleCalls = 1;
  let initialComplement = 0;
  for (let observation = 0;
       observation < OBSERVATIONS; observation += 1) {
    const ceiling = strictActionMask(observation);
    initialComplement += popcount(ceiling & ~domain[observation]);
  }
  const result = new Uint16Array(domain);
  for (let observation = 0;
       observation < OBSERVATIONS; observation += 1) {
    const ceiling = strictActionMask(observation);
    const missing = ceiling & ~result[observation];
    if (missing === 0) continue;
    const wholeTrial = new Uint16Array(result);
    wholeTrial[observation] |= missing;
    oracleCalls += 1;
    if (domainForcesFailure(graph, wholeTrial, start)) {
      result[observation] = wholeTrial[observation];
      continue;
    }
    for (let action = 0; action < ACTIONS; action += 1) {
      const bit = 1 << action;
      if ((missing & bit) === 0) continue;
      const trial = new Uint16Array(result);
      trial[observation] |= bit;
      oracleCalls += 1;
      if (domainForcesFailure(graph, trial, start)) {
        result[observation] = trial[observation];
      }
    }
  }
  let finalComplement = 0;
  for (let observation = 0;
       observation < OBSERVATIONS; observation += 1) {
    finalComplement += popcount(
      strictActionMask(observation) & ~result[observation]
    );
  }
  return {
    initialComplement,
    finalComplement,
    addedActions: initialComplement - finalComplement,
    oracleCalls
  };
}

function popcount(value: number): number {
  let result = 0;
  let remaining = value & ((1 << ACTIONS) - 1);
  while (remaining !== 0) {
    remaining &= remaining - 1;
    result += 1;
  }
  return result;
}

function expectedDomainBlock(domain: Uint16Array): number[] {
  const result: number[] = [];
  for (let observation = 0;
       observation < OBSERVATIONS; observation += 1) {
    const complement =
      strictActionMask(observation) & ~domain[observation];
    for (let action = 0; action < ACTIONS; action += 1) {
      if ((complement & (1 << action)) !== 0) {
        result.push(observation * ACTIONS + action + 1);
      }
    }
  }
  return result;
}

function sameNumbers(
  left: readonly number[],
  right: readonly number[]
): boolean {
  return left.length === right.length &&
    left.every((value, index) => value === right[index]);
}

function graphWitness(
  graph: Graph,
  partial: Int8Array,
  start: number
): Witness | undefined {
  const initial = graph.fullState(
    start * graph.colorings,
    1 << start
  );
  const stateIndex = new Map<number, number>([[initial, 0]]);
  const states = [initial];
  for (let head = 0; head < states.length; head += 1) {
    const state = states[head];
    const physical = Math.floor(state / graph.masks);
    const visited = state % graph.masks;
    const observation = graph.observationOf(physical);
    const action = partial[observation];
    if (action < 0) continue;
    if (!strictActionIsValid(observation, action)) {
      return {
        graph6: graph.graph6,
        start,
        kind: "MISSING_TARGET",
        reachable: states.length
      };
    }
    if (action === STOP) {
      if (graph.positionOf(physical) !== start ||
          visited !== graph.masks - 1) {
        return {
          graph6: graph.graph6,
          start,
          kind: "BAD_STOP",
          reachable: states.length
        };
      }
      continue;
    }
    const transition = graph.transitionOf(physical, action);
    if (transition.nextPhysical.length === 0) {
      return {
        graph6: graph.graph6,
        start,
        kind: "MISSING_TARGET",
        reachable: states.length
      };
    }
    for (const nextPhysical of transition.nextPhysical) {
      const next = graph.nextState(state, nextPhysical);
      if (stateIndex.has(next)) continue;
      stateIndex.set(next, states.length);
      states.push(next);
    }
  }

  const indegree = new Uint32Array(states.length);
  const edges: number[][] = Array.from({ length: states.length }, () => []);
  for (let index = 0; index < states.length; index += 1) {
    const state = states[index];
    const physical = Math.floor(state / graph.masks);
    const observation = graph.observationOf(physical);
    const action = partial[observation];
    if (action < 0 || action === STOP) continue;
    for (const nextPhysical of graph.transitionOf(
      physical,
      action
    ).nextPhysical) {
      const nextState = graph.nextState(state, nextPhysical);
      const next = stateIndex.get(nextState);
      if (next === undefined) {
        throw new Error("reachable successor is missing from the state set");
      }
      edges[index].push(next);
      indegree[next] += 1;
    }
  }
  const sources: number[] = [];
  for (let index = 0; index < states.length; index += 1) {
    if (indegree[index] === 0) sources.push(index);
  }
  let removed = 0;
  for (let head = 0; head < sources.length; head += 1) {
    const index = sources[head];
    removed += 1;
    for (const next of edges[index]) {
      indegree[next] -= 1;
      if (indegree[next] === 0) sources.push(next);
    }
  }
  if (removed !== states.length) {
    return {
      graph6: graph.graph6,
      start,
      kind: "CYCLE",
      reachable: states.length
    };
  }
  return undefined;
}

function findWitness(
  graphs: readonly Graph[],
  clause: readonly number[]
): Witness | "TAUTOLOGY" | undefined {
  const partial = parsePartialPolicy(clause);
  if (partial === "TAUTOLOGY") return partial;
  for (const graph of graphs) {
    for (let start = 0; start < graph.n; start += 1) {
      const witness = graphWitness(graph, partial, start);
      if (witness !== undefined) return witness;
    }
  }
  return undefined;
}

function loadGraphs(path: string, added: readonly string[]): Graph[] {
  const graph6s = readFileSync(path, "utf8")
    .split(/\r?\n/)
    .map((line) => line.trim())
    .filter((line) => line.length > 0 && !line.startsWith("#"));
  graph6s.push(...added);
  if (graph6s.length === 0) throw new Error("graph list is empty");
  return graph6s.map((graph6) => new Graph(graph6));
}

async function verifyBlocks(
  graphs: readonly Graph[],
  path: string,
  limit: number,
  mutationTest: boolean,
  verbose: boolean,
  generalizeMaxOrder: number,
  filterOutput: string
): Promise<void> {
  const input = createInterface({
    input: createReadStream(path, { encoding: "utf8" }),
    crlfDelay: Infinity
  });
  let markerSeen = false;
  let verified = 0;
  let literals = 0;
  let tautologies = 0;
  let mutationRejected = false;
  let generalized = 0;
  let generalizationSkipped = 0;
  let initialComplement = 0;
  let finalComplement = 0;
  let domainOracleCalls = 0;
  let domainCertificates = 0;
  let pendingDomain: GraphDomainCertificate | undefined;
  let pendingDomainLine = "";
  const filteredLines = ["c strict-stop-verified-v1"];
  let skipped = 0;
  let processed = 0;
  let lineNumber = 0;
  for await (const rawLine of input) {
    lineNumber += 1;
    const line = rawLine.trim();
    if (line.length === 0) continue;
    if (!markerSeen) {
      if (line !== "c strict-stop-verified-v1") {
        throw new Error("block file lacks the strict STOP marker");
      }
      markerSeen = true;
      continue;
    }
    if (line.startsWith("c graph-gfp-domain ")) {
      if (pendingDomain !== undefined) {
        throw new Error(
          `missing graph domain clause before line ${lineNumber}`
        );
      }
      const match =
        /^c graph-gfp-domain graph6=(\S+) start=(\d+) masks=(.+)$/.exec(line);
      if (match === null) {
        throw new Error(
          `malformed graph domain comment at line ${lineNumber}`
        );
      }
      const masks = match[3].split(",").map(Number);
      if (masks.length !== OBSERVATIONS ||
          masks.some((mask, observation) =>
            !Number.isInteger(mask) || mask <= 0 ||
            mask >= (1 << ACTIONS) ||
            (mask & ~strictActionMask(observation)) !== 0)) {
        throw new Error(
          `invalid graph domain masks at line ${lineNumber}`
        );
      }
      pendingDomain = {
        graph6: match[1],
        start: Number(match[2]),
        domain: Uint16Array.from(masks)
      };
      pendingDomainLine = line;
      continue;
    }
    if (line.startsWith("c")) continue;
    const clause = line.split(/\s+/).map(Number);
    if (clause.length === 0 || clause.some((literal) =>
      !Number.isInteger(literal) || literal === 0)) {
      throw new Error(`invalid clause at line ${lineNumber}`);
    }
    let keep = true;
    const domainLine = pendingDomainLine;
    if (pendingDomain !== undefined) {
      const graph = graphs.find(
        (candidate) => candidate.graph6 === pendingDomain?.graph6
      );
      const expected = expectedDomainBlock(pendingDomain.domain);
      if (!sameNumbers(clause, expected)) {
        throw new Error(
          `graph domain clause mismatch at line ${lineNumber}`
        );
      }
      if (graph === undefined) {
        if (filterOutput.length === 0) {
          throw new Error(
            `graph domain has an unknown graph at line ${lineNumber}`
          );
        }
        keep = false;
      } else if (pendingDomain.start < 0 ||
          pendingDomain.start >= graph.n) {
        throw new Error(
          `graph domain has an invalid start at line ${lineNumber}`
        );
      } else if (!domainForcesFailure(
        graph,
        pendingDomain.domain,
        pendingDomain.start
      )) {
        throw new Error(
          `graph domain is not losing at line ${lineNumber}`
        );
      }
      if (keep && mutationTest && !mutationRejected) {
        for (let removed = 0; removed < clause.length; removed += 1) {
          const zeroBased = clause[removed] - 1;
          const observation = Math.floor(zeroBased / ACTIONS);
          const action = zeroBased % ACTIONS;
          const mutatedDomain =
            new Uint16Array(pendingDomain.domain);
          mutatedDomain[observation] |= 1 << action;
          const mutatedClause = clause.filter(
            (_, index) => index !== removed
          );
          if (!sameNumbers(
                mutatedClause,
                expectedDomainBlock(mutatedDomain))) {
            throw new Error(
              "internal graph domain mutation mismatch"
            );
          }
          if (!domainForcesFailure(
            graph,
            mutatedDomain,
            pendingDomain.start
          )) {
            mutationRejected = true;
            break;
          }
        }
      }
      if (keep) domainCertificates += 1;
      pendingDomain = undefined;
      pendingDomainLine = "";
    } else {
      const witness = findWitness(graphs, clause);
      if (witness === undefined) {
        if (filterOutput.length === 0) {
          throw new Error(
            `no partial-policy failure witness for line ${lineNumber}: ${line}`
          );
        }
        keep = false;
      } else if (witness === "TAUTOLOGY") {
        tautologies += 1;
      } else {
        if (verbose) {
          console.log(
            `block=${verified + 1} graph6=${witness.graph6} ` +
            `start=${witness.start} kind=${witness.kind} ` +
            `reachable=${witness.reachable}`
          );
        }
        if (generalizeMaxOrder > 0) {
          const graph = graphs.find(
            (candidate) => candidate.graph6 === witness.graph6
          );
          if (graph === undefined) {
            throw new Error("witness graph disappeared from the graph list");
          }
          if (graph.n <= generalizeMaxOrder) {
            const result = generalizeTraceDomain(
              graph,
              clause,
              witness.start
            );
            if (result !== undefined) {
              generalized += 1;
              initialComplement += result.initialComplement;
              finalComplement += result.finalComplement;
              domainOracleCalls += result.oracleCalls;
              if (verbose) {
                console.log(
                  `domain-generalization block=${verified + 1} ` +
                  `initial-complement=${result.initialComplement} ` +
                  `final-complement=${result.finalComplement} ` +
                  `added-actions=${result.addedActions} ` +
                  `oracle-calls=${result.oracleCalls}`
                );
              }
            }
          } else {
            generalizationSkipped += 1;
          }
        }
      }
      if (keep && mutationTest && !mutationRejected && clause.length > 1) {
        for (let removed = 0; removed < clause.length; removed += 1) {
          const mutation = clause.filter((_, index) => index !== removed);
          if (findWitness(graphs, mutation) === undefined) {
            mutationRejected = true;
            break;
          }
        }
      }
    }
    processed += 1;
    if (keep) {
      if (domainLine.length > 0) filteredLines.push(domainLine);
      filteredLines.push(line);
      verified += 1;
      literals += clause.length;
    } else {
      skipped += 1;
    }
    if (processed >= limit) {
      input.close();
      break;
    }
  }
  if (!markerSeen) throw new Error("block file lacks the strict STOP marker");
  if (pendingDomain !== undefined) {
    throw new Error("graph domain comment lacks a following clause");
  }
  if (verified === 0) throw new Error("block file contains no clauses");
  if (mutationTest && !mutationRejected) {
    throw new Error(
      "no tested single-literal deletion was rejected"
    );
  }
  if (filterOutput.length > 0) {
    writeFileSync(filterOutput, `${filteredLines.join("\n")}\n`);
  }
  console.log(
    "cactus-policy-blocks=" +
    `${filterOutput.length > 0 ? "FILTERED" : "VERIFIED"} ` +
    `blocks=${verified} skipped=${skipped} ` +
    `literals=${literals} graphs=${graphs.length} ` +
    `tautologies=${tautologies} ` +
    `mutation-rejected=${mutationRejected ? 1 : 0} ` +
    `domain-generalized=${generalized} ` +
    `domain-skipped=${generalizationSkipped} ` +
    `domain-certificates=${domainCertificates} ` +
    `initial-complement=${initialComplement} ` +
    `final-complement=${finalComplement} ` +
    `domain-oracle-calls=${domainOracleCalls} ` +
    `file=${path}`
  );
}

async function main(): Promise<void> {
  initializeObservations();
  let graphsPath = "";
  const blocksPaths: string[] = [];
  const addedGraphs: string[] = [];
  let limit = Number.POSITIVE_INFINITY;
  let mutationTest = false;
  let verbose = false;
  let generalizeMaxOrder = 0;
  let domainSelfTest = 0;
  let filterOutput = "";
  for (let index = 2; index < process.argv.length; index += 1) {
    const argument = process.argv[index];
    if (argument === "--graphs" && index + 1 < process.argv.length) {
      graphsPath = process.argv[++index];
    } else if (argument === "--add-graph" &&
               index + 1 < process.argv.length) {
      addedGraphs.push(process.argv[++index]);
    } else if (argument === "--blocks" &&
               index + 1 < process.argv.length) {
      blocksPaths.push(process.argv[++index]);
    } else if (argument === "--limit" &&
               index + 1 < process.argv.length) {
      limit = Number(process.argv[++index]);
    } else if (argument === "--mutation-test") {
      mutationTest = true;
    } else if (argument === "--domain-generalize-max-order" &&
               index + 1 < process.argv.length) {
      generalizeMaxOrder = Number(process.argv[++index]);
    } else if (argument === "--domain-self-test" &&
               index + 1 < process.argv.length) {
      domainSelfTest = Number(process.argv[++index]);
    } else if (argument === "--verbose") {
      verbose = true;
    } else if (argument === "--filter-output" &&
               index + 1 < process.argv.length) {
      filterOutput = process.argv[++index];
    } else {
      throw new Error(`unknown or incomplete argument: ${argument}`);
    }
  }
  if (graphsPath.length === 0 || blocksPaths.length === 0 ||
      !Number.isInteger(limit) && limit !== Number.POSITIVE_INFINITY ||
      limit <= 0) {
    throw new Error(
      "usage: verify_cactus_policy_blocks.ts --graphs FILE " +
      "[--add-graph GRAPH6] --blocks FILE [--limit N] " +
      "[--mutation-test] [--domain-generalize-max-order N] " +
      "[--domain-self-test N] [--verbose] " +
      "[--filter-output FILE]"
    );
  }
  if (filterOutput.length > 0) {
    if (blocksPaths.length !== 1) {
      throw new Error("--filter-output requires exactly one --blocks file");
    }
    if (existsSync(filterOutput)) {
      throw new Error(`filter output already exists: ${filterOutput}`);
    }
  }
  const graphs = loadGraphs(graphsPath, addedGraphs);
  if (!Number.isInteger(domainSelfTest) || domainSelfTest < 0) {
    throw new Error("--domain-self-test must be nonnegative");
  }
  if (domainSelfTest > 0) runDomainSelfTest(graphs, domainSelfTest);
  for (const blocksPath of blocksPaths) {
    await verifyBlocks(
      graphs,
      blocksPath,
      limit,
      mutationTest,
      verbose,
      generalizeMaxOrder,
      filterOutput
    );
  }
}

main().catch((error: unknown) => {
  const message = error instanceof Error ? error.message : String(error);
  console.error(`error: ${message}`);
  process.exitCode = 1;
});
