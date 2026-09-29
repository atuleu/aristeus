import random
from typing import Any

import kaggle
import os
import argparse
import dataclasses
import fiftyone as fo
import tqdm
import xml.etree.ElementTree as ET
import cv2

from pathlib import Path
import aristeus

dataset_path = "./datasets/"
dataset_name = "aristeus_v0"


@dataclasses.dataclass
class KaggleDatasetDescription:
    Handle: str
    Path: list[str]
    MergeFn: Any = None

    def _basepath(self):
        return os.path.join(dataset_path, "kaggle", self.Handle)

    def _all_exists(self):
        for p in self.Path:
            testpath = os.path.join(self._basepath(), p)
            if os.path.exists(testpath) is False:
                print(f"missing '{testpath}'")
                return False

        return True

    def ensure(self):

        if self._all_exists() is True:
            return

        os.makedirs(os.path.join(self._basepath()), exist_ok=True)

        kaggle.api.dataset_download_files(
            self.Handle, path=self._basepath(), unzip=True, quiet=False, force=False
        )

    def merge(self, master):
        if self.MergeFn is None:
            return
        self.MergeFn(self, master)


def rename_samples(ds: fo.Dataset):
    mapping = {"0": "bee", "1": "pollen"}

    for sample in ds:
        modified = False
        for d in sample.ground_truth.detections:
            if d.label not in mapping:
                continue
            d.label = mapping[d.label]
            modified = True
        if modified:
            sample.save()


def merge_lara311(this: KaggleDatasetDescription, master: fo.Dataset):
    name = os.path.join("kaggle", this.Handle)
    if fo.dataset_exists(os.path.join(name, "train")):
        train = fo.load_dataset(os.path.join(name, "train"))
    else:
        train = fo.Dataset.from_dir(
            data_path=os.path.join(this._basepath(), "train/images"),
            labels_path=os.path.join(this._basepath(), "train/labels"),
            dataset_type=fo.types.YOLOv4Dataset,
            name=os.path.join(name, "train"),
            progress=True,
            persistent=True,
        )
        rename_samples(train)

    train.tag_samples("split_train")
    train.tag_samples(name)

    if fo.dataset_exists(os.path.join(name, "validation")):
        validation = fo.load_dataset(os.path.join(name, "validation"))
    else:
        validation = fo.Dataset.from_dir(
            data_path=os.path.join(this._basepath(), "valid/images"),
            labels_path=os.path.join(this._basepath(), "valid/labels"),
            dataset_type=fo.types.YOLOv4Dataset,
            name=os.path.join(name, "validation"),
            progress=True,
            persistent=True,
        )
        rename_samples(validation)

    validation.tag_samples("split_validation")
    validation.tag_samples(name)

    if fo.dataset_exists(os.path.join(name, "test")):
        test = fo.load_dataset(os.path.join(name, "test"))
    else:
        test = fo.Dataset.from_dir(
            data_path=os.path.join(this._basepath(), "test/images"),
            labels_path=os.path.join(this._basepath(), "test/labels"),
            dataset_type=fo.types.YOLOv4Dataset,
            name=os.path.join(name, "test"),
            progress=True,
            persistent=True,
        )
        rename_samples(test)

    test.tag_samples("split_test")
    test.tag_samples(name)

    master.merge_samples(train)
    master.merge_samples(validation)
    master.merge_samples(test)


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


def merge_VUT1(this: KaggleDatasetDescription, master: fo.Dataset):
    name = os.path.join("kaggle", this.Handle)

    if fo.dataset_exists(name):
        ds = fo.load_dataset(name)
    else:
        ds = fo.Dataset.from_dir(
            data_path=os.path.join(this._basepath(), "BeeDataset_VUT-1/images"),
            labels_path=os.path.join(this._basepath(), "BeeDataset_VUT-1/annotations"),
            dataset_type=fo.types.YOLOv4Dataset,
            name=name,
            progress=True,
            persistent=True,
        )
        split_samples(ds)

    for sample in ds:
        modified = False
        for d in sample.ground_truth.detections:
            d.label = "bee"
            modified = True
        if modified is True:
            sample.save()

    ds.tag_samples(name)

    master.merge_samples(ds)


def merge_MLData(this: KaggleDatasetDescription, master: fo.Dataset):
    name = os.path.join("kaggle", this.Handle)
    if fo.dataset_exists(name):
        ds = fo.load_dataset(name)
        master.merge_samples(ds)
        return

    ds = fo.Dataset(name)
    ds.persistent = True
    ds.media_type = "image"

    samples = []
    classes = {"bee": 0}
    nextID = 1
    for filename in tqdm.tqdm(os.listdir(os.path.join(this._basepath(), "ML-Data"))):
        if filename.endswith(".xml") is False:
            continue

        xml_path = os.path.join(this._basepath(), "ML-Data", filename)
        tree = ET.parse(xml_path)
        root = tree.getroot()
        img_filename = root.find("filename").text
        img_path = os.path.join(this._basepath(), "ML-Data", img_filename)

        if not os.path.exists(img_path):
            print(f"missing file'{img_filename}'")
            continue

        if cv2.imread(img_path) is None:
            print(f"could not read file '{img_filename}'")
            continue

        size = root.find("size")
        width = int(size.find("width").text)
        height = int(size.find("height").text)

        detections = []
        for obj in root.findall("object"):
            label = obj.find("name").text
            if label not in classes:
                classes[label] = nextID
                nextID += 1
            aabb = obj.find("bndbox")
            xmin = float(aabb.find("xmin").text)
            xmax = float(aabb.find("xmax").text)
            ymin = float(aabb.find("ymin").text)
            ymax = float(aabb.find("ymax").text)

            rel_x = xmin / width
            rel_y = ymin / height
            rel_w = (xmax - xmin) / width
            rel_h = (ymax - ymin) / height
            detections.append(
                fo.Detection(
                    label=str(classes[label]), bounding_box=[rel_x, rel_y, rel_w, rel_h]
                )
            )
        sample = fo.Sample(
            filepath=img_path,
            ground_truth=fo.Detections(detections=detections),
        )
        sample.tags.append(name)
        samples.append(sample)

    ds.add_samples(samples)
    rename_samples(ds)
    split_samples(ds)

    master.merge_samples(ds)


def create_from_existing(ds: fo.Dataset, path: str):
    print(f"recreating {dataset_name} from {path}")
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


def main():
    parser = argparse.ArgumentParser(
        prog="ProgramName",
        description="bootstrap the dataset for aristeus from open dataset online",
    )
    parser.add_argument("--force", action="store_true")
    args = parser.parse_args()

    if fo.dataset_exists("aristeus_v0"):
        if args.force is False:
            return
        master = fo.load_dataset(dataset_name)
        master.clear()
    else:
        master = fo.Dataset(dataset_name)
        master.persistent = True

    export_dir = os.path.join(dataset_path, dataset_name)

    if os.path.exists(export_dir):
        create_from_existing(master, export_dir)
        return

    datasets = [
        KaggleDatasetDescription(
            Handle="lara311/bee-detection-dataset",
            Path=["data.yaml"],
            MergeFn=merge_lara311,
        ),
        KaggleDatasetDescription(
            "imonbilk/bee-dataset-but-1",
            [
                "BeeDataset_VUT-1/images/Image0314 11-40-24.jpg",
                "BeeDataset_VUT-1/annotations/Image0314 11-40-24.txt",
            ],
            MergeFn=merge_VUT1,
        ),
        KaggleDatasetDescription(
            "ashfaqsyed/bees-dataset",
            [
                "ML-Data/Chueried_Churied_01_ST_203.jpg",
                "ML-Data/Chueried_Churied_01_ST_201.xml",
            ],
            MergeFn=merge_MLData,
        ),
    ]

    for ds in datasets:
        ds.ensure()
        ds.merge(master)

    aristeus.export_dataset(master, dataset_path)


if __name__ == "__main__":
    main()

    # validation: 1604 test: 836 train: 5640
