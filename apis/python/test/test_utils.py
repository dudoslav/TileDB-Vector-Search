import os
import numpy as np
from tiledb.vector_search.utils import load_fvecs, load_ivecs, write_fvecs, write_ivecs

def test_load_and_write_vecs(tmp_path):
    fvecs_uri = "test/data/siftsmall/siftsmall_base.fvecs"
    ivecs_uri = "test/data/siftsmall/siftsmall_groundtruth.ivecs"

    fvecs = load_fvecs(fvecs_uri)
    assert fvecs.shape == (10000, 128)
    assert not np.any(np.isnan(fvecs))

    ivecs = load_ivecs(ivecs_uri)
    assert ivecs.shape == (100, 100)
    assert not np.any(np.isnan(ivecs))

    fvecs_uri = os.path.join(tmp_path, "fvecs")
    ivecs_uri = os.path.join(tmp_path, "ivecs")

    write_fvecs(fvecs_uri, fvecs[:10])
    write_ivecs(ivecs_uri, ivecs[:10])

    new_fvecs = load_fvecs(fvecs_uri)
    assert new_fvecs.shape == (10, 128)
    assert not np.any(np.isnan(fvecs))

    new_ivecs = load_ivecs(ivecs_uri)
    assert new_ivecs.shape == (10, 100)
    assert not np.any(np.isnan(ivecs))

