const COLORS = 3;

// ExOG in the proof, with paper vertices 1,...,6 represented by 0,...,5.
const adjacency = [
  [1, 2, 3],
  [0, 2, 4],
  [0, 1],
  [0],
  [1, 5],
  [4]
];

function assert(condition, message) {
  if (!condition) throw new Error(message);
}

function step(state, repaint, next) {
  assert(
    adjacency[state.position].includes(next),
    "the claimed adversarial move is not an edge"
  );
  assert(
    state.colors[next] === 0,
    "the claimed adversarial move does not target an old color-0 neighbor"
  );
  const colors = [...state.colors];
  colors[state.position] = repaint;
  return { position: next, colors };
}

function samePhysical(left, right) {
  return left.position === right.position &&
    left.colors.every((color, vertex) => color === right.colors[vertex]);
}

function initial(position) {
  return { position, colors: Array(adjacency.length).fill(0) };
}

function swapNonzero(color) {
  return color === 1 ? 2 : color === 2 ? 1 : 0;
}

function tupleKey(tuple) {
  return tuple.join("");
}

function verifyExog() {
  const reached = new Set([0]);
  const queue = [0];
  for (let head = 0; head < queue.length; head += 1) {
    for (const next of adjacency[queue[head]]) {
      assert(
        adjacency[next].includes(queue[head]),
        "ExOG adjacency is not symmetric"
      );
      if (reached.has(next)) continue;
      reached.add(next);
      queue.push(next);
    }
  }
  const degreeSum = adjacency.reduce(
    (sum, neighbors) => sum + neighbors.length,
    0
  );
  const edges = degreeSum / 2;
  assert(reached.size === adjacency.length, "ExOG is disconnected");
  assert(
    adjacency.every((neighbors) => neighbors.length <= 3),
    "ExOG is not subcubic"
  );
  assert(edges === adjacency.length, "ExOG is not unicyclic");
  assert(
    adjacency[0].includes(1) && adjacency[1].includes(2) &&
      adjacency[2].includes(0),
    "ExOG lacks its unique triangle"
  );
  console.log(
    `exog=VERIFIED vertices=${adjacency.length} edges=${edges} ` +
      "connected=1 max-degree=3 unicyclic=1 pseudotree=1 cactus=1"
  );
}

function main() {
  verifyExog();
  const representatives = new Set(["111", "121", "211", "221"]);
  const orbitCounts = new Map();
  let total = 0;
  let degree3Cycle = 0;
  let degree2Merge = 0;
  let degree1Merge = 0;
  let residual = 0;

  for (let r1 = 0; r1 < COLORS; r1 += 1) {
    for (let r2 = 0; r2 < COLORS; r2 += 1) {
      for (let r3 = 0; r3 < COLORS; r3 += 1) {
        total += 1;
        if (r3 === 0) {
          const start = initial(0);
          const afterTwoMoves = step(step(start, r3, 1), r3, 0);
          assert(
            samePhysical(start, afterTwoMoves),
            "the degree-3 two-edge cycle did not restore its physical state"
          );
          degree3Cycle += 1;
          continue;
        }
        if (r2 === 0) {
          const merged = step(initial(4), r2, 5);
          assert(
            samePhysical(merged, initial(5)),
            "the degree-2 merge did not reach the other initial state"
          );
          degree2Merge += 1;
          continue;
        }
        if (r1 === 0) {
          const fromFive = step(initial(4), r2, 1);
          const fromSix = step(step(initial(5), r1, 4), r2, 1);
          assert(
            samePhysical(fromFive, fromSix),
            "the degree-1/degree-2 paths did not merge"
          );
          degree1Merge += 1;
          continue;
        }

        const tuple = [r1, r2, r3];
        const representative = tupleKey(
          r3 === 1 ? tuple : tuple.map(swapNonzero)
        );
        assert(
          representatives.has(representative),
          `unexpected residual orbit representative ${representative}`
        );
        orbitCounts.set(
          representative,
          (orbitCounts.get(representative) ?? 0) + 1
        );
        residual += 1;
      }
    }
  }

  assert(total === 27, "did not classify all initial tuples");
  assert(residual === 8, "the residual set should contain eight tuples");
  for (const representative of representatives) {
    assert(
      orbitCounts.get(representative) === 2,
      `orbit ${representative} should contain two tuples`
    );
  }
  console.log(
    `initial-tuple-reduction=VERIFIED total=${total} direct=${
      degree3Cycle + degree2Merge + degree1Merge
    } residual=${residual} degree3-cycle=${degree3Cycle} ` +
      `degree2-merge=${degree2Merge} degree1-merge=${degree1Merge} ` +
      "orbit-representatives=111,121,211,221 orbit-size=2"
  );
}

main();
