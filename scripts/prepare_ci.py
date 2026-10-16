"""Generate CI build configurations from the example and README.

All configurations use the local checkout and dummy credentials. Nothing is
read from a user's secrets.yaml, and these configurations must not be flashed.
"""

import argparse
import base64
from copy import deepcopy
from pathlib import Path
import re

import yaml

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / ".esphome" / "ci"
PLATFORMS = ("sensor", "switch", "fan", "button", "binary_sensor")
CASES = ("parent", *PLATFORMS, "readme", "example")
DUMMY_SECRETS = {
    "api_key": base64.b64encode(bytes(32)).decode(),
    "wifi_ssid": "ci-only-do-not-flash",
    "wifi_password": "ci-wifi-password",
    "fallback_password": "ci-fallback-password",
    "ota_password": "ci-ota-password",
}


class ExampleLoader(yaml.SafeLoader):
    """Resolve only the example's known secrets to dummy values."""


def dummy_secret(loader, node):
    return DUMMY_SECRETS[loader.construct_scalar(node)]


ExampleLoader.add_constructor("!secret", dummy_secret)


def prepare(case, example):
    if case == "example":
        config = deepcopy(example)
    else:
        config = {
            key: deepcopy(example[key])
            for key in ("esphome", "esp8266", "logger", "uart", "lr500")
        }
        if case in PLATFORMS:
            config[case] = deepcopy(example[case])
        elif case == "readme":
            readme = (ROOT / "README.md").read_text(encoding="utf-8")
            match = re.search(
                r"^## YAML reference\n.*?^```yaml\n(.*?)^```",
                readme,
                flags=re.MULTILINE | re.DOTALL,
            )
            if match is None:
                raise ValueError("README YAML reference block was not found")
            config.update(yaml.safe_load(match.group(1)))

    # Override GitHub sources after every merge so CI always builds this checkout.
    config["external_components"] = [{
        "source": {"type": "local", "path": str(ROOT / "components")},
        "components": ["lr500"],
    }]
    config["esphome"]["build_path"] = str(OUTPUT / "build" / case)
    path = OUTPUT / f"{case}.yaml"
    path.write_text(yaml.safe_dump(config, sort_keys=False), encoding="utf-8")
    print(path.relative_to(ROOT))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("case", nargs="?", choices=CASES, help="default: all cases")
    args = parser.parse_args()
    example = yaml.load(
        (ROOT / "examples" / "lr500.yaml").read_text(encoding="utf-8"),
        Loader=ExampleLoader,
    )
    OUTPUT.mkdir(parents=True, exist_ok=True)
    for case in (args.case,) if args.case else CASES:
        prepare(case, example)


if __name__ == "__main__":
    main()
