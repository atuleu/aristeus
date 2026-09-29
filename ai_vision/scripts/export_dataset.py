import fiftyone as fo
import aristeus
import argparse


def main():
    parser = argparse.ArgumentParser(
        prog="export_dataset",
        description="export_dataset",
    )
    parser.add_argument("dataset")
    args = parser.parse_args()

    ds = fo.load_dataset(args.dataset)
    aristeus.export_dataset(ds, dataset_dir="datasets")


if __name__ == "__main__":
    main()
