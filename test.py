def fnv1a64(b: bytes) -> int:
    h = 0xcbf29ce484222325
    for c in b:
        h ^= c
        h = (h * 0x100000001b3) & 0xFFFFFFFFFFFFFFFF
    return h

k = "name"
key = k.encode()
print(fnv1a64(key))
