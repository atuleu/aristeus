import fiftyone as fo
import argparse
import fiftyone.brain as fob
import numpy as np

from ultralytics import YOLO

import re

import aristeus


def zcore_score(
    embeddings, n_sample=10000, sample_dim=2, redund_nn=100, redund_exp=4, seed=42
):
    """
    Compute ZCore scores for coverage-based sample selection.

    Reference implementation from https://github.com/voxel51/zcore

    Args:
        embeddings: np.array of shape (n_samples, embedding_dim)
        n_sample: Number of random samples to draw
        sample_dim: Number of dimensions to sample at a time
        redund_nn: Number of nearest neighbors for redundancy penalty
        redund_exp: Exponent for distance-based redundancy penalty
        seed: Random seed for reproducibility

    Returns:
        Normalized scores (0-1) where higher = more valuable for labeling
    """
    np.random.seed(seed)

    n = len(embeddings)
    n_dim = embeddings.shape[1]

    emb_min = np.min(embeddings, axis=0)
    emb_max = np.max(embeddings, axis=0)
    emb_med = np.median(embeddings, axis=0)

    scores = np.random.uniform(0, 1, n)

    for i in range(n_sample):
        if i % 2000 == 0:
            print(f"  ZCore progress: {i}/{n_sample}")

        dim = np.random.choice(n_dim, min(sample_dim, n_dim), replace=False)
        sample = np.random.triangular(emb_min[dim], emb_med[dim], emb_max[dim])

        embed_dist = np.sum(np.abs(embeddings[:, dim] - sample), axis=1)
        idx = np.argmin(embed_dist)
        scores[idx] += 1

        cover_sample = embeddings[idx, dim]
        nn_dist = np.sum(np.abs(embeddings[:, dim] - cover_sample), axis=1)
        nn = np.argsort(nn_dist)[1:]

        if nn_dist[nn[0]] == 0:
            scores[nn[0]] -= 1
        else:
            nn = nn[:redund_nn]
            dist_penalty = 1 / (nn_dist[nn] ** redund_exp + 1e-8)
            dist_penalty /= np.sum(dist_penalty)
            scores[nn] -= dist_penalty

    scores = (scores - np.min(scores)) / (np.max(scores) - np.min(scores) + 1e-8)
    return scores.astype(np.float32)


def add_zcore(ds: fo.Dataset):
    no_score = [s for s in ds if s["zcore"] is None]
    if len(no_score) == 0:
        print("all images are z-scored, skipping")
        return

    fob.compute_visualization(
        ds, embeddings="embeddings", brain_key="img_viz", verbose=True
    )

    embeddings = np.array([s.embeddings for s in ds if s.embeddings is not None])
    valid_samples = [s for s in ds if s.embeddings is not None]

    print(f"{len(ds) - len(valid_samples)} invalid samples")

    scores = zcore_score(
        embeddings,
        n_sample=len(embeddings) // 3,
        sample_dim=2,
        redund_nn=50,
        redund_exp=4,
        seed=4225,
    )
    for sample, score in zip(valid_samples, scores):
        sample["zcore"] = float(score)
        sample.save()


def main():
    parser = argparse.ArgumentParser(
        prog="smart_sample_selection", description="Smart sample selection"
    )
    parser.add_argument("dataset_name")
    parser.add_argument("--size", "-s", default=100)
    parser.add_argument("--model", "-m", default="runs/detect/train-2/weights/best.pt")
    parser.add_argument("--index", "-i", default=0)
    parser.add_argument("--force-annotation", action="store_true")
    parser.add_argument("--force-use-annotation", action="store_true")
    parser.add_argument("--merge-with-dataset", default="")

    args = parser.parse_args()

    dataset = fo.load_dataset(args.dataset_name)
    add_zcore(dataset)

    top_k_view = (
        dataset.sort_by("zcore", reverse=True)
        .skip(int(args.index))
        .limit(int(args.size))
    )

    if dataset.has_field("pre_annotation") is False or args.force_annotation is True:
        model = YOLO(args.model)
        print("applying model")
        dataset.apply_model(
            model,
            label_field="pre_annotation",
            batch_size=64,
            num_workers=24,
            progress=True,
        )
    else:
        print("samples are already pre-annotated, skipping")

    if dataset.has_field("ground_truth") is False or args.force_use_annotation is True:
        for sample in top_k_view:
            sample["ground_truth"] = sample["pre_annotation"]
            sample.save()
    else:
        print("samples are already labelled, skipping")

    merge_ds_name = args.merge_with_dataset
    if len(merge_ds_name) > 0:
        master = fo.load_dataset(merge_ds_name)
        m = re.search(r"(.*v)(\d+)$", merge_ds_name)
        if m:
            new_name = m.group(1) + str(int(m.group(2)) + 1)
            print(f"clonning dataset {merge_ds_name} to {new_name}")
            if fo.dataset_exists(new_name):
                fo.delete_dataset(new_name)
            master = master.clone(new_name)
            master.persistent = True

        top_k_view.tag_samples(
            str(dataset.name)
            + f"/top_{int(args.index)}_{int(args.size)+int(args.index)}"
        )
        aristeus.split_samples(top_k_view)
        master.merge_samples(top_k_view)


if __name__ == "__main__":
    main()
