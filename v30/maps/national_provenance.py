#!/usr/bin/env python3
"""Record source and executable identities for the national candidate CI."""
import datetime
import importlib.metadata
import json
import os
from pathlib import Path
import subprocess
import sys


def main():
    coverage_path, output = map(Path, sys.argv[1:])
    coverage = json.loads(coverage_path.read_text())
    provenance = {
        "production": False,
        "source_date": os.environ["SOURCE_DATE"],
        "osm_source_url": os.environ["PBF_URL"],
        "osm_license": "ODbL-1.0",
        "osm_original_sha256": os.environ["EXPECTED_PBF"],
        "osm_official_published_md5": os.environ["OFFICIAL_MD5"],
        "source_hash_review_mode": "bootstrap-observed" if os.environ["BOOTSTRAP"] == "true" else "reviewed-exact-sha256",
        "osm_filtered_sha256": coverage["filtered_input_sha256"],
        "osm_replication_timestamp": os.environ["DATA_TIMESTAMP"],
        "boundary_url": os.environ["BOUNDARY_URL"],
        "boundary_license": "CC-BY-4.0",
        "boundary_sha256": coverage["boundaries_input_sha256"],
        "boundary_collection_metadata_url": os.environ["BOUNDARY_METADATA_URL"],
        "retrieved_at": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "source_commit": os.environ["GITHUB_SHA"],
        "osmium_tool": subprocess.check_output(["osmium", "--version"], text=True),
        "osmium_python_version": importlib.metadata.version("osmium"),
        "shapely_version": importlib.metadata.version("shapely"),
        "profile_and_pack_report": "candidate.meshmap.report.json",
        "pack_sha256": coverage["pack_sha256"],
        "padded_image_sha256": coverage["image_sha256"],
        "attribution": coverage["attribution"],
        "coverage_class": coverage["coverage_class"],
        "note": "Unsigned dev pack: candidate sampling and simplification do not establish road or address completeness."
    }
    output.write_text(json.dumps(provenance, sort_keys=True, indent=2) + "\n")


if __name__ == "__main__":
    main()
