# vim: tabstop=8 expandtab shiftwidth=4 softtabstop=4

"""Compact PNG output for matplotlib plots.

Plots are rendered at RENDER_SCALE times their display size without
anti-aliasing, and the page embedding them scales them back down. That leaves
only a handful of distinct colors, which compress far better as a palette image
than the blended edge pixels of anti-aliased rendering.

Importing this module disables anti-aliasing, so import it before creating the
figure. Embed the image at figure size in inches times DISPLAY_DPI.
"""

import io
from pathlib import Path

import matplotlib
from matplotlib.figure import Figure
from PIL import Image

DISPLAY_DPI = 100
RENDER_SCALE = 2

# Artists read these on construction, so set them before plotting
matplotlib.rcParams["lines.antialiased"] = False
matplotlib.rcParams["patch.antialiased"] = False
matplotlib.rcParams["text.antialiased"] = False


def save_png(fig: Figure, path: Path) -> None:
    buf = io.BytesIO()
    fig.savefig(buf, format="png", dpi=DISPLAY_DPI * RENDER_SCALE)
    image = Image.open(buf).convert("RGB")
    # without anti-aliasing the colors are exact and few, so this is lossless.
    # Only a plot with a very large number of colors falls back to approximating
    colors = image.getcolors(256)
    num_colors = len(colors) if colors is not None else 256
    image.quantize(num_colors, dither=Image.Dither.NONE).save(path, optimize=True)
