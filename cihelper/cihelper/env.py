from typing import Mapping
from typing import Sequence

from . import boost


def get() -> Mapping[str, str]:
    return {
        "BOOST_ROOT": str(boost.get_root()),
    }


def get_path() -> Sequence[str]:
    return [str(boost.get_root())]
