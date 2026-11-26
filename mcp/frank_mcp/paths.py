"""Where frank-sdk lives: this package sits in <sdk>/mcp/frank_mcp."""

import os
from pathlib import Path

SDK = Path(os.environ.get("FRANK_SDK", Path(__file__).resolve().parents[2]))
BOARDS_JSON = SDK / "boards" / "gen" / "boards.json"
DOCS = SDK / "docs"
EXAMPLES = SDK / "examples"
DRIVERS = SDK / "drivers"
PLATFORM = SDK / "platform"
TEMPLATE = SDK / "template"
