acc_x = 0
acc_y = 0
acc_z = 0
n = 10_000_000
for i in range(n):
    acc_x += i
    acc_y += i * 2
    acc_z += i * 3
print(acc_x + acc_y + acc_z)
