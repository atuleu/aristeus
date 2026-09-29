from ultralytics import YOLO
import os
import argparse


def main():
    parser = argparse.ArgumentParser(prog="train", description="train a new model")
    parser.add_argument("--model", "-m", default="yolo26n.pt")
    parser.add_argument("dataset")
    parser.add_argument("--reset", action="store_true")
    args = parser.parse_args()

    frozen_weights_path = "runs/detect/train/weights/best.pt"

    if os.path.exists(frozen_weights_path) is False or args.reset is True:
        print("initial training with frozen layer")
        model = YOLO(args.model)

        results = model.train(
            data=args.dataset,
            epochs=100,
            imgsz=640,
            batch=64,
            device=0,
            freeze=10,
            patience=10,
        )

    fine_tune_path = "runs/detect/train-2/weights/last.pt"

    if os.path.exists(fine_tune_path) is True and args.reset is False:
        model = YOLO(fine_tune_path)
        model.train(resume=True)
    else:
        model = YOLO(frozen_weights_path)
        fine_tune_result = model.train(
            data="./datasets/aristeus_v0/dataset.yaml",
            epochs=500,
            imgsz=640,
            batch=64,
            device=0,
            freeze=0,
            patience=20,
        )


if __name__ == "__main__":
    main()
