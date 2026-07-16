# Lab 0 Report — Profiling & Accelerator Proposal

**Group:** _______   **Members:** _______, _______, _______

---

## 1. Vendoring

What did `make vendor` do? (2–3 sentences: what it fetched, where it put it,
snapshot vs submodule.)

> _your answer_

## 2. Profiling results

Fill in the measured numbers. Use the same X-HEEP build for all rows.

| App    | Kernel cycles | Kernel instr | Dominant function (RV_PROFILE) |
|--------|---------------|--------------|--------------------------------|
| matmul |               |              |                                |
| crc32  |               |              |                                |

What did each profiling method tell you that the others did not?

- Cycle printf: _______
- RV_PROFILE flamegraph: _______
- Waveforms (debug observation): _______

## 3. Chosen application & accelerator proposal

**Chosen app:** _______   **Kernel to accelerate:** _______

**Interface:** ☐ CV-XIF   ☐ OBI + register file
**Justification:** _______

**Expected speedup and why:** _______

**Datapath sketch:** (diagram or description)

> _your sketch_

**Risks / open questions:** _______
