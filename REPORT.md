# Lab 0 Report — Profiling & Accelerator Proposal

**Group:** _______   **Members:** _______, _______, _______

---

## 1. Vendoring

What did `make vendor` do? (2–3 sentences: what it fetched, where it put it,
snapshot vs submodule.)

> _your answer_

## 2. Profiling results

Copy the per-kernel table each application prints. Use the same X-HEEP build
for all three.

| rxchain kernel | cycles | share |
|----------------|--------|-------|
| total          |        | 100%  |
| nco            |        |       |
| mixer          |        |       |
| fir            |        |       |
| decimate       |        |       |
| magnitude      |        |       |
| detect         |        |       |

| tinydnn kernel | cycles | share |
|----------------|--------|-------|
| total          |        | 100%  |
| conv           |        |       |
| pool           |        |       |
| linear         |        |       |
| requant        |        |       |

| tinyformer kernel | cycles | share |
|-------------------|--------|-------|
| total             |        | 100%  |
| linear            |        |       |
| qk                |        |       |
| softmax           |        |       |
| av                |        |       |
| norm              |        |       |
| requant           |        |       |

| pqcrypto kernel | cycles | share |
|-----------------|--------|-------|
| total           |        | 100%  |
| ntt             |        |       |
| intt            |        |       |
| pointwise       |        |       |
| polyadd         |        |       |
| sample          |        |       |
| compress        |        |       |
| message         |        |       |

Dominant function per app from the RV_PROFILE flamegraph:

| App | Dominant function | Agrees with the table above? |
|-----|-------------------|------------------------------|
| rxchain    |  |  |
| tinydnn    |  |  |
| tinyformer |  |  |
| pqcrypto   |  |  |

What did each profiling method tell you that the others did not?

- Cycle counters: _______
- RV_PROFILE flamegraph: _______
- Waveforms (debug observation): _______

## 3. Amdahl's law

Dominant kernel per app, and its share:

- rxchain: _______ (___%)
- tinydnn: _______ (___%)
- tinyformer: _______ (___%)
- pqcrypto: _______ (___%)

Maximum whole-application speedup if that kernel cost zero cycles:

- rxchain: _______×
- tinydnn: _______×
- tinyformer: _______×
- pqcrypto: _______×

Which kernels were cheaper than they look, and which were more expensive?

> _your answer_

Does any one kernel carry weight in more than one app?

> _your answer_

## 4. Chosen application & accelerator proposal

**Chosen app:** _______   **Kernel to accelerate:** _______

**Interface:** ☐ CV-XIF   ☐ OBI + register file
**Justification:** _______

**Expected speedup and why:** _______

**Datapath sketch:** (diagram or description)

> _your sketch_

**Which of the three apps this accelerator speeds up, and by how much in each:**
_______

**Risks / open questions:** _______

## 5. Optional: FPGA run

Did you run a complete application on the board? If so: board, `NINFER` /
`NBLOCK`, cycles or wall-clock per run, and how it compares with the Verilator
number.

> _optional_
