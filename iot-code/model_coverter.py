import joblib
import os
import json
import math

BASE_DIR = os.path.dirname(os.path.abspath(__file__))

def c_factor(n):
    if n <= 1: return 0.0
    if n == 2: return 1.0
    return 2.0 * (math.log(n - 1) + 0.5772156649) - 2.0 * (n - 1) / n

def extract_trees(model):
    trees = []
    for estimator in model.estimators_:
        t = estimator.tree_
        trees.append({
            "children_left":  t.children_left.tolist(),
            "children_right": t.children_right.tolist(),
            "feature":        t.feature.tolist(),
            "threshold":      t.threshold.tolist(),
            "n_node_samples": t.n_node_samples.tolist(),
        })
    return trees

def generate_header(speed, trees, scaler_mean, scaler_scale, score_min, score_max, max_samples):
    n_trees   = len(trees)
    max_nodes = max(len(t["children_left"]) for t in trees)
    S = speed

    def pad(arr, fill, length):
        return arr + [fill] * (length - len(arr))

    lines = []
    lines.append(f"// Auto-generated IsolationForest - Speed {S}")
    lines.append(f"// Features: voltageV, currentmA, vibrationRms, powerW")
    lines.append(f"#ifndef IFOREST_SPEED{S}_H")
    lines.append(f"#define IFOREST_SPEED{S}_H")
    lines.append( "#include <math.h>")
    lines.append( "")
    lines.append(f"#define IF{S}_N_TREES    {n_trees}")
    lines.append(f"#define IF{S}_N_FEATURES 4")
    lines.append(f"#define IF{S}_MAX_NODES  {max_nodes}")
    lines.append(f"#define IF{S}_MAX_SAMPLES {max_samples}")
    lines.append( "")

    lines.append(f"static const float IF{S}_MEAN[4]  = {{ {', '.join(f'{v:.6f}f' for v in scaler_mean)} }};")
    lines.append(f"static const float IF{S}_SCALE[4] = {{ {', '.join(f'{v:.6f}f' for v in scaler_scale)} }};")
    lines.append(f"static const float IF{S}_SCORE_MIN = {score_min:.6f}f;")
    lines.append(f"static const float IF{S}_SCORE_MAX = {score_max:.6f}f;")
    lines.append( "")

    for i, tree in enumerate(trees):
        n  = len(tree["children_left"])
        cl = pad(tree["children_left"],  -1,   max_nodes)
        cr = pad(tree["children_right"], -1,   max_nodes)
        ft = pad(tree["feature"],        -1,   max_nodes)
        th = pad(tree["threshold"],       0.0, max_nodes)
        ns = pad(tree["n_node_samples"],  0,   max_nodes)
        lines.append(f"static const int   IF{S}_CL{i}[{max_nodes}] = {{ {', '.join(str(x) for x in cl)} }};")
        lines.append(f"static const int   IF{S}_CR{i}[{max_nodes}] = {{ {', '.join(str(x) for x in cr)} }};")
        lines.append(f"static const int   IF{S}_FT{i}[{max_nodes}] = {{ {', '.join(str(x) for x in ft)} }};")
        lines.append(f"static const float IF{S}_TH{i}[{max_nodes}] = {{ {', '.join(f'{x:.6f}f' for x in th)} }};")
        lines.append(f"static const int   IF{S}_NS{i}[{max_nodes}] = {{ {', '.join(str(x) for x in ns)} }};")
        lines.append( "")

    lines.append(f"static const int*   IF{S}_CL[IF{S}_N_TREES] = {{ {', '.join(f'IF{S}_CL{i}' for i in range(n_trees))} }};")
    lines.append(f"static const int*   IF{S}_CR[IF{S}_N_TREES] = {{ {', '.join(f'IF{S}_CR{i}' for i in range(n_trees))} }};")
    lines.append(f"static const int*   IF{S}_FT[IF{S}_N_TREES] = {{ {', '.join(f'IF{S}_FT{i}' for i in range(n_trees))} }};")
    lines.append(f"static const float* IF{S}_TH[IF{S}_N_TREES] = {{ {', '.join(f'IF{S}_TH{i}' for i in range(n_trees))} }};")
    lines.append(f"static const int*   IF{S}_NS[IF{S}_N_TREES] = {{ {', '.join(f'IF{S}_NS{i}' for i in range(n_trees))} }};")
    lines.append( "")

    lines.append(f"static inline float if{S}_c(int n) {{")
    lines.append( "    if (n <= 1) return 0.0f;")
    lines.append( "    if (n == 2) return 1.0f;")
    lines.append( "    return 2.0f*(logf((float)(n-1))+0.5772156649f) - 2.0f*(n-1)/(float)n;")
    lines.append( "}")
    lines.append( "")

    lines.append(f"static inline float if{S}_path(int ti, const float* x) {{")
    lines.append(f"    const int*   cl = IF{S}_CL[ti];")
    lines.append(f"    const int*   cr = IF{S}_CR[ti];")
    lines.append(f"    const int*   ft = IF{S}_FT[ti];")
    lines.append(f"    const float* th = IF{S}_TH[ti];")
    lines.append(f"    const int*   ns = IF{S}_NS[ti];")
    lines.append( "    int node = 0; float depth = 0.0f;")
    lines.append( "    while (cl[node] != -1) {")
    lines.append( "        node = (x[ft[node]] <= th[node]) ? cl[node] : cr[node];")
    lines.append( "        depth += 1.0f;")
    lines.append( "    }")
    lines.append(f"    return depth + if{S}_c(ns[node]);")
    lines.append( "}")
    lines.append( "")

    lines.append(f"// Returns health score 0-100 (100=healthy, 0=anomaly)")
    lines.append(f"static inline float if{S}_health(float voltageV, float currentmA, float vibrationRms, float powerW) {{")
    lines.append( "    float raw[4] = {voltageV, currentmA, vibrationRms, powerW};")
    lines.append( "    float x[4];")
    lines.append(f"    for (int i = 0; i < 4; i++) x[i] = (raw[i] - IF{S}_MEAN[i]) / IF{S}_SCALE[i];")
    lines.append( "    float avg = 0.0f;")
    lines.append(f"    for (int t = 0; t < IF{S}_N_TREES; t++) avg += if{S}_path(t, x);")
    lines.append(f"    avg /= IF{S}_N_TREES;")
    lines.append(f"    float score = -powf(2.0f, -avg / if{S}_c(IF{S}_MAX_SAMPLES));")
    lines.append(f"    float lo = IF{S}_SCORE_MIN, hi = IF{S}_SCORE_MAX;")
    lines.append( "    float clamped = score < lo ? lo : (score > hi ? hi : score);")
    lines.append( "    return (clamped - lo) / (hi - lo) * 100.0f;")
    lines.append( "}")
    lines.append( "")
    lines.append(f"#endif // IFOREST_SPEED{S}_H")
    return "\n".join(lines)


# Load scaler_params.json for score_min / score_max
with open(os.path.join(BASE_DIR, "scaler_params.json")) as f:
    scaler_params = json.load(f)

for speed in [1, 2, 3]:
    print(f"Processing speed {speed}...")

    model  = joblib.load(os.path.join(BASE_DIR, f"isolation_forest_speed{speed}.pkl"))
    scaler = joblib.load(os.path.join(BASE_DIR, f"scaler_speed{speed}.pkl"))

    mean        = scaler.mean_.tolist()
    scale       = scaler.scale_.tolist()
    max_samples = int(model.max_samples_)

    sp        = scaler_params[f"speed{speed}"]
    score_min = float(sp["score_min"])
    score_max = float(sp["score_max"])

    trees  = extract_trees(model)
    header = generate_header(speed, trees, mean, scale, score_min, score_max, max_samples)

    out = os.path.join(BASE_DIR, f"iforest_speed{speed}.h")
    with open(out, "w") as f:
        f.write(header)

    size_kb = os.path.getsize(out) // 1024
    print(f"  -> iforest_speed{speed}.h  ({len(trees)} trees, {size_kb} KB)")

print("All done! Files saved next to convert.py")
