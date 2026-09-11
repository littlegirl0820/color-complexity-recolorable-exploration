#!/usr/bin/env node

import { readFileSync } from "node:fs";

const TARGETS = [
  ["C3", 3, [[0, 1], [1, 2], [0, 2]]],
  ["K1,3", 4, [[0, 1], [0, 2], [0, 3]]],
  ["C4", 4, [[0, 1], [1, 2], [2, 3], [0, 3]]],
  ["C3+leaf", 4, [[0, 1], [1, 2], [0, 2], [2, 3]]],
  ["T5", 5, [[0, 1], [1, 2], [2, 3], [2, 4]]],
  ["C5", 5, [[0, 1], [1, 2], [2, 3], [3, 4], [0, 4]]],
  ["C4+leaf", 5, [[0, 1], [1, 2], [2, 3], [0, 3], [3, 4]]],
  ["C3+tail2", 5, [[0, 1], [1, 2], [0, 2], [2, 3], [3, 4]]],
  ["C3+2leaves", 5, [[0, 1], [1, 2], [0, 2], [1, 3], [2, 4]]]
].map(([name, n, edges]) => ({ name, n, edges: normalizeEdges(edges) }));

// This order is fixed by the checked certificate.  Changing it changes the
// variable numbering even when the same nine isomorphism classes are used.
const EXPECTED_CERTIFICATE_ORDER = [
  "C3",
  "C4+leaf",
  "C3+leaf",
  "C3+2leaves",
  "K1,3",
  "C4",
  "C3+tail2",
  "T5",
  "C5"
];

function normalizeEdges(edges) {
  return new Set(edges.map(([u, v]) => `${Math.min(u, v)},${Math.max(u, v)}`));
}

function decodeStandardGraph6(encoded) {
  if (encoded.length === 0 || encoded.charCodeAt(0) === 126) {
    throw new Error(`only short standard graph6 is supported: ${encoded}`);
  }
  const n = encoded.charCodeAt(0) - 63;
  if (n < 1 || n > 62) throw new Error(`invalid graph order: ${encoded}`);
  const requiredBits = n * (n - 1) / 2;
  const expectedCharacters = 1 + Math.ceil(requiredBits / 6);
  if (encoded.length !== expectedCharacters) {
    throw new Error(`noncanonical graph6 length: ${encoded}`);
  }
  const bits = [];
  for (let index = 1; index < encoded.length; index += 1) {
    const value = encoded.charCodeAt(index) - 63;
    if (value < 0 || value > 63) {
      throw new Error(`invalid graph6 character: ${encoded}`);
    }
    for (let bit = 5; bit >= 0; bit -= 1) bits.push((value >> bit) & 1);
  }
  if (bits.slice(requiredBits).some((bit) => bit !== 0)) {
    throw new Error(`nonzero graph6 padding: ${encoded}`);
  }
  const edges = new Set();
  let cursor = 0;
  for (let right = 1; right < n; right += 1) {
    for (let left = 0; left < right; left += 1) {
      if (bits[cursor] !== 0) edges.add(`${left},${right}`);
      cursor += 1;
    }
  }
  return { encoded, n, edges };
}

function adjacencyOf(graph) {
  const adjacency = Array.from({ length: graph.n }, () => []);
  for (const edge of graph.edges) {
    const [u, v] = edge.split(",").map(Number);
    adjacency[u].push(v);
    adjacency[v].push(u);
  }
  return adjacency;
}

function isConnected(graph) {
  const adjacency = adjacencyOf(graph);
  const reached = new Set([0]);
  const queue = [0];
  for (let head = 0; head < queue.length; head += 1) {
    for (const next of adjacency[queue[head]]) {
      if (reached.has(next)) continue;
      reached.add(next);
      queue.push(next);
    }
  }
  return reached.size === graph.n;
}

function isPseudotree(graph) {
  return isConnected(graph) && graph.edges.size <= graph.n;
}

function biconnectedEdgeComponents(graph) {
  const adjacency = adjacencyOf(graph);
  const discovery = Array(graph.n).fill(-1);
  const low = Array(graph.n).fill(-1);
  const stack = [];
  const components = [];
  let time = 0;

  function visit(vertex, parent) {
    discovery[vertex] = low[vertex] = time;
    time += 1;
    for (const next of adjacency[vertex]) {
      if (next === parent) continue;
      if (discovery[next] < 0) {
        const edge = `${Math.min(vertex, next)},${Math.max(vertex, next)}`;
        stack.push(edge);
        visit(next, vertex);
        low[vertex] = Math.min(low[vertex], low[next]);
        if (low[next] >= discovery[vertex]) {
          const component = [];
          while (stack.length > 0) {
            const popped = stack.pop();
            component.push(popped);
            if (popped === edge) break;
          }
          components.push(component);
        }
      } else if (discovery[next] < discovery[vertex]) {
        stack.push(`${Math.min(vertex, next)},${Math.max(vertex, next)}`);
        low[vertex] = Math.min(low[vertex], discovery[next]);
      }
    }
  }

  visit(0, -1);
  return components;
}

function isCactus(graph) {
  if (!isConnected(graph)) return false;
  for (const component of biconnectedEdgeComponents(graph)) {
    if (component.length === 1) continue;
    const degrees = new Map();
    for (const edge of component) {
      const [u, v] = edge.split(",").map(Number);
      degrees.set(u, (degrees.get(u) ?? 0) + 1);
      degrees.set(v, (degrees.get(v) ?? 0) + 1);
    }
    if (component.length !== degrees.size) return false;
    if ([...degrees.values()].some((degree) => degree !== 2)) return false;
  }
  return true;
}

function degreeSequence(graph) {
  return adjacencyOf(graph).map((neighbors) => neighbors.length).sort((a, b) => b - a);
}

function permutations(values) {
  if (values.length === 0) return [[]];
  const result = [];
  for (let index = 0; index < values.length; index += 1) {
    const remaining = values.slice(0, index).concat(values.slice(index + 1));
    for (const suffix of permutations(remaining)) {
      result.push([values[index], ...suffix]);
    }
  }
  return result;
}

function isIsomorphic(left, right) {
  if (left.n !== right.n || left.edges.size !== right.edges.size) return false;
  if (degreeSequence(left).join(",") !== degreeSequence(right).join(",")) return false;
  for (const permutation of permutations([...Array(left.n).keys()])) {
    const mapped = new Set();
    for (const edge of left.edges) {
      const [u, v] = edge.split(",").map(Number);
      const a = permutation[u];
      const b = permutation[v];
      mapped.add(`${Math.min(a, b)},${Math.max(a, b)}`);
    }
    if (mapped.size === right.edges.size && [...mapped].every((edge) => right.edges.has(edge))) {
      return true;
    }
  }
  return false;
}

const input = process.argv[2] ?? new URL("./residual-family.graph6", import.meta.url);
const lines = readFileSync(input, "utf8").split(/\r?\n/).filter((line) => line.length > 0);
if (lines.length !== TARGETS.length) {
  throw new Error(`expected ${TARGETS.length} obstruction graphs, got ${lines.length}`);
}

const graphs = lines.map(decodeStandardGraph6);
const names = [];
for (let index = 0; index < graphs.length; index += 1) {
  const graph = graphs[index];
  if (!isConnected(graph)) throw new Error(`disconnected obstruction: ${graph.encoded}`);
  const degrees = degreeSequence(graph);
  if (degrees[0] > 3) throw new Error(`non-subcubic obstruction: ${graph.encoded}`);
  if (!isPseudotree(graph)) throw new Error(`non-pseudotree obstruction: ${graph.encoded}`);
  if (!isCactus(graph)) throw new Error(`non-cactus obstruction: ${graph.encoded}`);
  const matches = TARGETS.filter((target) => isIsomorphic(graph, target));
  if (matches.length !== 1) {
    throw new Error(`obstruction ${graph.encoded} matches ${matches.length} target shapes`);
  }
  names.push(matches[0].name);
  for (let previous = 0; previous < index; previous += 1) {
    if (isIsomorphic(graph, graphs[previous])) {
      throw new Error(`isomorphic duplicate: ${graphs[previous].encoded}, ${graph.encoded}`);
    }
  }
}

if (names.join("\n") !== EXPECTED_CERTIFICATE_ORDER.join("\n")) {
  throw new Error(`certificate order changed: ${names.join(",")}`);
}
const missing = TARGETS.map((target) => target.name).filter((name) => !names.includes(name));
if (missing.length > 0) throw new Error(`missing target shapes: ${missing.join(",")}`);
const trees = graphs.filter((graph) => graph.edges.size === graph.n - 1).length;
const unicyclic = graphs.filter((graph) => graph.edges.size === graph.n).length;
if (trees !== 2 || unicyclic !== 7) {
  throw new Error(`expected two trees and seven unicyclic graphs, got ${trees} and ${unicyclic}`);
}

console.log(
  `obstruction-family=VERIFIED graphs=${graphs.length} trees=${trees} ` +
    `unicyclic=${unicyclic} order=${names.join(",")}`
);
