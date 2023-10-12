import numpy as np

from common import *

from tiledb.vector_search.utils import load_fvecs
from tiledb.vector_search.ingestion import ingest
from tiledb.vector_search.index import Index
from tiledb.vector_search.flat_index import FlatIndex
from tiledb.vector_search.ivf_flat_index import IVFFlatIndex
from tiledb.cloud.dag import Mode

MINIMUM_ACCURACY = 0.85
MAX_UINT64 = np.iinfo(np.dtype("uint64")).max


def test_flat_ingestion_u8(tmp_path):
    dataset_dir = os.path.join(tmp_path, "dataset")
    index_uri = os.path.join(tmp_path, "array")
    create_random_dataset_u8(nb=10000, d=100, nq=100, k=10, path=dataset_dir)
    dtype = np.uint8
    k = 10

    query_vectors = get_queries(dataset_dir, dtype=dtype)
    gt_i, gt_d = get_groundtruth(dataset_dir, k)

    index = ingest(
        index_type="FLAT",
        index_uri=index_uri,
        source_uri=os.path.join(dataset_dir, "data.u8bin"),
    )
    _, result = index.query(query_vectors, k=k)
    assert accuracy(result, gt_i) > MINIMUM_ACCURACY


def test_flat_ingestion_f32(tmp_path):
    dataset_dir = os.path.join(tmp_path, "dataset")
    index_uri = os.path.join(tmp_path, "array")
    create_random_dataset_f32(nb=10000, d=100, nq=100, k=10, path=dataset_dir)
    dtype = np.float32
    k = 10

    query_vectors = get_queries(dataset_dir, dtype=dtype)
    gt_i, gt_d = get_groundtruth(dataset_dir, k)

    index = ingest(
        index_type="FLAT",
        index_uri=index_uri,
        source_uri=os.path.join(dataset_dir, "data.f32bin"),
    )
    _, result = index.query(query_vectors, k=k)
    assert accuracy(result, gt_i) > MINIMUM_ACCURACY

    index_ram = FlatIndex(uri=index_uri)
    _, result = index_ram.query(query_vectors, k=k)
    assert accuracy(result, gt_i) > MINIMUM_ACCURACY


def test_flat_ingestion_external_id_u8(tmp_path):
    dataset_dir = os.path.join(tmp_path, "dataset")
    index_uri = os.path.join(tmp_path, "array")
    size = 10000
    dtype = np.uint8
    create_random_dataset_u8(nb=size, d=100, nq=100, k=10, path=dataset_dir)
    k = 10
    external_ids_offset = 100

    query_vectors = get_queries(dataset_dir, dtype=dtype)
    gt_i, gt_d = get_groundtruth(dataset_dir, k)
    external_ids = np.array([range(external_ids_offset, size+external_ids_offset)], np.uint64)

    index = ingest(
        index_type="FLAT",
        index_uri=index_uri,
        source_uri=os.path.join(dataset_dir, "data.u8bin"),
        external_ids=external_ids
    )
    _, result = index.query(query_vectors, k=k)
    assert accuracy(result, gt_i, external_ids_offset=external_ids_offset) > MINIMUM_ACCURACY


def test_ivf_flat_ingestion_u8(tmp_path):
    dataset_dir = os.path.join(tmp_path, "dataset")
    index_uri = os.path.join(tmp_path, "array")
    k = 10
    size = 100000
    partitions = 100
    dimensions = 128
    nqueries = 100
    nprobe = 20
    create_random_dataset_u8(nb=size, d=dimensions, nq=nqueries, k=k, path=dataset_dir)
    dtype = np.uint8

    query_vectors = get_queries(dataset_dir, dtype=dtype)
    gt_i, gt_d = get_groundtruth(dataset_dir, k)
    index = ingest(
        index_type="IVF_FLAT",
        index_uri=index_uri,
        source_uri=os.path.join(dataset_dir, "data.u8bin"),
        partitions=partitions,
        input_vectors_per_work_item=int(size / 10),
    )
    _, result = index.query(query_vectors, k=k, nprobe=nprobe)
    assert accuracy(result, gt_i) > MINIMUM_ACCURACY

    index_ram = IVFFlatIndex(uri=index_uri, memory_budget=int(size / 10))
    _, result = index_ram.query(query_vectors, k=k, nprobe=nprobe)
    assert accuracy(result, gt_i) > MINIMUM_ACCURACY

    _, result = index_ram.query(
        query_vectors,
        k=k,
        nprobe=nprobe,
        use_nuv_implementation=True,
    )
    assert accuracy(result, gt_i) > MINIMUM_ACCURACY

    _, result = index_ram.query(
        query_vectors,
        k=k,
        nprobe=nprobe,
        mode=Mode.LOCAL,
    )
    assert accuracy(result, gt_i) > MINIMUM_ACCURACY


def test_ivf_flat_ingestion_f32(tmp_path):
    dataset_dir = os.path.join(tmp_path, "dataset")
    index_uri = os.path.join(tmp_path, "array")
    k = 10
    size = 100000
    dimensions = 128
    partitions = 100
    nqueries = 100
    nprobe = 20

    create_random_dataset_f32(nb=size, d=dimensions, nq=nqueries, k=k, path=dataset_dir)
    dtype = np.float32

    query_vectors = get_queries(dataset_dir, dtype=dtype)
    gt_i, gt_d = get_groundtruth(dataset_dir, k)

    index = ingest(
        index_type="IVF_FLAT",
        index_uri=index_uri,
        source_uri=os.path.join(dataset_dir, "data.f32bin"),
        partitions=partitions,
        input_vectors_per_work_item=int(size / 10),
    )

    _, result = index.query(query_vectors, k=k, nprobe=nprobe)
    assert accuracy(result, gt_i) > MINIMUM_ACCURACY

    index_ram = IVFFlatIndex(uri=index_uri)
    _, result = index_ram.query(query_vectors, k=k, nprobe=nprobe)
    assert accuracy(result, gt_i) > MINIMUM_ACCURACY

    index_ram = IVFFlatIndex(uri=index_uri, memory_budget=int(size / 10))
    _, result = index_ram.query(query_vectors, k=k, nprobe=nprobe)
    assert accuracy(result, gt_i) > MINIMUM_ACCURACY

    _, result = index_ram.query(
        query_vectors,
        k=k,
        nprobe=nprobe,
        use_nuv_implementation=True,
    )
    assert accuracy(result, gt_i) > MINIMUM_ACCURACY

    _, result = index_ram.query(query_vectors, k=k, nprobe=nprobe, mode=Mode.LOCAL)
    assert accuracy(result, gt_i) > MINIMUM_ACCURACY


def test_ivf_flat_ingestion_fvec(tmp_path):
    source_uri = "test/data/siftsmall/siftsmall_base.fvecs"
    queries_uri = "test/data/siftsmall/siftsmall_query.fvecs"
    gt_uri = "test/data/siftsmall/siftsmall_groundtruth.ivecs"
    index_uri = os.path.join(tmp_path, "array")
    k = 100
    partitions = 100
    nqueries = 100
    nprobe = 20

    query_vectors = load_fvecs(queries_uri)
    gt_i, gt_d = get_groundtruth_ivec(gt_uri, k=k, nqueries=nqueries)

    index = ingest(
        index_type="IVF_FLAT",
        index_uri=index_uri,
        source_uri=source_uri,
        partitions=partitions,
    )
    _, result = index.query(query_vectors, k=k, nprobe=nprobe)
    assert accuracy(result, gt_i) > MINIMUM_ACCURACY

    # Test single query vector handling
    _, result1 = index.query(query_vectors[10], k=k, nprobe=nprobe)
    assert accuracy(result1, np.array([gt_i[10]])) > MINIMUM_ACCURACY

    index_ram = IVFFlatIndex(uri=index_uri)
    _, result = index_ram.query(query_vectors, k=k, nprobe=nprobe)
    assert accuracy(result, gt_i) > MINIMUM_ACCURACY

    _, result = index_ram.query(
        query_vectors,
        k=k,
        nprobe=nprobe,
        use_nuv_implementation=True,
    )
    assert accuracy(result, gt_i) > MINIMUM_ACCURACY

    # NB: local mode currently does not return distances
    _, result = index_ram.query(query_vectors, k=k, nprobe=nprobe, mode=Mode.LOCAL)
    assert accuracy(result, gt_i) > MINIMUM_ACCURACY


def test_ivf_flat_ingestion_numpy(tmp_path):
    source_uri = "test/data/siftsmall/siftsmall_base.fvecs"
    queries_uri = "test/data/siftsmall/siftsmall_query.fvecs"
    gt_uri = "test/data/siftsmall/siftsmall_groundtruth.ivecs"
    index_uri = os.path.join(tmp_path, "array")
    k = 100
    partitions = 100
    nqueries = 100
    nprobe = 20

    input_vectors = load_fvecs(source_uri)

    query_vectors = load_fvecs(queries_uri)
    gt_i, gt_d = get_groundtruth_ivec(gt_uri, k=k, nqueries=nqueries)

    index = ingest(
        index_type="IVF_FLAT",
        index_uri=index_uri,
        input_vectors=input_vectors,
        partitions=partitions,
    )
    _, result = index.query(query_vectors, k=k, nprobe=nprobe)
    assert accuracy(result, gt_i) > MINIMUM_ACCURACY

    # Test single query vector handling
    _, result1 = index.query(query_vectors[10], k=k, nprobe=nprobe)
    assert accuracy(result1, np.array([gt_i[10]])) > MINIMUM_ACCURACY

    index_ram = IVFFlatIndex(uri=index_uri)
    _, result = index_ram.query(query_vectors, k=k, nprobe=nprobe)
    assert accuracy(result, gt_i) > MINIMUM_ACCURACY

    _, result = index_ram.query(
        query_vectors,
        k=k,
        nprobe=nprobe,
        use_nuv_implementation=True,
    )
    assert accuracy(result, gt_i) > MINIMUM_ACCURACY

    _, result = index_ram.query(query_vectors, k=k, nprobe=nprobe, mode=Mode.LOCAL)
    assert accuracy(result, gt_i) > MINIMUM_ACCURACY


def test_ivf_flat_ingestion_external_ids_numpy(tmp_path):
    source_uri = "test/data/siftsmall/siftsmall_base.fvecs"
    queries_uri = "test/data/siftsmall/siftsmall_query.fvecs"
    gt_uri = "test/data/siftsmall/siftsmall_groundtruth.ivecs"
    index_uri = os.path.join(tmp_path, "array")
    k = 100
    partitions = 100
    nqueries = 100
    nprobe = 20
    size = 10000
    external_ids_offset = 100

    input_vectors = load_fvecs(source_uri)

    query_vectors = load_fvecs(queries_uri)
    gt_i, gt_d = get_groundtruth_ivec(gt_uri, k=k, nqueries=nqueries)
    external_ids = np.array([range(external_ids_offset, size+external_ids_offset)], np.uint64)
    index = ingest(
        index_type="IVF_FLAT",
        index_uri=index_uri,
        input_vectors=input_vectors,
        partitions=partitions,
        external_ids=external_ids
    )
    _, result = index.query(query_vectors, k=k, nprobe=nprobe)
    assert accuracy(result, gt_i, external_ids_offset) > MINIMUM_ACCURACY


def test_ivf_flat_ingestion_with_updates(tmp_path):
    dataset_dir = os.path.join(tmp_path, "dataset")
    index_uri = os.path.join(tmp_path, "array")
    k = 10
    size = 1000
    partitions = 10
    dimensions = 128
    nqueries = 100
    nprobe = 10
    data = create_random_dataset_u8(nb=size, d=dimensions, nq=nqueries, k=k, path=dataset_dir)
    dtype = np.uint8

    query_vectors = get_queries(dataset_dir, dtype=dtype)
    gt_i, gt_d = get_groundtruth(dataset_dir, k)
    index = ingest(
        index_type="IVF_FLAT",
        index_uri=index_uri,
        source_uri=os.path.join(dataset_dir, "data.u8bin"),
        partitions=partitions,
    )
    _, result = index.query(query_vectors, k=k, nprobe=nprobe)
    assert accuracy(result, gt_i) == 1.0

    update_ids_offset = MAX_UINT64-size
    updated_ids = {}
    for i in range(100):
        index.delete(external_id=i)
        index.update(vector=data[i].astype(dtype), external_id=i + update_ids_offset)
        updated_ids[i] = i + update_ids_offset

    _, result = index.query(query_vectors, k=k, nprobe=nprobe)
    assert accuracy(result, gt_i, updated_ids=updated_ids) == 1.0

    index = index.consolidate_updates()
    _, result = index.query(query_vectors, k=k, nprobe=nprobe)
    assert accuracy(result, gt_i, updated_ids=updated_ids) == 1.0

def test_ivf_flat_ingestion_with_batch_updates(tmp_path):
    dataset_dir = os.path.join(tmp_path, "dataset")
    index_uri = os.path.join(tmp_path, "array")
    k = 10
    size = 100000
    partitions = 100
    dimensions = 128
    nqueries = 100
    nprobe = 100
    data = create_random_dataset_u8(nb=size, d=dimensions, nq=nqueries, k=k, path=dataset_dir)
    dtype = np.uint8

    query_vectors = get_queries(dataset_dir, dtype=dtype)
    gt_i, gt_d = get_groundtruth(dataset_dir, k)
    index = ingest(
        index_type="IVF_FLAT",
        index_uri=index_uri,
        source_uri=os.path.join(dataset_dir, "data.u8bin"),
        partitions=partitions,
        input_vectors_per_work_item=int(size / 10),
    )
    _, result = index.query(query_vectors, k=k, nprobe=nprobe)
    assert accuracy(result, gt_i) > 0.99

    update_ids = {}
    updated_ids = {}
    update_ids_offset = MAX_UINT64 - size
    for i in range(0, 100000, 2):
        updated_ids[i] = i + update_ids_offset
        update_ids[i + update_ids_offset] = i
    external_ids = np.zeros((len(updated_ids) * 2), dtype=np.uint64)
    updates = np.empty((len(updated_ids) * 2), dtype='O')
    id = 0
    for prev_id, new_id in updated_ids.items():
        external_ids[id] = prev_id
        updates[id] = np.array([], dtype=dtype)
        id += 1
        external_ids[id] = new_id
        updates[id] = data[prev_id].astype(dtype)
        id += 1

    index.update_batch(vectors=updates, external_ids=external_ids)
    _, result = index.query(query_vectors, k=k, nprobe=nprobe)
    assert accuracy(result, gt_i, updated_ids=updated_ids) > 0.99

    index = index.consolidate_updates()
    _, result = index.query(query_vectors, k=k, nprobe=nprobe)
    assert accuracy(result, gt_i, updated_ids=updated_ids) > 0.99


def test_ivf_flat_ingestion_with_updates_and_timetravel(tmp_path):
    dataset_dir = os.path.join(tmp_path, "dataset")
    index_uri = os.path.join(tmp_path, "array")
    k = 10
    size = 1000
    partitions = 10
    dimensions = 128
    nqueries = 100
    nprobe = 10
    data = create_random_dataset_u8(nb=size, d=dimensions, nq=nqueries, k=k, path=dataset_dir)
    dtype = np.uint8

    query_vectors = get_queries(dataset_dir, dtype=dtype)
    gt_i, gt_d = get_groundtruth(dataset_dir, k)
    index = ingest(
        index_type="IVF_FLAT",
        index_uri=index_uri,
        source_uri=os.path.join(dataset_dir, "data.u8bin"),
        partitions=partitions,
        index_timestamp=1,
    )
    _, result = index.query(query_vectors, k=k, nprobe=nprobe)
    assert accuracy(result, gt_i) == 1.0

    update_ids_offset = MAX_UINT64-size
    updated_ids = {}
    for i in range(2, 102):
        index.delete(external_id=i, timestamp=i)
        index.update(vector=data[i].astype(dtype), external_id=i + update_ids_offset, timestamp=i)
        updated_ids[i] = i + update_ids_offset

    index = IVFFlatIndex(uri=index_uri, timestamp=101)
    _, result = index.query(query_vectors, k=k, nprobe=nprobe)
    assert accuracy(result, gt_i, updated_ids=updated_ids) == 1.0
    index = IVFFlatIndex(uri=index_uri, timestamp=(0, 101))
    _, result = index.query(query_vectors, k=k, nprobe=nprobe)
    assert accuracy(result, gt_i, updated_ids=updated_ids) == 1.0
    index = IVFFlatIndex(uri=index_uri, timestamp=(2, 101))
    _, result = index.query(query_vectors, k=k, nprobe=nprobe)
    assert 0.05 <= accuracy(result, gt_i, updated_ids=updated_ids, only_updated_ids=True) <= 0.15

    # Timetravel with partial read from updates table
    updated_ids_part = {}
    for i in range(2, 52):
        updated_ids_part[i] = i + update_ids_offset
    index = IVFFlatIndex(uri=index_uri, timestamp=51)
    _, result = index.query(query_vectors, k=k, nprobe=nprobe)
    assert accuracy(result, gt_i, updated_ids=updated_ids_part) == 1.0
    index = IVFFlatIndex(uri=index_uri, timestamp=(0, 51))
    _, result = index.query(query_vectors, k=k, nprobe=nprobe)
    assert accuracy(result, gt_i, updated_ids=updated_ids_part) == 1.0
    index = IVFFlatIndex(uri=index_uri, timestamp=(2, 51))
    _, result = index.query(query_vectors, k=k, nprobe=nprobe)
    assert 0.02 <= accuracy(result, gt_i, updated_ids=updated_ids, only_updated_ids=True) <= 0.07

    # Timetravel at previous ingestion timestamp
    index = IVFFlatIndex(uri=index_uri, timestamp=1)
    _, result = index.query(query_vectors, k=k, nprobe=nprobe)
    assert accuracy(result, gt_i) == 1.0

    # Consolidate updates
    index = index.consolidate_updates()
    index = IVFFlatIndex(uri=index_uri, timestamp=101)
    _, result = index.query(query_vectors, k=k, nprobe=nprobe)
    assert accuracy(result, gt_i, updated_ids=updated_ids) == 1.0
    index = IVFFlatIndex(uri=index_uri, timestamp=(0, 101))
    _, result = index.query(query_vectors, k=k, nprobe=nprobe)
    assert accuracy(result, gt_i, updated_ids=updated_ids) == 1.0
    index = IVFFlatIndex(uri=index_uri, timestamp=(2, 101))
    _, result = index.query(query_vectors, k=k, nprobe=nprobe)
    assert 0.05 <= accuracy(result, gt_i, updated_ids=updated_ids, only_updated_ids=True) <= 0.15

    # Timetravel with partial read from updates table
    updated_ids_part = {}
    for i in range(2, 52):
        updated_ids_part[i] = i + update_ids_offset
    index = IVFFlatIndex(uri=index_uri, timestamp=51)
    _, result = index.query(query_vectors, k=k, nprobe=nprobe)
    assert accuracy(result, gt_i, updated_ids=updated_ids_part) == 1.0
    index = IVFFlatIndex(uri=index_uri, timestamp=(0, 51))
    _, result = index.query(query_vectors, k=k, nprobe=nprobe)
    assert accuracy(result, gt_i, updated_ids=updated_ids_part) == 1.0
    index = IVFFlatIndex(uri=index_uri, timestamp=(2, 51))
    _, result = index.query(query_vectors, k=k, nprobe=nprobe)
    assert 0.02 <= accuracy(result, gt_i, updated_ids=updated_ids, only_updated_ids=True) <= 0.07

    # Timetravel at previous ingestion timestamp
    index = IVFFlatIndex(uri=index_uri, timestamp=1)
    _, result = index.query(query_vectors, k=k, nprobe=nprobe)
    assert accuracy(result, gt_i) == 1.0
    index = IVFFlatIndex(uri=index_uri, timestamp=(0, 1))
    _, result = index.query(query_vectors, k=k, nprobe=nprobe)
    assert accuracy(result, gt_i) == 1.0


def test_ivf_flat_ingestion_with_additions_and_timetravel(tmp_path):
    dataset_dir = os.path.join(tmp_path, "dataset")
    index_uri = os.path.join(tmp_path, "array")
    k = 100
    size = 100
    partitions = 1
    dimensions = 128
    nqueries = 1
    nprobe = 1
    data = create_random_dataset_u8(nb=size, d=dimensions, nq=nqueries, k=k, path=dataset_dir)
    dtype = np.uint8

    query_vectors = get_queries(dataset_dir, dtype=dtype)
    gt_i, gt_d = get_groundtruth(dataset_dir, k)
    index = ingest(
        index_type="IVF_FLAT",
        index_uri=index_uri,
        source_uri=os.path.join(dataset_dir, "data.u8bin"),
        partitions=partitions,
        index_timestamp=1,
    )
    _, result = index.query(query_vectors, k=k)
    assert accuracy(result, gt_i) == 1.0

    update_ids_offset = MAX_UINT64-size
    updated_ids = {}
    for i in range(100):
        index.update(vector=data[i].astype(dtype), external_id=i + update_ids_offset, timestamp=i+2)
        updated_ids[i] = i + update_ids_offset

    index = IVFFlatIndex(uri=index_uri)
    _, result = index.query(query_vectors, k=k)
    assert 0.45 < accuracy(result, gt_i) < 0.55

    index = index.consolidate_updates()
    _, result = index.query(query_vectors, k=k)
    assert 0.45 < accuracy(result, gt_i) < 0.55
