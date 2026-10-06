import os
from typing import List, Optional, Tuple

import fiftyone as fo
from fiftyone import ViewField as F

import argparse

import cv2

from utils import tile_size, Rect
from pathlib import Path
from rich.progress import track
import re


def build_tile(
    sample: fo.Sample,
    img,
    index: int,
    *,
    path: str,
    tile: Rect,
) -> Optional[fo.Sample]:
    height, width, _ = img.shape
    roi = img[tile.ymin : tile.ymax, tile.xmin : tile.xmax]
    dest_path = os.path.join(
        path, str(Path(sample.filename).stem) + f"_{index:02d}.png"
    )

    detections = []
    for d in sample.ground_truth.detections:
        xmin, ymin, w, h = d.bounding_box
        bbox = Rect(
            xmin * width, ymin * height, (xmin + w) * width, (ymin + h) * height
        )
        if bbox.area == 0:
            continue
        in_image = tile.intersect(bbox)
        if (in_image.area / bbox.area) < 0.1:
            # removes small intersections
            continue
        xmin = (in_image.xmin - tile.xmin) / tile.width
        ymin = (in_image.ymin - tile.ymin) / tile.height
        w = in_image.width / tile.width
        h = in_image.height / tile.height
        detections.append(
            fo.Detection(
                label=d.label, bounding_box=[xmin, ymin, w, h], confidence=None
            )
        )

    if len(detections) == 0:
        return None

    cv2.imwrite(dest_path, roi)
    new_sample = fo.Sample(filepath=dest_path)
    new_sample.tags = list([t for t in sample.tags])
    if "tile" in new_sample.tags:
        sample.tags.remove("tile")
    else:
        new_sample.tags.append("tile")
    new_sample["tile_index"] = index
    new_sample["parent_filename"] = sample.filename
    new_sample["ground_truth"] = fo.Detections(
        detections=detections,
    )
    return new_sample


def tile_sample(
    sample: fo.Sample, *, path: str, size: int, minOverlap: int
) -> List[fo.Sample]:
    img = cv2.imread(sample.filepath)
    height, width, _ = img.shape

    tiles = tile_size((width, height), size=size, minOverlap=minOverlap)
    samples = []
    for i, tile in enumerate(tiles):
        samples.append(build_tile(sample, img, i, tile=tile, path=path))

    return [s for s in samples if s is not None]


def main():
    parser = argparse.ArgumentParser(
        prog="tile_dataset", description="tile images in dataset"
    )
    parser.add_argument("dataset_name", help="dataset name to tile")

    parser.add_argument("--tiling-threshold", "-t", default=1500)
    parser.add_argument("--tile-size", "-s", default=1280)
    parser.add_argument("--min-tile-overlap", default=280)
    parser.add_argument("--do-not-save", action="store_true")
    parser.add_argument("--show", action="store_true")
    parser.add_argument("new_sample_dir", help="where to put new sample")

    args = parser.parse_args()

    ds = fo.load_dataset(args.dataset_name)
    ds.compute_metadata()
    m = (F("metadata.width") > args.tiling_threshold) | (
        F("metadata.height") > args.tiling_threshold
    )

    if args.do_not_save:
        newDS = fo.Dataset("temporary", persistent=False, overwrite=True)
    else:
        matches = re.search(R"(.*)_v(\d+)$", args.dataset_name)
        if matches is None:
            new_name = args.dataset_name + "_tiled"
        else:
            new_name = matches.group(1) + "_tiled_v" + matches.group(2)
        newDS = fo.Dataset(new_name, persistent=True, overwrite=True)

    large_images = ds.match(m)
    small_images = ds.exclude(large_images)
    for s in small_images:
        s["parent_filename"] = s.filename
        s.save()

    newDS.add_samples(small_images)

    session = None
    if args.show is True:
        session = fo.launch_app(newDS.group_by("parent_filename"))

    os.makedirs(args.new_sample_dir, exist_ok=True)

    for s in track(large_images):
        newDS.add_samples(
            tile_sample(
                s,
                path=args.new_sample_dir,
                size=args.tile_size,
                minOverlap=args.min_tile_overlap,
            ),
            progress=False,
        )

    if session is not None:
        session.wait()


if __name__ == "__main__":
    main()
