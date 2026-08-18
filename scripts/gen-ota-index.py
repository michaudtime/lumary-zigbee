#!/usr/bin/env python3
"""Generate a Zigbee2MQTT OTA override index for a built image.

Z2M does NOT discover firmware by scanning a folder -- dropping a file into
`data/ota/` does nothing on its own. It reads an index that describes the image,
pointed at by `ota.zigbee_ota_override_index_location` in configuration.yaml.
The index format is the one used by https://github.com/Koenkk/zigbee-OTA.

Usage:
    python scripts/gen-ota-index.py build/ota/1001-0001-02000000-ota-file.zigbee

Writes index.json beside the image. See README "OTA Updates".
"""

import hashlib
import json
import os
import struct
import sys

# Zigbee OTA image header, little-endian, from the ZCL OTA Upgrade spec.
# Offsets are into the file: magic(4) headerVersion(2) headerLength(2)
# fieldControl(2) manufacturerCode(2) imageType(2) fileVersion(4)
# stackVersion(2) headerString(32)
OTA_MAGIC = 0x0BEEF11E


def read_header(path):
    """Pull the identifiers out of the image itself.

    Deliberately not taken from command-line arguments: the whole failure mode
    this guards against is an index that disagrees with the image it describes,
    which presents to the user as "No image currently available" with nothing
    saying why.
    """
    with open(path, "rb") as f:
        head = f.read(56)
    if len(head) < 56:
        sys.exit(f"{path}: too short to be an OTA image")

    magic, _hdr_ver, _hdr_len, _field_ctrl, manuf, img_type, file_ver, _stack = struct.unpack(
        "<IHHHHHIH", head[:20]
    )
    if magic != OTA_MAGIC:
        sys.exit(f"{path}: bad OTA magic {magic:#010x}, expected {OTA_MAGIC:#010x}")

    header_string = head[20:52].rstrip(b"\x00").decode("utf-8", "replace")
    return manuf, img_type, file_ver, header_string


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    image = sys.argv[1]

    manuf, img_type, file_ver, header_string = read_header(image)
    data = open(image, "rb").read()
    name = os.path.basename(image)

    entry = {
        "fileName": name,
        "fileVersion": file_ver,
        "fileSize": len(data),
        # Z2M fetches this. A path under the Z2M data directory works for a
        # locally served image; an http(s) URL works for a hosted one.
        "url": name,
        "imageType": img_type,
        "manufacturerCode": manuf,
        "sha512": hashlib.sha512(data).hexdigest(),
        "otaHeaderString": header_string,
    }

    out = os.path.join(os.path.dirname(image) or ".", "index.json")
    with open(out, "w", encoding="utf-8") as f:
        json.dump([entry], f, indent=2)
        f.write("\n")

    print(f"Wrote {out}")
    print(f"  manufacturerCode {manuf:#06x} ({manuf})")
    print(f"  imageType        {img_type:#06x} ({img_type})")
    print(f"  fileVersion      {file_ver:#010x} ({file_ver})")
    print(f"  otaHeaderString  {header_string!r}")
    print(f"  fileSize         {len(data)}")


if __name__ == "__main__":
    main()
