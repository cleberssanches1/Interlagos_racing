import json
import sys
from pathlib import Path

if len(sys.argv) != 2:
    print("Usage: minify_json.py <path>", file=sys.stderr)
    sys.exit(1)

path = Path(sys.argv[1])
text = path.read_text(encoding="utf-8-sig")
data = json.loads(text)
path.write_text(json.dumps(data, separators=(",", ":")), encoding="utf-8")
