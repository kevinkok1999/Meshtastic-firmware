import json
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "maps"))
from build_nl import PROVINCES, build_pack

def main(path):
    root = Path(path).parent
    source = root / "reader-fixture.jsonl"
    rows = []
    kinds = ["boundary", "motorway", "trunk", "primary", "secondary", "tertiary", "local", "water", "place"]
    for i, province in enumerate(PROVINCES):
        rows.append({"id": f"way/{i}", "kind": kinds[i % len(kinds)], "province": province,
                     "name": "N225" if i % 2 else "Dorp", "zoom_min": 8,
                     "geometry": [[4.0 + i / 100, 51.0 + i / 100], [4.01 + i / 100, 51.01 + i / 100]]})
    source.write_text("\n".join(json.dumps(row, sort_keys=True) for row in rows) + "\n")
    build_pack(source, root / "../maps/nl_profile.json", path)

if __name__ == "__main__":
    main(sys.argv[1])
