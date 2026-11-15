import pathlib
import shutil
import subprocess
import sys
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parent.parent
RAW_DIR = ROOT / "data" / "raw"
OUT_DICT = ROOT / "data" / "liteime.dict"
DIST_DIR = ROOT / "dist"
COMPILER = ROOT / "build" / "dict_compiler.exe"

URLS = {
    "8105.dict.yaml": ("cn", "https://raw.githubusercontent.com/iDvel/rime-ice/main/cn_dicts/8105.dict.yaml"),
    "base.dict.yaml": ("cn", "https://raw.githubusercontent.com/iDvel/rime-ice/main/cn_dicts/base.dict.yaml"),
    "en.dict.yaml": ("en", "https://raw.githubusercontent.com/iDvel/rime-ice/main/en_dicts/en.dict.yaml"),
    "en_ext.dict.yaml": ("en", "https://raw.githubusercontent.com/iDvel/rime-ice/main/en_dicts/en_ext.dict.yaml"),
}

def main() -> int:
    RAW_DIR.mkdir(parents=True, exist_ok=True)
    if not COMPILER.exists():
        print(f"Missing compiler: {COMPILER}. Run cmake --build build first.", file=sys.stderr)
        return 1

    cmd = [str(COMPILER), str(OUT_DICT)]
    for filename, (kind, url) in URLS.items():
        dst = RAW_DIR / filename
        if not dst.exists():
            print(f"Downloading {url} -> {dst}...")
            urllib.request.urlretrieve(url, dst)
        else:
            print(f"Using cached {dst}")
        cmd.extend([f"--{kind}", str(dst)])

    print("Running dict_compiler...")
    rc = subprocess.call(cmd)
    if rc != 0:
        return rc

    # Stage clean runtime bundle in D:\LiteIME\dist
    (DIST_DIR / "data").mkdir(parents=True, exist_ok=True)
    shutil.copy2(ROOT / "build" / "LiteIME64.dll", DIST_DIR / "LiteIME64.dll")
    shutil.copy2(ROOT / "build" / "LiteIMEHost.exe", DIST_DIR / "LiteIMEHost.exe")
    shutil.copy2(OUT_DICT, DIST_DIR / "data" / "liteime.dict")
    print(f"Staged runtime bundle at {DIST_DIR}")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
