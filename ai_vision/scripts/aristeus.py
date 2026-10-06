from typing import Tuple

import fiftyone as fo
import tqdm
import os
import math

from pathlib import Path
import random


def split_samples(ds: fo.Dataset):
    random.seed(4224)
    sample_ids = ds.values("id")
    random.shuffle(sample_ids)
    total = len(sample_ids)
    train_end = int(0.7 * total)
    valid_end = train_end + int(0.2 * total)

    ds.select(sample_ids[:train_end]).tag_samples("split_train")
    ds.select(sample_ids[train_end:valid_end]).tag_samples("split_validation")
    ds.select(sample_ids[valid_end:]).tag_samples("split_test")

    ds.save()


def make_symlinks_relative(export_dir):
    converted_count = 0
    for dirpath, _, filenames in tqdm.tqdm(os.walk(export_dir)):
        for filename in filenames:
            file_path = os.path.join(dirpath, filename)

            # Check if the file is a symbolic link
            if os.path.islink(file_path) is False:
                continue

            current_target = os.readlink(file_path)

            # If it's an absolute path, convert it
            if os.path.isabs(current_target) is False:
                continue

            # Calculate the relative path from the symlink's directory to the target file
            rel_target = os.path.relpath(current_target, dirpath)

            # Recreate the symlink as relative
            os.unlink(file_path)
            os.symlink(rel_target, file_path)
            converted_count += 1


def export_dataset(ds: fo.Dataset, dataset_dir: str):
    export_dir = os.path.join(dataset_dir, str(ds.name))
    for name, split in {"train": "train", "validation": "val", "test": "test"}.items():
        view = ds.match_tags(f"split_{name}")
        view.export(
            export_dir=export_dir,
            dataset_type=fo.types.YOLOv5Dataset,
            label_field="ground_truth",
            export_media="symlink",
            split=split,
            classes=["bee", "pollen"],
        )

    make_symlinks_relative(export_dir)


def create_from_existing(ds: fo.Dataset, path: str):
    print(f"recreating {ds.name} from {path}")
    train = fo.Dataset.from_dir(
        dataset_dir=path, split="train", dataset_type=fo.types.YOLOv5Dataset
    )
    train.tag_samples("split_train")
    validation = fo.Dataset.from_dir(
        dataset_dir=path, split="val", dataset_type=fo.types.YOLOv5Dataset
    )
    validation.tag_samples("split_validation")
    test = fo.Dataset.from_dir(
        dataset_dir=path, split="test", dataset_type=fo.types.YOLOv5Dataset
    )
    test.tag_samples("split_test")
    ds.merge_samples(train)
    ds.merge_samples(test)
    ds.merge_samples(validation)

    for sample in tqdm.tqdm(ds):
        target = Path(os.readlink(sample.filepath))
        try:
            idx = target.parts.index("kaggle")
            origin = str(Path(*target.parts[idx : idx + 3]))
            sample.tags.append(origin)
            sample.save()
        except ValueError:
            print(f"No kaggle found in '{sample.filepath}'")

    total = len(ds)
    train_size = len(ds.match_tags("split_train"))
    val_size = len(ds.match_tags("split_validation"))
    test_size = len(ds.match_tags("split_test"))
    print(
        f"loaded {total} , train: {train_size} ({100.0*(train_size/total):.2f}%)"
        f" validation: {val_size} ({100.0*(val_size/total):.2f}%)"
        f" test: {test_size} ({100.0*(test_size/total):.2f}%)"
    )
