#!/usr/bin/env python3
"""
Automated PDF Book Index Generator

Features:
- Recursive PDF discovery
- Metadata extraction via pdfinfo
- SHA-256 integrity hash
- Markdown index generation
- JSON export
- Statistical summary

Requires:
    poppler (pdfinfo)
"""

import hashlib
import json
import subprocess
import statistics
from pathlib import Path
from datetime import datetime, UTC

CHUNK_SIZE = 1024 * 1024


# =========================
# Utilities
# =========================

def sha256_file(path: Path) -> str:
    hasher = hashlib.sha256()
    with path.open("rb") as f:
        while chunk := f.read(CHUNK_SIZE):
            hasher.update(chunk)
    return hasher.hexdigest()


def human_size(num_bytes: int) -> str:
    for unit in ["B", "KB", "MB", "GB", "TB"]:
        if num_bytes < 1024:
            return f"{num_bytes:.2f} {unit}"
        num_bytes /= 1024
    return f"{num_bytes:.2f} PB"


def extract_pdf_metadata(path: Path) -> dict:
    try:
        result = subprocess.run(
            ["pdfinfo", str(path)],
            capture_output=True,
            text=True,
            check=False
        )

        meta = {}
        for line in result.stdout.splitlines():
            if ":" in line:
                key, value = line.split(":", 1)
                meta[key.strip()] = value.strip()

        return {
            "title": meta.get("Title", path.stem),
            "author": meta.get("Author", "Unknown"),
            "pages": int(meta.get("Pages", 0))
        }

    except Exception:
        return {
            "title": path.stem,
            "author": "Unknown",
            "pages": 0
        }


# =========================
# Core Index Builder
# =========================

def build_index(directory: Path):

    pdf_files = list(directory.rglob("*.pdf"))

    records = []

    for pdf in pdf_files:
        size_bytes = pdf.stat().st_size
        metadata = extract_pdf_metadata(pdf)

        record = {
            "filename": pdf.name,
            "path": str(pdf.resolve()),
            "size_bytes": size_bytes,
            "size_human": human_size(size_bytes),
            "pages": metadata["pages"],
            "title": metadata["title"],
            "author": metadata["author"],
            "sha256": sha256_file(pdf)
        }

        records.append(record)

    return records


# =========================
# Markdown Generator
# =========================

def generate_markdown(records, stats, output_path: Path):

    with output_path.open("w") as f:

        f.write("# Book Collection Index\n\n")
        f.write(f"Generated: {datetime.now(UTC).isoformat()}\n\n")

        f.write("## Summary\n\n")
        f.write(f"- Total Books: {stats['count']}\n")
        f.write(f"- Total Pages: {stats['total_pages']}\n")
        f.write(f"- Total Size: {stats['total_size_human']}\n")
        f.write(f"- Average Pages per Book: {stats['avg_pages']:.2f}\n")
        f.write(f"- Largest File: {stats['largest_file']}\n")
        f.write(f"- Smallest File: {stats['smallest_file']}\n\n")

        f.write("## Book List\n\n")
        f.write("| Title | Author | Pages | Size | SHA256 |\n")
        f.write("|-------|--------|-------|------|--------|\n")

        for r in sorted(records, key=lambda x: x["title"].lower()):
            f.write(
                f"| {r['title']} | {r['author']} | "
                f"{r['pages']} | {r['size_human']} | "
                f"{r['sha256'][:12]}... |\n"
            )


# =========================
# Statistics
# =========================

def compute_stats(records):

    total_pages = sum(r["pages"] for r in records)
    total_size = sum(r["size_bytes"] for r in records)
    pages_list = [r["pages"] for r in records if r["pages"] > 0]

    return {
        "count": len(records),
        "total_pages": total_pages,
        "total_size_bytes": total_size,
        "total_size_human": human_size(total_size),
        "avg_pages": statistics.mean(pages_list) if pages_list else 0,
        "largest_file": max(records, key=lambda x: x["size_bytes"])["filename"] if records else "N/A",
        "smallest_file": min(records, key=lambda x: x["size_bytes"])["filename"] if records else "N/A"
    }


# =========================
# Main
# =========================

if __name__ == "__main__":

    import argparse

    parser = argparse.ArgumentParser(description="PDF Book Index Generator")
    parser.add_argument("directory", type=str)
    parser.add_argument("--markdown", default="README.md")
    parser.add_argument("--json", default="books_index.json")

    args = parser.parse_args()

    target_dir = Path(args.directory)

    if not target_dir.is_dir():
        print("Invalid directory.")
        exit(1)

    records = build_index(target_dir)
    stats = compute_stats(records)

    generate_markdown(records, stats, Path(args.markdown))

    with open(args.json, "w") as jf:
        json.dump({
            "generated": datetime.now(UTC).isoformat(),
            "stats": stats,
            "books": records
        }, jf, indent=4)

    print(f"Indexed {stats['count']} books.")
    print(f"Markdown written to {args.markdown}")
    print(f"JSON written to {args.json}")
