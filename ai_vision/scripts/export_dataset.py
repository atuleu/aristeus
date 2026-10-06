import fiftyone as fo
import aristeus
import argparse
from fiftyone import ViewField as F
import os


def main():
    parser = argparse.ArgumentParser(
        prog="export_dataset",
        description="export_dataset",
    )
    parser.add_argument("dataset")
    args = parser.parse_args()

    ds = fo.load_dataset(args.dataset)
    view = ds.match(
        ~F("tags").contains(["split_train", "split_validation", "split_test"])
    )
    if len(view) > 0:
        print(f"{len(view)} samples are missing split assignation")
        aristeus.split_samples(view)

    dest_dir = os.path.join("datasets", args.dataset)
    if os.path.exists(dest_dir):
        os.removedirs(dest_dir)

    aristeus.export_dataset(ds, dataset_dir="datasets")


if __name__ == "__main__":
    main()
