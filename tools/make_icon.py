#!/usr/bin/env python3
"""Make the launcher icons by cropping Nyan Cat out of the rendered title screen.
Usage: tools/make_icon.py build/host/title.png   (run tools/host_render first)"""
import sys
from PIL import Image

src = Image.open(sys.argv[1] if len(sys.argv) > 1 else "build/host/title.png").convert("RGB")
crop = src.crop((58, 12, 255, 209))
for d, px in {"mdpi": 48, "hdpi": 72, "xhdpi": 96, "xxhdpi": 144, "xxxhdpi": 192}.items():
    import os
    os.makedirs(f"res/mipmap-{d}", exist_ok=True)
    crop.resize((px, px), Image.LANCZOS).save(f"res/mipmap-{d}/ic_launcher.png")
