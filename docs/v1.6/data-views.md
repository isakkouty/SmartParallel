# SmartParallel v1.6 data views

```cpp
template <typename T, std::size_t Rank>
class smart::data::View;

template <typename T> using VectorView = View<T, 1>;
template <typename T> using MatrixView = View<T, 2>;
```

Views are non-owning host-memory descriptors. They store a pointer, compile-time rank, extents, element strides, and optional explicitly declared alignment. Strides are always measured in **elements**, not bytes.

## Construction rules

- no hidden allocation or ownership;
- a null pointer is valid only when logical size is zero;
- extent, stride, address-span, and byte-span arithmetic is overflow checked;
- writable-to-read-only conversion is const-correct;
- contiguous row-major helpers compute strides safely;
- explicitly strided views support padded rows and sub-fields;
- public indexing checks dimensions and indices and throws on invalid access;
- scientific operations validate their complete view/alias contract once at entry, then use internal validated pointer/stride kernels so bounds and overflow checks are not repeated for every hot-loop element;
- writable operations require a unique logical mapping and reject zero-stride or lattice-aliasing outputs;
- writable-to-const conversion preserves extents, strides, logical size, and declared alignment;
- only host memory is supported in v1.6.

## Overlap and aliasing

The base view does not claim universal alias analysis. It reports:

- exact mapping when pointer, extents, and strides match;
- disjoint or overlapping conservative address spans where computable;
- `Unknown` when safe computation is impossible; such ambiguity is rejected by writable scientific operations.

Operations define their own contracts:

- AXPY permits exact same mapping and disjoint mappings; partial or ambiguous overlap is rejected.
- `stencil_2d` requires disjoint input and output.
- dot and norm are read-only.

The validated claim is: **view construction, overlap detection where computable, and operation-specific alias contracts are validated.**

## Vision adapter

`smart::vision::ImageView` remains unchanged. `smart::vision::as_element_matrix_view` explicitly flattens image channels into matrix columns and converts byte stride to element stride. The reverse adapter requires a unit column stride and an exactly matching width × channel extent.
