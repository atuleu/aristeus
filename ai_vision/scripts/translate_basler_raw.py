import argparse
import os

import cv2
import numpy as np

from rich.progress import track


def open_raw_file(path: str, *, width: int, height: int):
    expected_size = width * height
    with open(path, "rb") as f:
        raw_data = f.read()
        if len(raw_data) != expected_size:
            raise RuntimeError(
                (
                    f"unexpected size for '{path}'. "
                    f"raw Bayer RG8 {width}x{height} should be"
                    f"{expected_size} but got {len(raw_data)}"
                )
            )

        img = np.frombuffer(raw_data, dtype=np.uint8)
        return cv2.cvtColor(img.reshape((height, width)), cv2.COLOR_BayerBG2BGR)


def main():
    parser = argparse.ArgumentParser(
        prog="translate_basler_raw", description="translate basler raw files to jpg"
    )

    parser.add_argument("input_directory")
    parser.add_argument("output_directory")
    parser.add_argument("--width", "-W", default=3840)
    parser.add_argument("--height", "-H", default=2160)

    args = parser.parse_args()

    raw_files = sorted(
        [f for f in os.listdir(args.input_directory) if f.endswith(".raw")]
    )

    for i, f in enumerate(track(raw_files)):
        filepath = os.path.join(args.input_directory, f)
        img = open_raw_file(filepath, width=args.width, height=args.height)
        destination = os.path.join(args.output_directory, f"frame_{i:05d}.jpg")
        cv2.imwrite(destination, img)


if __name__ == "__main__":
    main()
