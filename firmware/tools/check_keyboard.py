from pathlib import Path
import ast
import re

root = Path(__file__).resolve().parents[1]
source = (root / "src" / "keyboard.cpp").read_text(encoding="utf-8")
keys = set(" ")
for name in ("KB_KEYS_LOWER", "KB_KEYS_UPPER", "KB_KEYS_NUMBER"):
    match = re.search(rf"{name}\[\]\s*=\s*\{{(.*?)\}};", source, re.S)
    assert match, f"missing {name}"
    layout = [ast.literal_eval(token) for token in re.findall(r'"(?:\\.|[^"\\])*"', match[1])]
    assert len(layout) == 40, f"{name}: expected 40 keys, got {len(layout)}"
    keys.update(layout)

missing = sorted(set(map(chr, range(32, 127))) - keys)
assert not missing, f"missing printable ASCII: {missing}"
assert "#define KB_MAX_INPUT 65" in (root / "src" / "keyboard.h").read_text(encoding="utf-8")
assert "#define WIFI_PASS_CAPACITY 65" in (root / "src" / "wifi_config.h").read_text(encoding="utf-8")
print("keyboard: printable ASCII and 64-character WiFi password capacity OK")
