import argparse
import csv
import plotille


def parse_args():
    parser = argparse.ArgumentParser(
        description="Terminal plotter for YOLO training results."
    )
    parser.add_argument(
        "--csv",
        type=str,
        default="runs/detect/train/results.csv",
        help="Path to YOLO results.csv",
    )
    return parser.parse_args()


WIDTH = 80
HEIGHT = 15


def main():
    args = parse_args()

    epochs, map50, map50_95, loss = [], [], [], []

    with open(args.csv, "r") as f:
        reader = csv.DictReader(f)
        for row in reader:
            # Clean leading/trailing spaces from column keys and values
            row = {k.strip(): v.strip() for k, v in row.items()}
            epochs.append(float(row["epoch"]))
            map50.append(float(row["metrics/mAP50(B)"]))
            map50_95.append(float(row["metrics/mAP50-95(B)"]))
            loss.append(float(row["train/box_loss"]))

    boxFig = plotille.Figure()
    boxFig.width = WIDTH
    boxFig.height = HEIGHT
    if len(epochs) > 1:
        boxFig.set_x_limits(min(epochs), max(epochs))
        boxFig.set_y_limits(max(min(loss) - 0.1, 0.0), max(loss) + 0.1)
    else:
        boxFig.set_x_limits(0, 1)
        boxFig.set_y_limits(0, 1)
    boxFig.color = True
    boxFig.plot(epochs, loss, label="box_loss", lc="green")

    mAPFig = plotille.Figure()
    mAPFig.width = WIDTH
    mAPFig.height = HEIGHT
    if len(epochs) > 1:
        mAPFig.set_x_limits(min(epochs), max(epochs))
        mAPFig.set_y_limits(0, min(1.0, max(map50_95 + map50) + 0.1))
    else:
        mAPFig.set_x_limits(0, 1)
        mAPFig.set_y_limits(0, 1)
    mAPFig.color = True
    # Plot both series on the same canvas
    mAPFig.plot(epochs, map50, label="mAP50", lc="green")
    mAPFig.plot(epochs, map50_95, label="mAP50-95", lc="cyan")

    print(boxFig.show(legend=True))
    print("\n" + ("=" * 65) + "\n")
    print(mAPFig.show(legend=True))


if __name__ == "__main__":
    main()
