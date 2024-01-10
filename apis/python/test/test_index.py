import numpy as np
from common import *
import pytest

import tiledb.vector_search.index as ind
from tiledb.vector_search import flat_index, ivf_flat_index
from tiledb.vector_search.index import Index
from tiledb.vector_search.ingestion import ingest
from tiledb.vector_search.utils import load_fvecs

def query_and_check(index, queries, k, expected, **kwargs):
    for _ in range(3):
        result_d, result_i = index.query(queries, k=k, **kwargs)
        assert expected.issubset(set(result_i[0]))

def test_flat_index(tmp_path):
    uri = os.path.join(tmp_path, "array")
    index = flat_index.create(uri=uri, dimensions=3, vector_type=np.dtype(np.uint8))
    query_and_check(index, np.array([[2, 2, 2]], dtype=np.float32), 3, {ind.MAX_UINT64})

    update_vectors = np.empty([5], dtype=object)
    update_vectors[0] = np.array([0, 0, 0], dtype=np.dtype(np.uint8))
    update_vectors[1] = np.array([1, 1, 1], dtype=np.dtype(np.uint8))
    update_vectors[2] = np.array([2, 2, 2], dtype=np.dtype(np.uint8))
    update_vectors[3] = np.array([3, 3, 3], dtype=np.dtype(np.uint8))
    update_vectors[4] = np.array([4, 4, 4], dtype=np.dtype(np.uint8))
    index.update_batch(vectors=update_vectors, external_ids=np.array([0, 1, 2, 3, 4]))
    query_and_check(index, np.array([[2, 2, 2]], dtype=np.float32), 3, {1, 2, 3})

    index = index.consolidate_updates()
    query_and_check(index, np.array([[2, 2, 2]], dtype=np.float32), 3, {1, 2, 3})

    index.delete_batch(external_ids=np.array([1, 3]))
    query_and_check(index, np.array([[2, 2, 2]], dtype=np.float32), 3, {0, 2, 4})

    index = index.consolidate_updates()
    query_and_check(index, np.array([[2, 2, 2]], dtype=np.float32), 3, {0, 2, 4})

    update_vectors = np.empty([2], dtype=object)
    update_vectors[0] = np.array([1, 1, 1], dtype=np.dtype(np.uint8))
    update_vectors[1] = np.array([3, 3, 3], dtype=np.dtype(np.uint8))
    index.update_batch(vectors=update_vectors, external_ids=np.array([1, 3]))
    query_and_check(index, np.array([[2, 2, 2]], dtype=np.float32), 3, {1, 2, 3})

    index = index.consolidate_updates()
    query_and_check(index, np.array([[2, 2, 2]], dtype=np.float32), 3, {1, 2, 3})

    index.delete_batch(external_ids=np.array([1, 3]))
    query_and_check(index, np.array([[2, 2, 2]], dtype=np.float32), 3, {0, 2, 4})

    index = index.consolidate_updates()
    query_and_check(index, np.array([[2, 2, 2]], dtype=np.float32), 3, {0, 2, 4})


def test_ivf_flat_index(tmp_path):
    partitions = 10
    uri = os.path.join(tmp_path, "array")
    index = ivf_flat_index.create(
        uri=uri, dimensions=3, vector_type=np.dtype(np.uint8), partitions=partitions
    )
    query_and_check(index, np.array([[2, 2, 2]], dtype=np.float32), 3, {ind.MAX_UINT64}, nprobe=partitions)

    update_vectors = np.empty([5], dtype=object)
    update_vectors[0] = np.array([0, 0, 0], dtype=np.dtype(np.uint8))
    update_vectors[1] = np.array([1, 1, 1], dtype=np.dtype(np.uint8))
    update_vectors[2] = np.array([2, 2, 2], dtype=np.dtype(np.uint8))
    update_vectors[3] = np.array([3, 3, 3], dtype=np.dtype(np.uint8))
    update_vectors[4] = np.array([4, 4, 4], dtype=np.dtype(np.uint8))
    index.update_batch(vectors=update_vectors, external_ids=np.array([0, 1, 2, 3, 4]))
    query_and_check(index, np.array([[2, 2, 2]], dtype=np.float32), 3, {1, 2, 3}, nprobe=partitions)

    index = index.consolidate_updates()
    query_and_check(index, np.array([[2, 2, 2]], dtype=np.float32), 3, {1, 2, 3}, nprobe=partitions)

    index.delete_batch(external_ids=np.array([1, 3]))
    query_and_check(index, np.array([[2, 2, 2]], dtype=np.float32), 3, {0, 2, 4}, nprobe=partitions)

    index = index.consolidate_updates()
    query_and_check(index, np.array([[2, 2, 2]], dtype=np.float32), 3, {0, 2, 4}, nprobe=partitions)

    update_vectors = np.empty([2], dtype=object)
    update_vectors[0] = np.array([1, 1, 1], dtype=np.dtype(np.uint8))
    update_vectors[1] = np.array([3, 3, 3], dtype=np.dtype(np.uint8))
    index.update_batch(vectors=update_vectors, external_ids=np.array([1, 3]))
    query_and_check(index, np.array([[2, 2, 2]], dtype=np.float32), 3, {1, 2, 3}, nprobe=partitions)

    index = index.consolidate_updates()
    query_and_check(index, np.array([[2, 2, 2]], dtype=np.float32), 3, {1, 2, 3}, nprobe=partitions)

    index.delete_batch(external_ids=np.array([1, 3]))
    query_and_check(index, np.array([[2, 2, 2]], dtype=np.float32), 3, {0, 2, 4}, nprobe=partitions)

    index = index.consolidate_updates()
    query_and_check(index, np.array([[2, 2, 2]], dtype=np.float32), 3, {0, 2, 4}, nprobe=partitions)

def test_index_with_incorrect_dimensions(tmp_path):
    indexes = [flat_index, ivf_flat_index]
    for index_type in indexes:
        uri = os.path.join(tmp_path, f"array_{index_type.__name__}")
        index = index_type.create(uri=uri, dimensions=3, vector_type=np.dtype(np.uint8))

        # Wrong number of dimensions will raise a TypeError.
        with pytest.raises(TypeError):
            index.query(np.array(1, dtype=np.float32), k=3)
        with pytest.raises(TypeError):
            index.query(np.array([1, 1, 1], dtype=np.float32), k=3)
        with pytest.raises(TypeError):
            index.query(np.array([[[1, 1, 1]]], dtype=np.float32), k=3)
        with pytest.raises(TypeError):
            index.query(np.array([[[[1, 1, 1]]]], dtype=np.float32), k=3)

        # Okay otherwise.
        index.query(np.array([[1, 1, 1]], dtype=np.float32), k=3)

def test_index_with_incorrect_num_of_query_columns_simple(tmp_path):
    siftsmall_uri = "test/data/siftsmall/siftsmall_base.fvecs"
    queries_uri = "test/data/siftsmall/siftsmall_query.fvecs"
    indexes = ["FLAT", "IVF_FLAT"]
    for index_type in indexes:
        index_uri = os.path.join(tmp_path, f"sift10k_flat_{index_type}")
        index = ingest(
            index_type=index_type,
            index_uri=index_uri,
            source_uri=siftsmall_uri,
            source_type = "FVEC",
        )

        # Wrong number of columns will raise a TypeError.
        query_shape = (1, 1)
        with pytest.raises(TypeError):
            index.query(np.random.rand(*query_shape).astype(np.float32), k=10)

        # Okay otherwise.
        queries = load_fvecs(queries_uri)
        index.query(queries, k=10)

def test_index_with_incorrect_num_of_query_columns_complex(tmp_path):
    # Tests that we raise a TypeError if the number of columns in the query is not the same as the 
    # number of columns in the indexed data.
    size=1000
    indexes = ["FLAT", "IVF_FLAT"]
    num_columns_in_vector = [1, 2, 3, 4, 5, 10]
    for index_type in indexes:
        for num_columns in num_columns_in_vector:
            index_uri = os.path.join(tmp_path, f"array_{index_type}_{num_columns}")
            dataset_dir = os.path.join(tmp_path, f"dataset_{index_type}_{num_columns}")
            create_random_dataset_f32_only_data(nb=size, d=num_columns, centers=1, path=dataset_dir)
            index = ingest(index_type=index_type, index_uri=index_uri, source_uri=os.path.join(dataset_dir, "data.f32bin"))

            # We have created a dataset with num_columns in each vector. Let's try creating queries 
            # with different numbers of columns and confirming incorrect ones will throw.
            for num_columns_for_query in range(1, num_columns + 2):
                query_shape = (1, num_columns_for_query)
                query = np.random.rand(*query_shape).astype(np.float32)
                if query.shape[1] == num_columns:
                    index.query(query, k=1)
                else:
                    with pytest.raises(TypeError):
                        index.query(query, k=1)
