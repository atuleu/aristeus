import fiftyone as fo
import argparse
import random
from pathlib import Path
from rich.progress import track


def main():
    parser = argparse.ArgumentParser(
        prog="add_captured_dataset", description="import in fiftyone a captured dataset"
    )
    parser.add_argument("input_directory")
    parser.add_argument("--force", "-f", action="store_true")
    parser.add_argument("--batch-size", "-b", default=100)
    args = parser.parse_args()

    target = Path(args.input_directory)

    if target.parts[0] != "datasets":
        raise RuntimeError("should be in dataset")

    dataset_name = str(Path(*target.parts[1:]))

    if fo.dataset_exists(dataset_name):
        if args.force is False:
            raise RuntimeError(f"dataset '{dataset_name}' exist")
        dataset = fo.load_dataset(dataset_name)
        dataset.clear()
    else:
        dataset = fo.Dataset(dataset_name)
        dataset.persistent = True

    all_images = sorted([str(p.resolve()) for p in target.glob("*.jpg")])
    samples = [fo.Sample(filepath=p) for p in all_images]
    dataset.add_samples(samples)


if __name__ == "__main__":
    main()
