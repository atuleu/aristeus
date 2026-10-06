from ultralytics import YOLO
import os
import argparse


def train_or_resume(default_weight: str, weight_dir: str, reset: bool, **kwargs):
    done_path = os.path.join(weight_dir, ".training_done")
    if os.path.exists(done_path):
        return
    last_path = os.path.join(weight_dir, "last.pt")
    if os.path.exists(last_path) and reset is False:
        model = YOLO(last_path)
        model.train(resume=True)
    else:
        model = YOLO(default_weight)
        model.train(**kwargs)

    with open(done_path, "w") as f:
        f.write("done")


def main():
    parser = argparse.ArgumentParser(prog="train", description="train a new model")
    parser.add_argument("--model", "-m", default="yolo26n.pt")
    parser.add_argument("dataset")
    parser.add_argument("--reset", action="store_true")
    parser.add_argument("--fine-tune", action="store_true")
    args = parser.parse_args()

    frozen_weights_dir = "runs/detect/train/weights"
    frozen_weights_path = os.path.join(frozen_weights_dir, "best.pt")

    train_args = {
        "mosaic": 0.25,
        "mixup": 0.0,
        "copy_paste": 0.0,
        "workers": 8,
        "cls_pw": 0.25,
    }

    train_or_resume(
        args.model,
        frozen_weights_dir,
        args.reset,
        data=args.dataset,
        epochs=100,
        imgsz=1280,
        batch=32,
        device=0,
        freeze=10,
        patience=20,
        **train_args,
    )

    if args.fine_tune is False:
        return

    fine_tune_weight_dir = "runs/detect/train-2/weights"

    train_or_resume(
        frozen_weights_path,
        fine_tune_weight_dir,
        args.reset,
        data=args.dataset,
        epochs=500,
        imgsz=1280,
        batch=16,
        device=0,
        freeze=0,
        patience=20,
        **train_args,
    )


if __name__ == "__main__":
    main()
