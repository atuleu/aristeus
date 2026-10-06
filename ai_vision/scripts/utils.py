from __future__ import annotations

import math
from typing import List, Tuple
import dataclasses


@dataclasses.dataclass
class Rect:
    xmin: int
    ymin: int
    xmax: int
    ymax: int

    def __post_init__(self):
        if self.xmin > self.xmax:
            self.xmin, self.xmax = self.xmax, self.xmin
        if self.ymin > self.ymax:
            self.ymin, self.ymax = self.ymax, self.ymin

    def intersect(self, other: Rect):
        if (
            self.xmin >= other.xmax
            or self.xmax <= other.xmin
            or self.ymin >= other.ymax
            or self.ymax <= other.ymin
        ):
            return Rect(0, 0, 0, 0)

        return Rect(
            max(self.xmin, other.xmin),
            max(self.ymin, other.ymin),
            min(self.xmax, other.xmax),
            min(self.ymax, other.ymax),
        )

    @property
    def width(self) -> int:
        return self.xmax - self.xmin

    @property
    def height(self) -> int:
        return self.ymax - self.ymin

    @property
    def area(self) -> int:
        return self.width * self.height


def tile_size(
    originalSize: Tuple[int, int], *, size: int = 1280, minOverlap=280
) -> List[Rect]:
    if minOverlap >= size:
        raise ValueError("overlap should be smaller than size")
    width, height = originalSize

    tileX = math.ceil((width - minOverlap) / (size - minOverlap))
    if tileX > 1:
        overlapX = (tileX * size - width) // (tileX - 1)
        sizeX = size
    else:
        overlapX = 0
        sizeX = width

    tileY = math.ceil((height - minOverlap) / (size - minOverlap))
    if tileY > 1:
        overlapY = (tileY * size - height) // (tileY - 1)
        sizeY = size
    else:
        overlapY = 0
        sizeY = height

    res = []
    for i in range(tileX):
        xstart = i * (sizeX - overlapX)
        if xstart + sizeX > width:
            xstart = width - sizeX
        for j in range(tileY):
            ystart = j * (sizeY - overlapY)
            if ystart + sizeY > height:
                ystart = height = sizeY
            res.append(
                Rect(xmin=xstart, ymin=ystart, xmax=xstart + sizeX, ymax=ystart + sizeY)
            )
    return res
