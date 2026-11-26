"""A new firmware repo from frank-sdk's template."""

from __future__ import annotations

import datetime
import json
import re
import subprocess
from pathlib import Path

from .bench import BenchError
from .paths import BOARDS_JSON, EXAMPLES, SDK, TEMPLATE

SDK_URL = "https://github.com/rh1tech/frank-sdk.git"
FEATURE_LIBS = {"video": "frank_video", "audio": "frank_audio", "storage": "frank_storage",
                "input": "frank_input", "link": "frank_link"}


def new_firmware(path: str, name: str, boards: list[str], description: str = "",
                 license: str = "MIT", features: list[str] | None = None,
                 sdk: str = "submodule") -> dict:
    """sdk: "submodule" (frank-sdk from GitHub, pinned) or "local" (a symlink to this
    frank-sdk, for work before frank-sdk is published)."""
    features = features or []
    dest = Path(path).expanduser().resolve()
    if dest.exists() and any(dest.iterdir()):
        raise BenchError(f"{dest} exists and is not empty")
    if not re.fullmatch(r"[a-z0-9][a-z0-9-]*", name):
        raise BenchError("name: lower case, digits and dashes (it becomes the repo and image name)")
    known = json.loads(BOARDS_JSON.read_text())
    boards = [b.upper() for b in boards]
    bad = [b for b in boards if b not in known]
    if bad or not boards:
        raise BenchError(f"unknown boards {bad}; one of {', '.join(known)}")
    unknown = [f for f in features if f not in FEATURE_LIBS]
    if unknown:
        raise BenchError(f"unknown features {unknown}; available: {', '.join(FEATURE_LIBS)}")
    need = {"video": lambda b: b.get("video"), "storage": lambda b: b.get("sd"),
            "audio": lambda b: b.get("audio") or b.get("video", {}).get("hdmi"),
            "link": lambda b: b.get("link")}
    for f, has in need.items():
        if f in features:
            lacking = [b for b in boards if not has(known[b])]
            if lacking:
                what = {"video": "no HDMI or other video", "storage": "no SD card",
                        "audio": "no sound output", "link": "no link"}[f]
                raise BenchError(f"{f} asked for, but {lacking} have {what}")

    subst = {
        "{{NAME}}": name,
        "{{NAME_ID}}": name.replace("-", "_"),
        "{{DESCRIPTION}}": description or name,
        "{{BOARDS}}": " ".join(boards),
        "{{BOARD0}}": boards[0],
        "{{LICENSE}}": "MIT" if license.upper() == "MIT" else license,
        "{{LIBS}}": " ".join(FEATURE_LIBS[f] for f in features) or "",
        "{{YEAR}}": str(datetime.date.today().year),
    }

    def fill(text: str) -> str:
        for k, v in subst.items():
            text = text.replace(k, v)
        return text

    dest.mkdir(parents=True, exist_ok=True)
    written = []
    for src in sorted(TEMPLATE.rglob("*")):
        if src.is_dir():
            continue
        rel = src.relative_to(TEMPLATE)
        out = dest / rel
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(fill(src.read_text()))
        out.chmod(src.stat().st_mode)
        written.append(str(rel))
    # the main program: the matching example, renamed
    # (diag adapts to what is linked: audio, storage, input, video)
    exname = "diag" if set(features) & {"audio", "storage", "input"} else "video" if "video" in features else "hello"
    main = (EXAMPLES / exname / "main.c").read_text()
    for n in ("video", "hello", "diag"):
        main = main.replace(f'frank_init("{n}", FRANK_SDK_VERSION)', "frank_init(FW_NAME, FW_VERSION)")
    main = re.sub(r"^ \* frank-sdk example: (\w+) — ", f" * {name} — from frank-sdk's example \\1: ", main, count=1, flags=re.M)
    (dest / "src").mkdir(exist_ok=True)
    (dest / "src" / "main.c").write_text(main)
    written.append("src/main.c")
    if license.upper() == "MIT":
        lic = (SDK / "LICENSE").read_text() if (SDK / "LICENSE").exists() else ""
        if lic:
            (dest / "LICENSE").write_text(lic)
            written.append("LICENSE")

    def git(*a, check=True):
        r = subprocess.run(["git", "-C", str(dest), *a], capture_output=True, text=True, timeout=120)
        if check and r.returncode:
            raise BenchError(f"git {' '.join(a)}: {r.stderr.strip()}")
        return r.stdout.strip()

    git("init", "-q", "-b", "main")
    if sdk == "local":
        (dest / "frank-sdk").symlink_to(SDK)
        note = f"frank-sdk is a symlink to {SDK} (sdk=local); make it a submodule once frank-sdk is published"
    else:
        git("submodule", "add", "-q", SDK_URL, "frank-sdk")
        tag = git("-C", "frank-sdk", "describe", "--tags", "--abbrev=0", check=False)
        if tag:
            git("-C", "frank-sdk", "checkout", "-q", tag)
        note = f"frank-sdk submodule at {tag or 'its default branch'}"
    subprocess.run([str(dest / "tools" / "install-hooks.sh")], cwd=dest, capture_output=True, timeout=60)
    return {"path": str(dest), "files": written, "sdk": note,
            "next": [f"./build.sh {boards[0]}", f"./flash.sh {boards[0]}",
                     "nothing is committed: review, then commit yourself"]}
