# Task F: handwritten appendix

`Task_F_handwritten_backprop.pdf` is a two-page scan of the candidate's handwritten derivation. It covers:

* forward pass;
* local derivatives;
* chain-rule gradients for w, b, v and c;
* one simultaneous gradient-descent update (learning rate 0.1);
* a loss check with the new parameters;
* an explanation of the gradient direction.

Signed and dated 01/11/2026.

The handwritten values agree with the C++ implementation (`src/core/src/backprop.cpp`) and with
`results/backprop_gradcheck.csv` to the 8 decimal places written:

| Quantity | Handwritten | C++ |
|---|---|---|
| ∂J/∂w = ∂J/∂b | −1.20946448 | −1.20946447524 |
| ∂J/∂v | −0.71068205 | −0.710682047486 |
| ∂J/∂c | −1.53788284 | −1.53788284274 |
| updated w, b, v, c | 0.62094645, 0.12094645, 1.07106820, 0.15378828 | same |
| loss before → after | 1.18254182 → 0.68577377 | same |
