import fiftyone as fo
import fiftyone.brain as fob
from ultralytics import YOLO


def main():
    dataset = fo.load_dataset("aristeus_v0")

    if len(dataset) != len(dataset.exists("aristeus_v0")):
        model = YOLO("runs/detect/train-2/weights/best.pt")
        print("applying model")
        dataset.apply_model(
            model, label_field="aristeus_v0", batch_size=64, num_workers=24
        )

    fob.compute_mistakenness(dataset, "aristeus_v0", label_field="ground_truth")

    new_version = dataset.clone("aristeus_v1")
    new_version.persistent = True

    mistake_view = new_version.sort_by("mistakenness", reverse=True)

    session = fo.launch_app(new_version)
    session.view = mistake_view
    session.wait()


if __name__ == "__main__":
    main()
