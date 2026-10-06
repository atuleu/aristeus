from sahi import AutoDetectionModel
from sahi.predict import get_sliced_prediction

import argparse
import os


def main():
    parser = argparse.ArgumentParser()

    parser.add_argument("--model", default="runs/detect/train-2/weights/best.pt")
    parser.add_argument("--model-size", default=1280)
    parser.add_argument("image_path")

    args = parser.parse_args()

    model = AutoDetectionModel.from_pretrained(
        model_type="ultralytics",
        model_path=args.model,
        confidence_threshold=0.3,
        device="cuda:0",
    )

    result = get_sliced_prediction(
        args.image_path,
        model,
        slice_height=args.model_size,
        slice_width=args.model_size,
        overlap_height_ratio=0.2,
        overlap_width_ratio=0.2,
    )
    export_dir = "/workspace/runs/sahi/"
    os.makedirs(export_dir, exist_ok=True)
    result.export_visuals(export_dir=export_dir)


if __name__ == "__main__":
    main()
