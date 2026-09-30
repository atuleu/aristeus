import math
from typing import Tuple


def tile_size(originalSize: Tuple[int, int], *, size: int = 1280, minOverlap=280):
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
            res.append((xstart, ystart, sizeX, sizeY))
    return res
