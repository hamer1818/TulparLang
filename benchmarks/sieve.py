def sieve(n):
    flags = bytearray(n + 1)
    count = 0
    for i in range(2, n + 1):
        if not flags[i]:
            count += 1
            for k in range(i * i, n + 1, i):
                flags[k] = 1
    return count

print(sieve(100000))
