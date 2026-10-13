"""Derive the PCEN front-end fixtures from eloc_model.tflite and eloc_golden_vectors.bin.

The compiled-in model is a log-mel model, so its golden vectors never run PCEN.
This script re-embeds the same model with its feature configuration switched to
PCEN, once per smoother start ("first_frame", "zero"), and writes golden
vectors whose features come from the training worker's NumPy reference
(cloud_training/eloc_features.py). test_generic_mel_frontend checks the C
front-end against them. Only the features and the quantized input are
meaningful; the model's outputs are copied from the log-mel records unchanged.

    py -3.12 test/fixtures/make_pcen_fixtures.py <path to the islandelephants (ie) checkout>

Rerun it whenever eloc_model.tflite or eloc_golden_vectors.bin is replaced.
"""

import copy
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
RECORDS = 4  # enough windows to cover tones, noise and silence; keeps the fixtures small

sys.path.insert(0, str(Path(sys.argv[1]).resolve()))

import numpy as np  # noqa: E402

from cloud_training import device_package  # noqa: E402
from cloud_training.eloc_features import PCEN_DEFAULTS, PCEN_STARTS, FeatureSpec  # noqa: E402

model = (HERE / "eloc_model.tflite").read_bytes()
config = device_package.read_embedded_config(model)
golden = device_package.read_golden_vectors(HERE / "eloc_golden_vectors.bin")["entries"][:RECORDS]
classifier = device_package.Int8Classifier(model)

for start in PCEN_STARTS:
    variant = copy.deepcopy(config)
    features = variant["features"]
    features["transform"] = "pcen"
    features["pcen"] = {**PCEN_DEFAULTS, "start": start}
    # PCEN output is about 0..15; keep the quantized input inside the int8 range
    features["normalization"] = {"mean": 0.3, "divisor": 0.35}
    spec = FeatureSpec.from_config(variant)
    entries = []
    for record in golden:
        values = spec.compute(np.asarray(record["samples"], dtype=np.int16))
        entries.append({**record, "features": values, "input": classifier.quantize(values)})
    stem = f"eloc_model_pcen_{start}"
    (HERE / f"{stem}.tflite").write_bytes(device_package.embed_config(model, variant))
    device_package.write_golden_vectors(HERE / f"eloc_golden_vectors_pcen_{start}.bin", spec, entries, len(golden[0]["output"]))
    print(f"{stem}: {len(entries)} records, features {spec.n_frames} x {spec.n_mels}")
