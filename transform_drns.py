#!/usr/bin/env python3

import argparse
import os
import re
import tempfile
from datetime import datetime
from pathlib import Path

STATE_RE = re.compile(r'^\s*state\s+(\d+)')
TRANS_RE = re.compile(r'^(\s*)(\d+)\s*:\s*([0-9eE+.\-]+)\s*$')


def find_sink_state(path: Path):
    """Return the numeric ID of the state labelled discount_sink."""
    current_state = None

    with path.open("r") as f:
        for line in f:
            m = STATE_RE.match(line)
            if m:
                current_state = int(m.group(1))

            if "discount_sink" in line:
                if current_state is None:
                    raise RuntimeError(f"Found discount_sink before state declaration in {path}")
                return current_state

    raise RuntimeError(f"No state labelled 'discount_sink' found in {path}")


def process_file(src: Path, dst: Path):
    sink = find_sink_state(src)

    dst.parent.mkdir(parents=True, exist_ok=True)

    with src.open("r") as fin, dst.open("w") as fout:

        skipping_state = False
        current_state = None

        action_lines = []
        action_transitions = []

        def flush_action():
            nonlocal action_lines, action_transitions

            if not action_lines:
                return

            # No transitions -> just emit verbatim.
            if not action_transitions:
                fout.writelines(action_lines)
                action_lines = []
                return

            sink_prob = 0.0
            kept = []

            for idx, indent, target, prob in action_transitions:
                if target == sink:
                    sink_prob = prob
                else:
                    kept.append((idx, indent, target, prob))

            if sink_prob == 0.0:
                fout.writelines(action_lines)
                action_lines = []
                action_transitions = []
                return

            remaining = sum(p for _, _, _, p in kept)

            if remaining <= 0:
                raise RuntimeError(
                    f"Action has no remaining probability after removing sink in {src}"
                )

            # Replace transition lines.
            new_lines = list(action_lines)

            for idx, indent, target, prob in kept:
                new_prob = prob / remaining
                new_lines[idx] = f"{indent}{target} : {new_prob:.12g}\n"

            # Remove sink transition.
            sink_indices = [
                idx for idx, _, target, _ in action_transitions if target == sink
            ]
            for idx in reversed(sink_indices):
                del new_lines[idx]

            fout.writelines(new_lines)

            action_lines = []
            action_transitions = []


        for line in fin:

            m = STATE_RE.match(line)
            if m:
                flush_action()

                current_state = int(m.group(1))
                skipping_state = current_state == sink

            if skipping_state:
                continue

            # New action starts.
            if line.lstrip().startswith("action "):
                flush_action()
                action_lines.append(line)
                continue

            # Inside an action?
            if action_lines:
                tm = TRANS_RE.match(line)
                if tm:
                    indent = tm.group(1)
                    target = int(tm.group(2))
                    prob = float(tm.group(3))

                    action_transitions.append(
                        (len(action_lines), indent, target, prob)
                    )
                    action_lines.append(line)
                else:
                    flush_action()
                    fout.write(line)
            else:
                fout.write(line)

        flush_action()

    with open(dst, "r") as fin, tempfile.NamedTemporaryFile("w", delete=False) as fout:
        it = iter(fin)
        for line in it:
            fout.write(line)

            l = line.rstrip()
            if l == "@nr_states" or l == "@nr_choices":
                next_line = next(it)

                try:
                    value = int(next_line.strip())
                    value -= 1
                    fout.write(str(value) + "\n")
                except ValueError as ve:
                    print(ve)
                    break

    temp_name = fout.name
    os.replace(temp_name, dst)


def main():
    """
    This script transforms an input directory of .drn files:
    - The sink state labeled discount_sink is removed.
    - Every transition to it is removed and its probability is
    proportionally redistributed to other transitions.
    - Number of states and number of choices is decreased by one.
    """

    parser = argparse.ArgumentParser()
    parser.add_argument("directory", type=Path)
    parser.add_argument(
        "--exclude",
        nargs="*",
        default=[],
        help="Exact filenames to skip.",
    )

    args = parser.parse_args()

    root = args.directory.resolve()
    out_root = root / f"transformed_{datetime.now().strftime('%Y%m%d_%H%M%S')}"

    excluded = set(args.exclude)

    for src in root.rglob("*.drn"):

        if out_root in src.parents:
            continue

        if src.name in excluded:
            print(f"Skipping {src.name}")
            continue

        rel = src.relative_to(root)
        dst = out_root / rel

        print(f"Processing {rel}")
        process_file(src, dst)


if __name__ == "__main__":
    main()
