import os
import random
import string

import numpy as np

import tiledb


def xbin_mmap(fname, dtype):
    n, d = map(int, np.fromfile(fname, dtype="uint32", count=2))
    assert os.stat(fname).st_size == 8 + n * d * np.dtype(dtype).itemsize
    return np.memmap(fname, dtype=dtype, mode="r", offset=8, shape=(n, d))


def get_groundtruth_ivec(file, k=None, nqueries=None):
    vfs = tiledb.VFS()
    vector_values = 1 + k
    vector_size = vector_values * 4
    read_size = nqueries
    read_offset = 0
    with vfs.open(file, "rb") as f:
        f.seek(read_offset)
        return (
            np.delete(
                np.reshape(
                    np.frombuffer(
                        f.read(read_size * vector_size),
                        count=read_size * vector_values,
                        dtype=np.int32,
                    ).astype(np.int32),
                    (read_size, k + 1),
                ),
                0,
                axis=1,
            ),
            None,
        )


def get_queries(dataset_dir, dtype, nqueries=None):
    fname = os.path.join(dataset_dir, "queries")
    x = xbin_mmap(fname, dtype=dtype)
    if nqueries is not None:
        return x.astype(np.float32)[:nqueries, :]
    else:
        return x.astype(np.float32)


def get_groundtruth(dataset_dir, k=None, nqueries=None):
    I, D = groundtruth_read(dataset_dir, nqueries)
    if k is not None:
        assert k <= 100
        I = I[:, :k]
        D = D[:, :k]
    return I, D


def groundtruth_read(dataset_dir, nqueries=None):
    fname = os.path.join(dataset_dir, "gt")
    n, d = map(int, np.fromfile(fname, dtype="uint32", count=2))
    assert os.stat(fname).st_size == 8 + n * d * (4 + 4)
    f = open(fname, "rb")
    f.seek(4 + 4)
    I = np.fromfile(f, dtype="uint32", count=n * d).reshape(n, d).astype("uint64")
    D = np.fromfile(f, dtype="float32", count=n * d).reshape(n, d)

    if nqueries is not None:
        return I[:nqueries, :], D[:nqueries, :]
    else:
        return I, D

def create_random_dataset_f32_only_data(nb, d, centers, path):
    """
    Creates a random float32 dataset containing just a dataset and then writes it to disk.

    Parameters
    ----------
    nb: int
        Number of points in the dataset
    d: int
        Dimension of the dataset
    nq: int
        Number of centers
    path: str
        Path to write the dataset to
    """
    from sklearn.datasets import make_blobs

    os.mkdir(path)
    X, _ = make_blobs(n_samples=nb, n_features=d, centers=centers, random_state=1)

    with open(os.path.join(path, "data.f32bin"), "wb") as f:
        np.array([nb, d], dtype="uint32").tofile(f)
        X.astype("float32").tofile(f)

def create_random_dataset_f32(nb, d, nq, k, path):
    """
    Creates a random float32 dataset containing both a dataset and queries against it, and then writes those to disk.

    Parameters
    ----------
    nb: int
        Number of points in the dataset
    d: int
        Dimension of the dataset
    nq: int
        Number of queries
    k: int
        Number of nearest neighbors to return
    path: str
        Path to write the dataset to
    """
    import sklearn.model_selection
    from sklearn.datasets import make_blobs
    from sklearn.neighbors import NearestNeighbors

    # print(f"Preparing datasets with {nb} random points and {nq} queries.")
    os.mkdir(path)
    X, _ = make_blobs(n_samples=nb + nq, n_features=d, centers=nq, random_state=1)

    data, queries = sklearn.model_selection.train_test_split(
        X, test_size=nq, random_state=1
    )

    with open(os.path.join(path, "data.f32bin"), "wb") as f:
        np.array([nb, d], dtype="uint32").tofile(f)
        data.astype("float32").tofile(f)
    with open(os.path.join(path, "queries"), "wb") as f:
        np.array([nq, d], dtype="uint32").tofile(f)
        queries.astype("float32").tofile(f)

    # print("Computing groundtruth")

    nbrs = NearestNeighbors(n_neighbors=k, metric="euclidean", algorithm="brute").fit(
        data
    )
    D, I = nbrs.kneighbors(queries)
    with open(os.path.join(path, "gt"), "wb") as f:
        np.array([nq, k], dtype="uint32").tofile(f)
        I.astype("uint32").tofile(f)
        D.astype("float32").tofile(f)


def create_random_dataset_u8(nb, d, nq, k, path):
    """
    Creates a random uint8 dataset containing both a dataset and queries against it, and then writes those to disk.

    Parameters
    ----------
    nb: int
        Number of points in the dataset
    d: int
        Dimension of the dataset
    nq: int
        Number of queries
    k: int
        Number of nearest neighbors to return
    path: str
        Path to write the dataset to
    """
    import sklearn.model_selection
    from sklearn.datasets import make_blobs
    from sklearn.neighbors import NearestNeighbors

    # print(f"Preparing datasets with {nb} random points and {nq} queries.")
    os.mkdir(path)
    X, _ = make_blobs(n_samples=nb + nq, n_features=d, centers=nq, random_state=1)

    data, queries = sklearn.model_selection.train_test_split(
        X, test_size=nq, random_state=1
    )
    data = data.astype("uint8")
    queries = queries.astype("uint8")

    with open(os.path.join(path, "data.u8bin"), "wb") as f:
        np.array([nb, d], dtype="uint32").tofile(f)
        data.tofile(f)
    with open(os.path.join(path, "queries"), "wb") as f:
        np.array([nq, d], dtype="uint32").tofile(f)
        queries.tofile(f)

    # print("Computing groundtruth")

    nbrs = NearestNeighbors(n_neighbors=k, metric="euclidean", algorithm="brute").fit(
        data
    )
    D, I = nbrs.kneighbors(queries)
    with open(os.path.join(path, "gt"), "wb") as f:
        np.array([nq, k], dtype="uint32").tofile(f)
        I.astype("uint32").tofile(f)
        D.astype("float32").tofile(f)
    return data


def create_schema():
    schema = tiledb.ArraySchema(
        domain=tiledb.Domain(
            *[
                tiledb.Dim(name="__dim_0", domain=(0, 2), tile=3, dtype="int32"),
                tiledb.Dim(name="__dim_1", domain=(0, 3), tile=3, dtype="int32"),
            ]
        ),
        attrs=[
            tiledb.Attr(name="", dtype="float32", var=False, nullable=False),
        ],
        cell_order="col-major",
        tile_order="col-major",
        capacity=10000,
        sparse=False,
    )
    return schema


def create_array(path: str, data):
    schema = create_schema()
    tiledb.Array.create(path, schema)
    with tiledb.open(path, "w") as A:
        A[:] = data


def accuracy(
    result, gt, external_ids_offset=0, updated_ids=None, only_updated_ids=False
):
    found = 0
    total = 0
    if updated_ids is not None:
        updated_ids_rev = {}
        for updated_id in updated_ids:
            updated_ids_rev[updated_ids[updated_id]] = updated_id
    for i in range(len(result)):
        if external_ids_offset != 0:
            temp_result = []
            for j in range(len(result[i])):
                temp_result.append(int(result[i][j] - external_ids_offset))
        elif updated_ids is not None:
            temp_result = []
            for j in range(len(result[i])):
                if result[i][j] in updated_ids:
                    raise ValueError(
                        f"Found updated id {result[i][j]} in query results."
                    )
                if only_updated_ids:
                    if result[i][j] not in updated_ids_rev:
                        raise ValueError(
                            f"Found not_updated_id {result[i][j]} in query results while expecting only_updated_ids."
                        )
                uid = updated_ids_rev.get(result[i][j])
                if uid is not None:
                    temp_result.append(int(uid))
                else:
                    temp_result.append(result[i][j])
        else:
            temp_result = result[i]
        total += len(temp_result)
        found += len(np.intersect1d(temp_result, gt[i]))
    return found / total


# Generate random names for test array uris
def random_name(name: str) -> str:
    suffix = "".join(random.choices(string.ascii_letters, k=10))
    return f"zzz_unittest_{name}_{suffix}"
