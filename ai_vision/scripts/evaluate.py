import fiftyone as fo
import fiftyone.brain as fob
from sahi import AutoDetectionModel
from sahi.predict import get_sliced_prediction
import argparse

from rich.progress import track


def ensure_predicition(dataset: fo.Dataset, *, label_name: str, model_path: str):
    if dataset.has_sample_field(label_name):
        dataset.delete_sample_field(label_name)

    model = AutoDetectionModel.from_pretrained(
        model_type="ultralytics",
        model_path=model_path,
        confidence_threshold=0.3,
        device="cuda:0",
    )

    for sample in track(dataset):
        result = get_sliced_prediction(
            sample.filepath,
            model,
            slice_height=1280,
            slice_width=1280,
            overlap_height_ratio=0.2,
            overlap_width_ratio=0.2,
        )
        sample[label_name] = fo.Detections(detections=result.to_fiftyone_detections())
        sample.save()


def main():
    parser = argparse.ArgumentParser(
        prog="evaluate",
        description="evaluate a model",
    )
    parser.add_argument("--force-inference", action="store_true")
    parser.add_argument("--force-mistakenness", action="store_true")

    parser.add_argument("model_name")
    parser.add_argument("model_path")
    parser.add_argument("dataset_name")

    args = parser.parse_args()
    dataset = fo.load_dataset(args.dataset_name)

    label_name = args.model_name + "_predictions"
    if len(dataset) != len(dataset.exists(label_name)) or args.force_inference is True:
        ensure_predicition(dataset, label_name=label_name, model_path=args.model_path)

    if args.force_mistakenness is True:
        fields = [
            "mistakenness",
            "possible_missing",
            "possible_spurious",
        ]
        for f in fields:
            if dataset.has_sample_field(f):
                dataset.delete_sample_field(f)
        dataset.delete_brain_run("mistakenness")

    fob.compute_mistakenness(
        dataset,
        label_field="ground_truth",
        pred_field=label_name,
    )

    mistake_view = dataset.sort_by("mistakenness", reverse=True)

    session = fo.launch_app(dataset)
    session.view = mistake_view
    session.wait()


if __name__ == "__main__":
    main()
