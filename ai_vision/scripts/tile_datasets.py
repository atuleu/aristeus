import fiftyone as fo
from fiftyone import ViewField as F

import argparse



def main():
    parser = argparse.ArgumentParser(prog="tile_dataset",
                                     description="tile images in dataset")
    parser.add_argument("dataset_name", description="dataset name to tile")
    parser.add_argument("--image-size","-s",default = 1280)

    args = parser.parse_args()

    ds = fo.load_dataset(args.dataset_name)
    ds.compute_metadata()
    large_images = ds.match(
        (f("metadata.width")> 1280 | (F("metadata.height") > 1280)
    )



if __name__ == "__main__":
    main()
