import fiftyone as fo
import tqdm
import os


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
