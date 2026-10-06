import cv2
from pathlib import Path
import fiftyone as fo
import argparse
from rich.progress import track


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("dataset_name")

    args = parser.parse_args()

    ds = fo.load_dataset(args.dataset_name)
    for s in track(ds.match_tags("need_flip")):
        filepath = Path(s.filepath).resolve()
        if not filepath.exists():
            print(f"file does not exists: {filepath}")
            continue

        img = cv2.imread(str(filepath))
        if img is None:
            print(f"could not read {filepath}")
            continue

        rotated = cv2.rotate(img, cv2.ROTATE_180)
        cv2.imwrite(str(filepath), rotated)


if __name__ == "__main__":
    main()
