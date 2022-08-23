import os

import pytest
from PIL import Image

from pyvmaf import Vmaf


@pytest.fixture
def ref_im():
    return Image.new("RGB", (1920, 1080), (16, 16, 16))


@pytest.fixture
def dis_im():
    return Image.new("RGB", (1920, 1080), (25, 25, 25))


def test_calculate(ref_im, dis_im):
    vmaf = Vmaf()
    metrics = vmaf.calculate(ref_im, dis_im)
    assert ("%.4f" % metrics["vmaf"]) == "97.4280"


def test_calculate_bytes():
    vmaf = Vmaf()
    ref_im = b"\x10\x80\x80" * (1920 * 1080)
    dis_im = b"\x18\x80\x80" * (1920 * 1080)
    metrics = vmaf.calculate(ref_im, dis_im, (1920, 1080))
    assert ("%.4f" % metrics["vmaf"]) == "97.4280"


def test_model_load(ref_im, dis_im):
    vmaf = Vmaf()
    vmaf.model_load("vmaf_neg", "vmaf_v0.6.1neg")
    metrics = vmaf.calculate(ref_im, dis_im)
    assert ("%.4f" % metrics["vmaf_neg"]) == "97.4280"


def test_add_feature(ref_im, dis_im):
    vmaf = Vmaf()
    vmaf.add_feature("psnr")
    metrics = vmaf.calculate(ref_im, dis_im)
    psnr_metrics = {k: "%.3f" % v for k, v in metrics.items() if k.startswith("psnr_")}
    assert psnr_metrics == {
        "psnr_y": "30.069",
        "psnr_cb": "60.000",
        "psnr_cr": "60.000",
    }
