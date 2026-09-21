#!/usr/bin/env python3
"""Extract a bounded, complete chr21 RNA annotation fixture by parent group."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
from urllib.parse import unquote


BODY_LIMIT = 128
BODY_BP_LIMIT = 8_000_000
FEATURE_BP_LIMIT = 20_000_000
PARENT_LIMIT = 100_000


def attributes(text):
    result = {}
    for field in text.rstrip("\n").split(";"):
        key, separator, value = field.partition("=")
        if separator:
            # GFF3 lists use literal commas; encoded commas belong in one value.
            result[key] = [unquote(item) for item in value.split(",")]
    return result


def gff_fields(line, source):
    if line.startswith("#"):
        return None
    fields = line.rstrip("\n").split("\t")
    if len(fields) != 9:
        raise ValueError(f"{source}: expected nine GFF3 fields")
    start, end = int(fields[3]), int(fields[4])
    if end < start:
        raise ValueError(f"{source}: invalid interval {start}-{end}")
    return fields, start, end, attributes(fields[8])


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def source_metadata(path):
    stat = path.stat()
    return {"path": str(path), "size_bytes": stat.st_size, "mtime_ns": stat.st_mtime_ns}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    root = args.source_root.resolve()
    annotation = root / "annotation"
    bodies = annotation / "retained_transcript_body_segments.gff3"
    combined = annotation / "retained_exons_and_bodies.gff3"
    aliases = annotation / "body_parent_aliases.tsv"
    pads = root / "retention_pad1000.gff3"
    for source in (bodies, combined, aliases, pads):
        if not source.is_file():
            raise SystemExit(f"missing required source: {source}")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)

    # The body-segment file is already ordered by its body Parent. Verify that
    # contract while aggregating each complete parent group before selection.
    selected = []
    selected_set = set()
    selected_bp = 0
    selected_body_lines = {}
    selected_unique_parent = {}
    previous = None
    current_parent = None
    current_bp = 0
    current_lines = 0
    current_unique = set()

    def consider(parent, span, lines, unique_parents):
        nonlocal selected_bp
        if parent is None or len(selected) == BODY_LIMIT:
            return
        if selected_bp + span <= BODY_BP_LIMIT:
            selected.append(parent)
            selected_set.add(parent)
            selected_bp += span
            selected_body_lines[parent] = lines
            selected_unique_parent[parent] = unique_parents

    with bodies.open() as stream:
        for line in stream:
            parsed = gff_fields(line, bodies)
            if parsed is None:
                continue
            _, start, end, attrs = parsed
            parents = attrs.get("Parent", [])
            if len(parents) != 1:
                raise ValueError(f"{bodies}: body feature lacks one Parent")
            parent = parents[0]
            if previous is not None and parent < previous:
                raise ValueError(f"{bodies}: Parent order is not lexical at {parent}")
            if parent != current_parent:
                consider(current_parent, current_bp, current_lines, current_unique)
                current_parent = parent
                current_bp = 0
                current_lines = 0
                current_unique = set()
            current_bp += end - start + 1
            current_lines += 1
            current_unique.update(attrs.get("exon_unique_parent", []))
            previous = parent
    consider(current_parent, current_bp, current_lines, current_unique)
    if not selected:
        raise ValueError("no body-parent groups fit the requested bounds")

    # Only retain alias rows needed to classify selected exon parent groups.
    exon_to_body = {}
    with aliases.open() as stream:
        for row in csv.DictReader(stream, delimiter="\t"):
            body_parent = unquote(row["raw_parent"])
            exon_parent = unquote(row["exon_parent"])
            if body_parent in selected_set:
                prior = exon_to_body.setdefault(exon_parent, body_parent)
                if prior != body_parent:
                    raise ValueError(f"ambiguous exon alias {exon_parent}")
    unique_to_body = {}
    for body_parent, unique_parents in selected_unique_parent.items():
        for unique_parent in unique_parents:
            prior = unique_to_body.setdefault(unique_parent, body_parent)
            if prior != body_parent:
                raise ValueError(f"ambiguous exon_unique_parent {unique_parent}")

    fixture = output / "annotation_with_retention.gff3"
    stats = {"exons": {"lines": 0, "bp": 0}, "bodies": {"lines": 0, "bp": 0},
             "retention_pads": {"lines": 0, "bp": 0}}
    observed_body_lines = {parent: 0 for parent in selected}
    observed_parents = set()
    observed_exon_aliases = set()

    # Discover only retention groups that have a selected source body. A second
    # pass below validates every line in those groups, so no global group map is
    # needed for the multi-million-group input.
    selected_pad_groups = {}
    with pads.open() as stream:
        for line in stream:
            parsed = gff_fields(line, pads)
            if parsed is None:
                continue
            _, _, _, attrs = parsed
            groups = attrs.get("Parent", [])
            sources = attrs.get("source_body_parent", [])
            if len(groups) != 1 or len(sources) != 1:
                raise ValueError(f"{pads}: retention feature lacks one group/source")
            group, source_body = groups[0], sources[0]
            if source_body in selected_set:
                prior = selected_pad_groups.setdefault(group, source_body)
                if prior != source_body:
                    raise ValueError(f"{pads}: retention group spans body parents: {group}")

    with fixture.open("w") as out, combined.open() as stream:
        out.write("##gff-version 3\n")
        for line in stream:
            parsed = gff_fields(line, combined)
            if parsed is None:
                continue
            fields, start, end, attrs = parsed
            parents = attrs.get("Parent", [])
            selected_parent = None
            for parent in parents:
                if parent in selected_set:
                    selected_parent = parent
                    break
                if parent in exon_to_body:
                    selected_parent = exon_to_body[parent]
                    observed_exon_aliases.add(parent)
                    break
            if selected_parent is None:
                for unique_parent in attrs.get("exon_unique_parent", []):
                    if unique_parent in unique_to_body:
                        selected_parent = unique_to_body[unique_parent]
                        break
            if selected_parent is None:
                continue
            out.write(line)
            layer = attrs.get("feature_layer", [""])[0]
            kind = "bodies" if layer == "transcript_body" else "exons"
            stats[kind]["lines"] += 1
            stats[kind]["bp"] += end - start + 1
            observed_parents.update(parents)
            if selected_parent in observed_body_lines and layer == "transcript_body":
                observed_body_lines[selected_parent] += 1

        for line in pads.open():
            parsed = gff_fields(line, pads)
            if parsed is None:
                continue
            fields, start, end, attrs = parsed
            groups = attrs.get("Parent", [])
            sources = attrs.get("source_body_parent", [])
            if len(groups) != 1 or len(sources) != 1:
                raise ValueError(f"{pads}: retention feature lacks one group/source")
            group, source_body = groups[0], sources[0]
            if group not in selected_pad_groups:
                continue
            if selected_pad_groups[group] != source_body:
                raise ValueError(f"{pads}: retention group spans body parents: {group}")
            out.write(line)
            stats["retention_pads"]["lines"] += 1
            stats["retention_pads"]["bp"] += end - start + 1
            observed_parents.add(group)

    if observed_body_lines != selected_body_lines:
        missing = {key: (selected_body_lines[key], observed_body_lines[key])
                   for key in selected if selected_body_lines[key] != observed_body_lines[key]}
        raise ValueError(f"incomplete body parent groups: {missing}")
    if observed_exon_aliases != set(exon_to_body):
        missing = sorted(set(exon_to_body) - observed_exon_aliases)
        raise ValueError(f"selected exon aliases absent from retained annotation: {missing[:5]}")
    total_bp = sum(item["bp"] for item in stats.values())
    if total_bp > FEATURE_BP_LIMIT or len(observed_parents) > PARENT_LIMIT:
        raise ValueError("fixture exceeds requested feature or Parent bound")
    if any(stats[kind]["lines"] == 0 for kind in stats):
        raise ValueError("fixture must include exon, body, and retention-pad features")

    selected_path = output / "selected_body_parents.txt"
    selected_path.write_text("\n".join(selected) + "\n")
    manifest = {
        "source_root": str(root),
        "sources": {"bodies": source_metadata(bodies), "combined": source_metadata(combined),
                    "aliases": source_metadata(aliases), "retention_pads": source_metadata(pads)},
        "selection": {"method": "first lexical complete body-parent groups within limits",
                      "body_parent_count": len(selected), "body_span_bp": selected_bp,
                      "body_parent_limit": BODY_LIMIT, "body_span_bp_limit": BODY_BP_LIMIT,
                      "selected_body_parents": selected},
        "fixture": {"gff3": fixture.name, "feature_stats": stats,
                    "total_feature_bp": total_bp, "unique_parent_count": len(observed_parents),
                    "feature_bp_limit": FEATURE_BP_LIMIT, "parent_limit": PARENT_LIMIT,
                    "selected_exon_alias_count": len(exon_to_body),
                    "represented_exon_alias_count": len(observed_exon_aliases),
                    "combined_then_retention_append": True,
                    "group_completeness": "all source lines matched by selected body/exon parent or selected retention source body were retained"},
        "sha256": {fixture.name: sha256(fixture), selected_path.name: sha256(selected_path)},
    }
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n")


if __name__ == "__main__":
    main()
