import os

import numpy as np
from tiledb.vector_search.module import *


class Index:
    def query(self, targets: np.ndarray, k=10, nqueries=10, nthreads=8, nprobe=1):
        raise NotImplementedError


class FlatIndex(Index):
    def __init__(self,
                 uri,
                 dtype: np.dtype,
                 parts_name="parts.tdb"):
        self.uri = uri
        self.dtype = dtype
        self._index = None

        self._db = load_as_matrix(os.path.join(uri, parts_name))

    def query(self, targets: np.ndarray, k=10, nqueries=10, nthreads=8, nprobe=1):
        # TODO:
        # - typecheck targets
        # - don't copy the array
        # - add all the options and query strategies

        assert targets.dtype == np.float32

        # TODO: make Matrix constructor from py::array. This is ugly (and copies).
        # Create a Matrix from the input targets
        targets_m = ColMajorMatrix_f32(*targets.shape)
        targets_m_a = np.array(targets_m, copy=False)
        targets_m_a[:] = targets

        r = query_vq(self._db, targets_m, k, nqueries, nthreads)
        return np.array(r)


class IVFFlatIndex(Index):
    def __init__(self,
                 uri,
                 dtype: np.dtype):
        self.parts_db_uri = os.path.join(uri, "parts.tdb")
        self.centroids_uri = os.path.join(uri, "centroids.tdb")
        self.index_uri = os.path.join(uri, "index.tdb")
        self.ids_uri = os.path.join(uri, "ids.tdb")
        self.dtype = dtype

        ctx = Ctx({})  # TODO pass in a context
        # TODO self._db = load_as_matrix(self.db_uri)
        self._centroids = load_as_matrix(self.centroids_uri)
        self._index = read_vector_u64(ctx, self.index_uri)
        # self._ids = load_as_matrix(self.ids_uri)

    def query(self, targets: np.ndarray, k=10, nqueries=10, nthreads=8, nprobe=1):
        assert targets.dtype == np.float32

        # TODO: use Matrix constructor from py::array
        targets_m = ColMajorMatrix_f32(*targets.shape)
        targets_m_a = np.array(targets_m, copy=False)
        targets_m_a[:] = targets

        r = query_kmeans(
            self.dtype,
            self.parts_db_uri,
            self._centroids,
            targets_m,
            self._index,
            self.ids_uri,
            nprobe=nprobe,
            k_nn=k,
            nth=True,  # ??
            nthreads=nthreads,
        )
        return np.array(r)
