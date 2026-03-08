# E2E Tests

Minimal single-node end-to-end tests for `shunya_store`.

Run:

```bash
cmake -S . -B build
cmake --build build -j
pytest tests/e2e -v
```

Current scope:

- starts one real `shunya_store` process
- connects over TCP
- sends raw RESP
- asserts on the wire response
