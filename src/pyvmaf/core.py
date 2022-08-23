import json
from . import _vmaf

try:
    from typing import Optional, Union
except ImportError:
    pass


def normalize_image(im):
    if isinstance(im, bytes):
        return im
    mode = getattr(im, "mode", None)
    has_convert = hasattr(im, "convert")
    has_tobytes = hasattr(im, "tobytes")
    if not isinstance(mode, str) or not has_convert or not has_tobytes:
        raise TypeError("Expected image to be bytes or a PIL Image")
    if mode != "YCbCr":
        im = im.convert("YCbCr")
    return im.tobytes()


SIZE_TYPE_ERROR = TypeError("Expected size to be a 2-tuple of ints as (width, height)")


def check_size_tuple(size):
    try:
        size_len = len(size)
    except (TypeError, ValueError):
        raise SIZE_TYPE_ERROR
    else:
        if size_len != 2 or not all(isinstance(sz, int) for sz in size):
            raise SIZE_TYPE_ERROR
    return size


class Vmaf(object):
    def __init__(
        self,
        model_version=None,  # type: Optional[str]
        log_level=None,  # type: Optional[Union[str, int]]
    ):
        self.model_version = model_version
        self.log_level = log_level or 0
        self.loaded_models = []
        self.added_features = []
        self._context = _vmaf.Vmaf(self.model_version, self.log_level)

    def _reset_context(self):
        self._context = _vmaf.Vmaf(self.model_version, self.log_level)
        for name, options in self.added_features:
            self._context.add_feature(name, options)
        for alias, path in self.loaded_models:
            self._context.model_load(alias, path)

    def model_load(self, alias, path):
        # type: (str, str) -> None
        self._context.model_load(alias, path)
        self.loaded_models.append((alias, path))

    def add_feature(self, name, options=None):
        options = options or {}
        self._context.add_feature(name, options or {})
        self.added_features.append((name, options))

    def calculate(self, ref_im, dist_im, size=None):
        if size is not None:
            width, height = check_size_tuple(size)
        else:
            ref_im_size = getattr(ref_im, "size", None)
            dist_im_size = getattr(dist_im, "size", None)
            size = ref_im_size or dist_im_size
            try:
                width, height = check_size_tuple(size)
            except TypeError:
                raise TypeError(
                    "If neither the reference nor the distorted images are "
                    "PIL Images, then a size kwarg of (width, height) is required"
                )
        ref_im_bytes = normalize_image(ref_im)
        dist_im_bytes = normalize_image(dist_im)
        try:
            results = self._context.calculate(
                ref_im_bytes, dist_im_bytes, width, height
            )
            data = json.loads(results)
            return data["frames"][0]["metrics"]
        finally:
            self._reset_context()
