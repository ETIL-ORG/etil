# MLP Library — Multilayer Perceptron in ETIL

The MLP library (`data/library/mlp.til`) provides words for constructing, training, and evaluating feedforward neural networks using ETIL's matrix primitives.

## Quick Start

```forth
include data/library/mlp.til

42 random-seed

# Build a 2-layer network: 2 inputs -> 4 hidden (ReLU) -> 1 output (sigmoid)
2 4 ' mat-relu ' mat-relu' make-layer
4 1 ' mat-sigmoid ' mat-sigmoid' make-layer
2 make-network
variable net
net !

# Training data: XOR
# X = 2x4 matrix (each column is a sample)
array-new
  array-new 0.0 array-push 0.0 array-push 1.0 array-push 1.0 array-push array-push
  array-new 0.0 array-push 1.0 array-push 0.0 array-push 1.0 array-push array-push
array->mat
variable X
X !

# Y = 1x4 matrix (XOR outputs)
array-new
  array-new 0.0 array-push 1.0 array-push 1.0 array-push 0.0 array-push array-push
array->mat
variable Y
Y !

# Train for 5000 epochs with learning rate 0.5
X @ Y @ net @ 0.5 5000 train
net !
```

## Data Representation

| Concept | Representation |
|---------|---------------|
| Layer | `HeapMap` with keys `"W"` (weight matrix), `"b"` (bias vector), `"act"` (activation xt), `"act'"` (activation derivative xt) |
| Network | `HeapArray` of layer maps |
| Training cache | `HeapArray` of `HeapMap`s with keys `"Z"` (pre-activation) and `"A"` (post-activation) |

Weight matrices are `(fan_out x fan_in)`, bias vectors are `(fan_out x 1)`, and input data is `(features x batch_size)` — each column is one sample.

## Word Reference

### Construction

| Word | Stack Effect | Description |
|------|-------------|-------------|
| `make-layer` | `( fan_in fan_out act-xt act'-xt -- layer )` | Create a layer with Xavier-initialized weights and zero bias |
| `make-network` | `( layer1 ... layerN n -- network )` | Collect N layers into a network array |

### Forward Pass

| Word | Stack Effect | Description |
|------|-------------|-------------|
| `layer-forward` | `( A_prev layer -- A )` | Single-layer forward: `A = act(W * A_prev + b)` |
| `forward` | `( X network -- Y )` | Full forward pass through all layers |
| `forward-cache` | `( X network -- Y cache )` | Forward pass caching intermediates for backprop |
| `predict` | `( X network -- Y )` | Alias for `forward` |

### Backpropagation

| Word | Stack Effect | Description |
|------|-------------|-------------|
| `layer-backward` | `( dA Z A_prev W act'-xt -- dA_prev dW db )` | Single-layer backward pass |
| `backward` | `( Y_hat Y network cache -- grads )` | Full backward pass, returns per-layer gradient maps |

### Training

| Word | Stack Effect | Description |
|------|-------------|-------------|
| `sgd-update` | `( network grads lr -- network )` | SGD parameter update: `W' = W - lr*dW` |
| `train-step` | `( X Y network lr -- network loss )` | One forward-backward-update cycle |
| `train` | `( X Y network lr epochs -- network )` | Train for N epochs, printing loss every 500 |

## Activation Functions

The library works with any activation function that has a matching derivative. Built-in options:

| Activation | Forward | Derivative |
|-----------|---------|------------|
| ReLU | `mat-relu` | `mat-relu'` |
| Sigmoid | `mat-sigmoid` | `mat-sigmoid'` |
| Tanh | `mat-tanh` | `mat-tanh'` |

Pass these as execution tokens to `make-layer`:

```forth
# Inside a : definition, use [']
: build-net
    2 4 ['] mat-relu ['] mat-relu' make-layer
    4 1 ['] mat-sigmoid ['] mat-sigmoid' make-layer
    2 make-network
;

# At the top level, use '
2 4 ' mat-relu ' mat-relu' make-layer
```

## Notes

- **Loss function**: `train-step` uses MSE loss with batch-averaged gradients. Loss is printed via `mat-mse`.
- **Optimizer**: SGD only. No momentum, no Adam. The learning rate is a raw float passed to `train`.
- **Allocation**: Every matrix operation allocates a new `HeapMatrix`. For toy problems (XOR, small datasets) this is fine. For larger problems, allocation pressure may dominate.
- **Batch size**: The library handles arbitrary batch sizes. Each column of X is one sample. For online learning, use a single-column X.

## Example

See `examples/tui/xor-mlp.til` for a complete XOR training example.
